/**
 * @file MusicalTimeline.h
 * @brief Turns song positions into bars and realignment periods, for host meters and the pattern's own meter.
 */
#pragma once

#include "Engine/BlockContext.h"
#include "Engine/Pattern.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace beatsmith
{

/**
 * The span of song time in which all lanes count their steps from the same anchor.
 *
 * Every lane's step k starts at anchor + k × (step duration). Steps before begin or from end on belong to a
 * neighbouring period. For Realignment::songStartOnly the period is the whole song.
 */
struct RealignmentPeriod
{
    double beginInQuarterNotes = 0.0;  ///< First position that belongs to this period (may be -infinity).
    double anchorInQuarterNotes = 0.0; ///< Where step 0 of every lane starts.
    double endInQuarterNotes = 0.0;    ///< First position after this period (may be +infinity).
};

/**
 * Knows where bars are and which realignment period a song position belongs to.
 *
 * With the pattern's own meter, bars are simply counted from the song start. When following the host, bars
 * come from the host's time signature and bar position. VST3 hosts do not report bar numbers, so the timeline
 * remembers each time-signature change it sees during playback (a *meter segment*: where it starts, its time
 * signature, and the number of its first bar). Everything else is computed from those segments and the song
 * position, never counted per block, so loops and jumps always land on the same result.
 *
 * Limitation: when playback starts after a time-signature change that has not been played through yet, the
 * timeline cannot know where that change was. It then assumes the current meter has applied since the song
 * start, and learns the real change once playback passes it.
 */
class MusicalTimeline
{
public:
    /** How many time-signature changes are remembered; when full, the memory starts over. */
    static constexpr std::size_t maximumRememberedMeterSegments = 32;

    /** Positions closer than this (in quarter notes) count as equal; far below one sample. */
    static constexpr double positionTolerance = 1.0e-9;

    /** Forgets all remembered time-signature changes. */
    void forgetMeterSegments() noexcept { numberOfSegments = 0; }

    /**
     * Learns the host's meter at the start of a block. Call once per block before the queries below.
     * Does nothing when the pattern uses its own meter or the host reports no position.
     * @return Where a newly learned meter begins, or NaN if the meter is already known. The host reports a
     *         time-signature change only from the first block after it, so a change may lie up to one block
     *         in the past; the engine uses this to play steps it could not know about in time.
     */
    double observeBlockStart (const BlockContext& context, const Pattern& pattern) noexcept;

    /** @return The realignment period that contains the position. */
    RealignmentPeriod getPeriodContaining (double positionInQuarterNotes, const Pattern& pattern) const noexcept;

    /** @return The number of the bar containing the position (bar 0 starts at the song start). */
    std::int64_t getBarNumberAt (double positionInQuarterNotes, const Pattern& pattern) const noexcept;

    /** @return Where a bar starts, in quarter notes. */
    double getBarStart (std::int64_t barNumber, const Pattern& pattern) const noexcept;

    /** @return How many meter segments are remembered (for tests). */
    std::size_t getNumberOfRememberedMeterSegments() const noexcept { return numberOfSegments; }

private:
    /** A stretch of the song with one time signature. */
    struct MeterSegment
    {
        double startInQuarterNotes = 0.0;
        int numerator = 4;
        int denominator = 4;
        std::int64_t firstBarNumber = 0;

        double getBarLength() const noexcept { return 4.0 * numerator / denominator; }
    };

    MeterSegment getSegmentAt (double positionInQuarterNotes, const Pattern& pattern) const noexcept;
    MeterSegment getSegmentContainingBar (std::int64_t barNumber, const Pattern& pattern) const noexcept;

    /** @return The start of the first remembered segment after the given one, or +infinity. */
    double getStartOfSegmentAfter (const MeterSegment& segment, const Pattern& pattern) const noexcept;

    void rememberSegment (const MeterSegment& segment) noexcept;

    std::array<MeterSegment, maximumRememberedMeterSegments> segments {}; ///< Sorted by start.
    std::size_t numberOfSegments = 0;
};

} // namespace beatsmith

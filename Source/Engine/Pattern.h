/**
 * @file Pattern.h
 * @brief The data model of a pattern: meter, lanes and steps, plus the rules that keep every value valid.
 *
 * All types are plain values with fixed sizes (no heap memory), so a pattern can be copied on the audio thread
 * and stored in a project without surprises. The enum values are stored in projects: never reorder or remove
 * them, only append.
 */
#pragma once

#include "Engine/Fraction.h"

#include <array>
#include <cstdint>

namespace beatsmith
{

//==============================================================================
// Limits

constexpr int maximumNumberOfLanes = 16;       ///< Lanes per pattern.
constexpr int maximumStepsPerLane = 128;       ///< Steps per lane.
constexpr int maximumBarsPerPattern = 64;      ///< Pattern length and realignment interval, in bars.
constexpr int maximumMeterNumerator = 32;      ///< Largest top number of a time signature.
constexpr int maximumTupletNumber = 32;        ///< Largest N and M of a free tuplet "N in the time of M".
constexpr int maximumGateInSixtyFourths = 256; ///< Longest note: 256/64 = 4 whole notes.

//==============================================================================
/**
 * How long one step of a lane lasts: a note value, optionally as a tuplet.
 *
 * "N steps in the time of M notes": a straight 1/16 is 1 in 1, an eighth-note triplet is 1/8 with 3 in 2, a
 * dotted eighth is 1/8 with 2 in 3, and five even steps across a 4/4 bar is 1/4 with 5 in 4.
 */
struct StepRate
{
    int noteValue = 16;  ///< The base note: 1 = whole, 2 = half, 4 = quarter … 64. Powers of two only.
    int tupletSteps = 1; ///< N: how many steps …
    int tupletSpan = 1;  ///< M: … fit into the time of this many base notes.

    /** @return The duration of one step in quarter notes, exactly. */
    Fraction getDurationInQuarterNotes() const
    {
        // Multiplied in 64 bits, so even unvalidated values cannot overflow.
        return Fraction (4 * (std::int64_t) tupletSpan, (std::int64_t) noteValue * tupletSteps);
    }

    /** @return True if all fields are allowed and the step lasts between a 1/64 note and four whole notes. */
    bool isValid() const;

    bool operator== (const StepRate& other) const noexcept
    {
        return noteValue == other.noteValue && tupletSteps == other.tupletSteps && tupletSpan == other.tupletSpan;
    }
};

/** Shortest allowed step: a 1/64 note (1/16 of a quarter note). */
inline const Fraction shortestStepDuration { 1, 16 };

/** Longest allowed step: four whole notes. */
inline const Fraction longestStepDuration { 16, 1 };

//==============================================================================
/**
 * A time signature with its beat grouping, e.g. 7/8 felt as 2+2+3.
 *
 * The grouping says how the bar's units (eighths in 7/8, quarters in 5/4) are bundled into felt beats. It
 * drives the grid shading, accent helpers and swing; it does not change when steps play.
 */
struct Meter
{
    int numerator = 4;   ///< Top number: units per bar, 1 … 32.
    int denominator = 4; ///< Bottom number: the unit, 1, 2, 4, 8, 16 or 32.
    int numberOfGroups = 4;
    std::array<std::uint8_t, maximumMeterNumerator> groupSizes { 1, 1, 1, 1 }; ///< Units per felt beat.

    /** @return The length of one bar in quarter notes, exactly (7/8 → 7/2). */
    Fraction getBarLengthInQuarterNotes() const { return Fraction (4 * (std::int64_t) numerator, denominator); }

    /** @return True if both meters have the same time signature (the grouping is ignored). */
    bool hasSameTimeSignatureAs (const Meter& other) const noexcept
    {
        return numerator == other.numerator && denominator == other.denominator;
    }
};

/**
 * @return A meter with the usual grouping for a time signature: quarter- and half-note meters count every
 *         unit (5/4 → 1+1+1+1+1); eighth and shorter units are grouped in threes when the numerator is a
 *         multiple of 3 (3/8, 6/8, 9/8, 12/8), otherwise in twos with a final three for odd numerators
 *         (5/8 → 2+3, 7/8 → 2+2+3, 11/8 → 2+2+2+2+3). Invalid input is clamped first.
 */
Meter makeMeterWithDefaultGrouping (int numerator, int denominator);

//==============================================================================
/** One cell of a lane. */
struct Step
{
    bool isOn = false;             ///< True if the step is a trig (plays).
    bool isAccented = false;       ///< True if the trig plays at the lane's accent velocity.
    std::uint8_t velocity = 100;   ///< Velocity when not accented, 1 … 127.
    std::int16_t gateOverride = 0; ///< Note length in 1/64 notes for this step only; 0 = use the lane's gate.
};

/** One drum voice: which note it plays and how its row of steps runs. */
struct Lane
{
    int noteNumber = 36;        ///< MIDI note, 0 … 127.
    int channel = 10;           ///< MIDI channel, 1 … 16 (10 is the General MIDI drum channel).
    int length = 16;            ///< Steps before the lane repeats, 1 … 128.
    StepRate rate;              ///< How long one step lasts.
    int gateInSixtyFourths = 2; ///< Note length in 1/64 notes (2 = a 1/32 note), 1 … 256.
    int defaultVelocity = 100;  ///< Velocity of a newly set trig, 1 … 127.
    int accentVelocity = 127;   ///< Velocity of accented trigs, 1 … 127.
    bool isMuted = false;       ///< A muted lane plays nothing.
    bool isSoloed = false;      ///< While any lane is soloed, only soloed lanes play.
    std::array<Step, maximumStepsPerLane> steps {};

    /** @return The velocity a trig on this step plays at. */
    int getVelocityOf (const Step& step) const noexcept { return step.isAccented ? accentVelocity : step.velocity; }

    /** @return The note length of a trig on this step, in quarter notes, exactly. */
    Fraction getGateOf (const Step& step) const
    {
        return Fraction (step.gateOverride > 0 ? step.gateOverride : gateInSixtyFourths, 16);
    }
};

//==============================================================================
/** Where a pattern takes its meter from. Stored in projects: append only. */
enum class MeterSource : int
{
    followHost = 0, ///< The host's time signature and bar positions.
    ownMeter = 1    ///< The pattern's own meter, with bars counted from the song start.
};

/** When all lanes jump back to their first step together. Stored in projects: append only. */
enum class Realignment : int
{
    songStartOnly = 0,       ///< Lanes run from the song start and never realign.
    everyPatternRestart = 1, ///< At the start of every pattern cycle (every lengthInBars bars).
    everyNumberOfBars = 2    ///< Every realignEveryBars bars.
};

/** What a time-signature change in the host does to the pattern cycle. Stored in projects: append only. */
enum class MeterChangeBehaviour : int
{
    restartAtChange = 0, ///< The pattern cycle restarts at the bar where the meter changes.
    keepPhase = 1        ///< Bars are counted on from the song start as if nothing happened.
};

/** A complete pattern: its timing settings and its lanes. */
struct Pattern
{
    MeterSource meterSource = MeterSource::followHost;
    Meter ownMeter;       ///< Used when meterSource is ownMeter.
    int lengthInBars = 1; ///< Length of one pattern cycle, 1 … 64 bars.
    Realignment realignment = Realignment::everyPatternRestart;
    int realignEveryBars = 4; ///< Interval for Realignment::everyNumberOfBars, 1 … 64.
    MeterChangeBehaviour meterChangeBehaviour = MeterChangeBehaviour::restartAtChange;
    int numberOfLanes = 0; ///< Lanes in use, 0 … 16; lanes beyond this are ignored.
    std::array<Lane, maximumNumberOfLanes> lanes {};
};

/**
 * Brings every value of a pattern into its allowed range, so a pattern from any source (a damaged project,
 * a crafted preset) can be played safely. Invalid rates become a straight 1/16, invalid meters 4/4, and
 * groupings that do not add up to the numerator get the default grouping.
 */
void sanitizePattern (Pattern& pattern);

} // namespace beatsmith

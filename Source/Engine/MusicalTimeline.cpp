#include "Engine/MusicalTimeline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace beatsmith
{

namespace
{
constexpr double infinity = std::numeric_limits<double>::infinity();

/** Integer division that rounds towards minus infinity (-1 / 4 = -1), unlike C++'s `/`. */
std::int64_t divideRoundingDown (std::int64_t dividend, std::int64_t divisor) noexcept
{
    const auto quotient = dividend / divisor;
    return (dividend % divisor != 0 && ((dividend < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

bool isUsableTimeSignature (int numerator, int denominator) noexcept
{
    if (numerator < 1 || numerator > maximumMeterNumerator)
        return false;
    for (int candidate = 1; candidate <= 32; candidate *= 2)
        if (denominator == candidate)
            return true;
    return false;
}
} // namespace

double MusicalTimeline::observeBlockStart (const BlockContext& context, const Pattern& pattern) noexcept
{
    constexpr double nothingNew = std::numeric_limits<double>::quiet_NaN();
    if (pattern.meterSource == MeterSource::ownMeter || ! context.hasPositionInQuarterNotes ||
        ! std::isfinite (context.positionInQuarterNotes))
        return nothingNew;

    MeterSegment observed;
    if (context.hasTimeSignature &&
        isUsableTimeSignature (context.timeSignatureNumerator, context.timeSignatureDenominator))
    {
        observed.numerator = context.timeSignatureNumerator;
        observed.denominator = context.timeSignatureDenominator;
    }
    const double barLength = observed.getBarLength();
    const double position = context.positionInQuarterNotes;

    // The start of the current bar: from the host if it reports a plausible one, otherwise computed as if this
    // meter had applied since the song start.
    double currentBarStart = std::floor (position / barLength + positionTolerance) * barLength;
    if (context.hasLastBarStart && std::isfinite (context.lastBarStartInQuarterNotes) &&
        context.lastBarStartInQuarterNotes <= position + positionTolerance &&
        position - context.lastBarStartInQuarterNotes < barLength + positionTolerance)
        currentBarStart = context.lastBarStartInQuarterNotes;

    if (numberOfSegments > 0)
    {
        const auto known = getSegmentAt (currentBarStart, pattern);
        if (known.numerator == observed.numerator && known.denominator == observed.denominator)
            return nothingNew;

        // A new meter from this bar on (or, after a jump, a guess at where it started).
        const bool isAfterKnownStart = currentBarStart >= known.startInQuarterNotes - positionTolerance;
        observed.startInQuarterNotes = currentBarStart;
        observed.firstBarNumber =
            isAfterKnownStart ? known.firstBarNumber +
                                    std::llround ((currentBarStart - known.startInQuarterNotes) / known.getBarLength())
                              : std::llround (currentBarStart / barLength);
    }
    else
    {
        // First observation: assume this meter has applied since the song start.
        const auto barsBefore = std::llround (currentBarStart / barLength);
        observed.startInQuarterNotes = currentBarStart - (double) barsBefore * barLength;
        observed.firstBarNumber = 0;
    }
    rememberSegment (observed);
    return observed.startInQuarterNotes;
}

void MusicalTimeline::rememberSegment (const MeterSegment& segment) noexcept
{
    // Replace a segment that starts at the same place.
    for (std::size_t index = 0; index < numberOfSegments; ++index)
    {
        if (std::abs (segments[index].startInQuarterNotes - segment.startInQuarterNotes) <= positionTolerance)
        {
            segments[index] = segment;
            return;
        }
    }

    if (numberOfSegments == segments.size())
        numberOfSegments = 0; // memory full: start over rather than keep stale guesses

    // Insert sorted by start.
    std::size_t insertAt = numberOfSegments;
    while (insertAt > 0 && segments[insertAt - 1].startInQuarterNotes > segment.startInQuarterNotes)
    {
        segments[insertAt] = segments[insertAt - 1];
        --insertAt;
    }
    segments[insertAt] = segment;
    ++numberOfSegments;
}

MusicalTimeline::MeterSegment MusicalTimeline::getSegmentAt (double positionInQuarterNotes,
                                                             const Pattern& pattern) const noexcept
{
    if (pattern.meterSource == MeterSource::ownMeter)
    {
        MeterSegment own;
        own.numerator = pattern.ownMeter.numerator;
        own.denominator = pattern.ownMeter.denominator;
        return own;
    }
    if (numberOfSegments == 0)
        return {}; // 4/4 from the song start until the host tells otherwise

    // The last segment starting at or before the position; positions before the first one use the first.
    std::size_t found = 0;
    for (std::size_t index = 1; index < numberOfSegments; ++index)
        if (segments[index].startInQuarterNotes <= positionInQuarterNotes + positionTolerance)
            found = index;
    return segments[found];
}

MusicalTimeline::MeterSegment MusicalTimeline::getSegmentContainingBar (std::int64_t barNumber,
                                                                        const Pattern& pattern) const noexcept
{
    if (pattern.meterSource == MeterSource::ownMeter || numberOfSegments == 0)
        return getSegmentAt (0.0, pattern);

    std::size_t found = 0;
    for (std::size_t index = 1; index < numberOfSegments; ++index)
        if (segments[index].firstBarNumber <= barNumber)
            found = index;
    return segments[found];
}

double MusicalTimeline::getStartOfSegmentAfter (const MeterSegment& segment, const Pattern& pattern) const noexcept
{
    if (pattern.meterSource == MeterSource::ownMeter)
        return infinity;
    for (std::size_t index = 0; index < numberOfSegments; ++index)
        if (segments[index].startInQuarterNotes > segment.startInQuarterNotes + positionTolerance)
            return segments[index].startInQuarterNotes;
    return infinity;
}

std::int64_t MusicalTimeline::getBarNumberAt (double positionInQuarterNotes, const Pattern& pattern) const noexcept
{
    const auto segment = getSegmentAt (positionInQuarterNotes, pattern);
    const double barsIntoSegment =
        (positionInQuarterNotes - segment.startInQuarterNotes) / segment.getBarLength() + positionTolerance;
    return segment.firstBarNumber + (std::int64_t) std::floor (barsIntoSegment);
}

double MusicalTimeline::getBarStart (std::int64_t barNumber, const Pattern& pattern) const noexcept
{
    const auto segment = getSegmentContainingBar (barNumber, pattern);
    return segment.startInQuarterNotes + (double) (barNumber - segment.firstBarNumber) * segment.getBarLength();
}

RealignmentPeriod MusicalTimeline::getPeriodContaining (double positionInQuarterNotes,
                                                        const Pattern& pattern) const noexcept
{
    if (pattern.realignment == Realignment::songStartOnly)
        return { -infinity, 0.0, infinity };

    const std::int64_t barsPerPeriod =
        pattern.realignment == Realignment::everyPatternRestart ? pattern.lengthInBars : pattern.realignEveryBars;

    const auto segment = getSegmentAt (positionInQuarterNotes, pattern);
    const bool restartsAtMeterChanges = pattern.meterSource == MeterSource::followHost &&
                                        pattern.meterChangeBehaviour == MeterChangeBehaviour::restartAtChange;
    const std::int64_t firstBarOfCounting = restartsAtMeterChanges ? segment.firstBarNumber : 0;

    const auto barNumber = getBarNumberAt (positionInQuarterNotes, pattern);
    const auto periodIndex =
        divideRoundingDown (barNumber - firstBarOfCounting, std::max<std::int64_t> (1, barsPerPeriod));
    const auto anchorBar = firstBarOfCounting + periodIndex * barsPerPeriod;

    RealignmentPeriod period;
    period.anchorInQuarterNotes = getBarStart (anchorBar, pattern);
    period.beginInQuarterNotes = period.anchorInQuarterNotes;
    period.endInQuarterNotes = getBarStart (anchorBar + barsPerPeriod, pattern);

    // With "restart at change", a cycle is cut short where the next known meter begins.
    if (restartsAtMeterChanges)
        period.endInQuarterNotes = std::min (period.endInQuarterNotes, getStartOfSegmentAfter (segment, pattern));

    // Never return an empty or backwards period (possible only with inconsistent host data).
    if (period.endInQuarterNotes <= period.anchorInQuarterNotes + positionTolerance)
        period.endInQuarterNotes = period.anchorInQuarterNotes + segment.getBarLength();
    return period;
}

} // namespace beatsmith

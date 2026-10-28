/**
 * @file Fraction.h
 * @brief An exact fraction, used for musical durations (step lengths, bar lengths) so lanes never drift.
 */
#pragma once

#include <cstdint>
#include <numeric>

namespace beatsmith
{

/**
 * An exact, always reduced fraction with a positive denominator.
 *
 * Musical durations are kept as fractions of a quarter note: a 1/16 note is 1/4, an eighth-note triplet is
 * 1/3, a bar of 7/8 is 7/2. They are converted to floating point only at the very end, which keeps lanes of
 * different rates exactly in line with each other however long a song runs.
 */
class Fraction
{
public:
    /** Creates 0/1. */
    constexpr Fraction() = default;

    /**
     * Creates numerator/denominator, reduced. A denominator of 0 gives 0/1 rather than undefined behaviour.
     */
    constexpr Fraction (std::int64_t numeratorToUse, std::int64_t denominatorToUse)
    {
        if (denominatorToUse == 0)
            return;
        if (denominatorToUse < 0)
        {
            numeratorToUse = -numeratorToUse;
            denominatorToUse = -denominatorToUse;
        }
        const auto divisor = std::gcd (numeratorToUse, denominatorToUse);
        numerator = numeratorToUse / (divisor == 0 ? 1 : divisor);
        denominator = denominatorToUse / (divisor == 0 ? 1 : divisor);
    }

    /** @return The numerator (may be negative). */
    constexpr std::int64_t getNumerator() const noexcept { return numerator; }

    /** @return The denominator (always positive). */
    constexpr std::int64_t getDenominator() const noexcept { return denominator; }

    /** @return The value as a double. */
    constexpr double toDouble() const noexcept { return (double) numerator / (double) denominator; }

    /** @return This fraction multiplied by another one, reduced. */
    constexpr Fraction operator* (const Fraction& other) const
    {
        return Fraction (numerator * other.numerator, denominator * other.denominator);
    }

    constexpr bool operator== (const Fraction& other) const noexcept
    {
        return numerator == other.numerator && denominator == other.denominator;
    }
    constexpr bool operator!= (const Fraction& other) const noexcept { return ! (*this == other); }

    /** @return True if this fraction is smaller than the other one. */
    constexpr bool operator< (const Fraction& other) const noexcept
    {
        return numerator * other.denominator < other.numerator * denominator;
    }

private:
    std::int64_t numerator = 0;
    std::int64_t denominator = 1;
};

} // namespace beatsmith

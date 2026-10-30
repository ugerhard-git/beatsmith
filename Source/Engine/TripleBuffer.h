/**
 * @file TripleBuffer.h
 * @brief Hands values from one thread to another without locks: the editor writes patterns, the audio thread
 *        reads the newest one.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace beatsmith
{

/**
 * A lock-free, wait-free triple buffer for one writer thread and one reader thread.
 *
 * Three copies of the value exist. The writer fills its own copy and then *publishes* it by swapping it with
 * the shared middle copy; the reader *takes* the newest value by swapping its own copy with the middle one.
 * Neither side ever waits for the other, and neither side ever sees a half-written value. If the writer
 * publishes several times before the reader looks, the reader simply gets the newest value.
 *
 * Nothing allocates after construction, so the reader side is safe on the audio thread. The value type
 * should be cheap enough to copy once per change (a Pattern is about 13 kB).
 *
 * @tparam Value A copyable type.
 */
template <typename Value> class TripleBuffer
{
public:
    /** Creates three copies of the initial value; the reader starts with it. */
    explicit TripleBuffer (const Value& initialValue = Value {}) : buffers { initialValue, initialValue, initialValue }
    {
    }

    //==============================================================================
    // Writer thread only

    /** Copies a value into the writer's copy and publishes it for the reader. */
    void write (const Value& value)
    {
        buffers[writeIndex] = value;
        publish();
    }

    //==============================================================================
    // Reader thread only

    /**
     * Takes the newest published value, if there is one the reader has not seen yet.
     * @return True if getReadValue() now holds a new value.
     */
    bool takeNewestValue() noexcept
    {
        if ((middleState.load (std::memory_order_relaxed) & hasNewValueFlag) == 0)
            return false;
        const auto previousMiddle = middleState.exchange (readIndex, std::memory_order_acq_rel);
        readIndex = previousMiddle & indexMask;
        return true;
    }

    /** @return The value the reader holds: the newest one at the time of the last takeNewestValue(). */
    const Value& getReadValue() const noexcept { return buffers[readIndex]; }

private:
    static constexpr std::uint8_t indexMask = 0x3;
    static constexpr std::uint8_t hasNewValueFlag = 0x4;

    void publish() noexcept
    {
        const auto previousMiddle =
            middleState.exchange ((std::uint8_t) (writeIndex | hasNewValueFlag), std::memory_order_acq_rel);
        writeIndex = previousMiddle & indexMask;
    }

    std::array<Value, 3> buffers;
    std::uint8_t writeIndex = 0; ///< Only touched by the writer.
    std::uint8_t readIndex = 2;  ///< Only touched by the reader.

    /** Index of the middle copy (bits 0–1) and whether it holds a value the reader has not taken (bit 2). */
    std::atomic<std::uint8_t> middleState { 1 };
};

} // namespace beatsmith

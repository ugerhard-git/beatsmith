/**
 * @file NoteEvent.h
 * @brief The notes the engine sends out, and a fixed-capacity list to collect them in real time.
 */
#pragma once

#include <cstddef>
#include <vector>

namespace beatsmith
{

/** One note-on or note-off the engine wants to send, positioned inside the current block. */
struct NoteEvent
{
    int samplePosition = 0; ///< Offset from the start of the block, 0 … numSamples - 1.
    int channel = 1;        ///< MIDI channel, 1 … 16.
    int noteNumber = 36;    ///< MIDI note number, 0 … 127.
    int velocity = 100;     ///< 1 … 127 for a note-on; ignored for a note-off.
    bool isNoteOn = true;   ///< True for a note-on, false for a note-off.
};

/**
 * A list of NoteEvents with a fixed capacity.
 *
 * All memory is reserved in the constructor, so adding and clearing never allocate and are safe on the
 * audio thread. When the list is full, further events are rejected instead of growing the list.
 */
class NoteEventList
{
public:
    /** @param maximumNumberOfEvents How many events one block can hold at most. */
    explicit NoteEventList (std::size_t maximumNumberOfEvents) : capacity (maximumNumberOfEvents)
    {
        events.reserve (capacity);
    }

    /**
     * Adds an event if there is room.
     * @return False if the list was full and the event was dropped.
     */
    bool add (const NoteEvent& event)
    {
        if (events.size() >= capacity)
            return false;
        events.push_back (event);
        return true;
    }

    /**
     * Sorts the events by sample position, keeping the order in which they were added for events at the same
     * sample (so a note-off added before a note-on of the same note stays before it). An insertion sort: it
     * allocates no memory and takes linear time for lists that are already nearly sorted, which is the norm.
     */
    void sortBySamplePosition() noexcept
    {
        for (std::size_t index = 1; index < events.size(); ++index)
        {
            const auto event = events[index];
            auto insertAt = index;
            while (insertAt > 0 && events[insertAt - 1].samplePosition > event.samplePosition)
            {
                events[insertAt] = events[insertAt - 1];
                --insertAt;
            }
            events[insertAt] = event;
        }
    }

    /** Removes all events but keeps the reserved memory. */
    void clear() noexcept { events.clear(); }

    /** @return The events in the order they were added. */
    const std::vector<NoteEvent>& getEvents() const noexcept { return events; }

    /** @return How many events the list can hold. */
    std::size_t getCapacity() const noexcept { return capacity; }

private:
    std::size_t capacity;
    std::vector<NoteEvent> events;
};

} // namespace beatsmith

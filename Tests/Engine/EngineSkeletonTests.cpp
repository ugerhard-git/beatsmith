/**
 * @file EngineSkeletonTests.cpp
 * @brief Tests of the phase 0 engine: it is silent, and its event list never grows beyond its capacity.
 */
#include "../TestFramework.h"

#include "Engine/BeatsmithEngine.h"

using namespace beatsmith;

TEST_CASE ("engine: produces no notes while the host plays")
{
    BeatsmithEngine engine;
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    BlockContext context;
    context.numSamples = 512;
    context.hostIsPlaying = true;

    for (int blockIndex = 0; blockIndex < 100; ++blockIndex)
    {
        engine.process (context, output);
        EXPECT (output.getEvents().empty());
    }
}

TEST_CASE ("engine: produces no notes while the host is stopped")
{
    BeatsmithEngine engine;
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    BlockContext context;
    context.numSamples = 512;
    context.hostIsPlaying = false;

    engine.process (context, output);
    EXPECT (output.getEvents().empty());
}

TEST_CASE ("engine: process clears what the list held before")
{
    BeatsmithEngine engine;
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    output.add (NoteEvent {});

    BlockContext context;
    context.numSamples = 256;
    engine.process (context, output);
    EXPECT (output.getEvents().empty());
}

TEST_CASE ("engine: releasing all notes sends nothing when nothing sounds")
{
    BeatsmithEngine engine;
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    engine.releaseAllNotes (output);
    EXPECT (output.getEvents().empty());
}

TEST_CASE ("note event list: rejects events beyond its capacity")
{
    NoteEventList list (3);
    EXPECT (list.add (NoteEvent {}));
    EXPECT (list.add (NoteEvent {}));
    EXPECT (list.add (NoteEvent {}));
    EXPECT (! list.add (NoteEvent {}));
    EXPECT (list.getEvents().size() == 3);
    EXPECT (list.getCapacity() == 3);
}

TEST_CASE ("note event list: clearing keeps the reserved memory")
{
    NoteEventList list (16);
    const auto capacityBefore = list.getEvents().capacity();
    list.add (NoteEvent {});
    list.clear();
    EXPECT (list.getEvents().empty());
    EXPECT (list.getEvents().capacity() == capacityBefore);
    EXPECT (capacityBefore >= 16);
}

/**
 * @file BlockContext.h
 * @brief What the engine needs to know about one audio block: its length, the sample rate and the host's
 *        transport state.
 *
 * The plug-in layer fills a BlockContext from the host's playhead for every block. The engine never talks to
 * the host directly, which keeps it free of JUCE and testable on its own. Hosts do not always report every
 * value, so each optional value has a "has…" flag.
 */
#pragma once

namespace beatsmith
{

/** Information about the audio block that is being processed. */
struct BlockContext
{
    double sampleRate = 48000.0;        ///< Samples per second.
    int numSamples = 0;                 ///< Number of samples in this block.
    bool hostIsPlaying = false;         ///< True while the host's transport is running.
    double tempoBeatsPerMinute = 120.0; ///< Host tempo in quarter notes per minute.

    bool hasPositionInQuarterNotes = false;
    double positionInQuarterNotes = 0.0; ///< Song position at the block start, in quarter notes from song start.

    bool hasLastBarStart = false;
    double lastBarStartInQuarterNotes = 0.0; ///< Where the bar containing the block start begins.

    bool hasTimeSignature = false;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    bool isLooping = false; ///< True while the host's loop (cycle) is switched on.
    bool hasLoopPoints = false;
    double loopStartInQuarterNotes = 0.0;
    double loopEndInQuarterNotes = 0.0;
};

} // namespace beatsmith

/**
 * @file BlockContext.h
 * @brief What the engine needs to know about one audio block: its length, the sample rate and the host's
 *        transport state.
 *
 * The plug-in layer fills a BlockContext from the host's playhead for every block. The engine never talks to
 * the host directly, which keeps it free of JUCE and testable on its own.
 */
#pragma once

namespace beatsmith
{

/** Information about the audio block that is being processed. */
struct BlockContext
{
    double sampleRate = 48000.0;       ///< Samples per second.
    int numSamples = 0;                ///< Number of samples in this block.
    bool hostIsPlaying = false;        ///< True while the host's transport is running.
    double tempoBeatsPerMinute = 120.; ///< Host tempo in quarter notes per minute.
};

} // namespace beatsmith

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2014-2026 James Dooley (aka formuls)

/*
 * AudioRecorder.h
 *
 * Records the formuls engine's output to a WAV file.
 *
 * The file always matches what the engine is actually producing: the sample
 * rate the audio device really opened at, and one WAV channel per engine
 * output channel (so "2 channels" in the main window gives a stereo file and
 * "14 channels" gives a 14-channel file). Files larger than 4 GB are written
 * as RF64, which juce::WavAudioFormat switches to on its own, so long
 * multichannel takes are not truncated.
 *
 * Threading model
 * ---------------
 * write() is called from the audio device thread by FormulsEngine and must
 * never block or allocate. It hands the block to a
 * juce::AudioFormatWriter::ThreadedWriter, which copies it into a FIFO; the
 * actual disk writing happens on backgroundThread. Following JUCE's own
 * AudioRecordingDemo, the writer pointer is swapped under writerLock -- the
 * only lock the audio thread ever takes here, and one that is never held
 * while touching the disk.
 *
 * start()/stop() are called from the message thread.
 *
 * Where the file goes
 * -------------------
 * Recording writes straight to a timestamped file in the folder returned by
 * getDefaultRecordingDirectory() -- ~/formulsAudioRecordings, or a mounted
 * USB stick on the appliance; RecordingLocation.h decides which and says
 * why. The "save as" dialog the
 * main window shows afterwards moves that finished file wherever the user
 * wants it. Nothing is ever recorded to a temporary file that could be lost:
 * if the app quits mid-take, or the user cancels the dialog, the audio is
 * still sitting in that folder.
 */

#pragma once

#include <JuceHeader.h>

namespace formuls
{

class AudioRecorder
{
public:
    AudioRecorder() = default;
    ~AudioRecorder();

    /** Sample format of the recorded WAV files: 32 writes 32-bit IEEE
        float, 24 writes 24-bit PCM.

        **Float, because the engine clips and the file should not.** Nothing
        between the Pd patch and this writer limits the signal (see
        docs/headroom/README.md): a polyphonic instance playing a chord sums
        past 1.0, and at any integer depth
        juce::AudioFormatWriter::writeFromFloatArrays clamps it -- `samp >=
        1.0` becomes INT_MAX -- so the take is hard clipped in the file, with
        the overshoot gone for good. A take measured on 29 October 2026 had
        8,784 frames pinned at exactly 0 dBFS across 27 of its 467 seconds.

        At 32 bits juce::WavAudioFormat writes IEEE float unless the options
        ask for SampleFormat::integral, and the float path hands the samples
        to the writer untouched (it reinterprets the float* as int* and
        copies 32 bits verbatim). Values above 1.0 therefore survive into the
        file intact, and the level can be brought down afterwards with the
        waveform still whole.

        This does nothing for the live output -- the audio device clamps
        too, so the room still hears it. It protects the recording only.

        Costs: files are 33% larger than at 24-bit (384 KB/s stereo, 2.7 MB/s
        at 14 channels), and 32-bit float is slightly less universally
        readable than 24-bit PCM, though every current DAW handles it. */
    static constexpr int bitsPerSample = 32;

    /** The folder new recordings are written to: a mounted USB stick when
        the appliance has one, otherwise ~/formulsAudioRecordings. See
        RecordingLocation.h. */
    static juce::File getDefaultRecordingDirectory();

    /** A fresh, non-clashing "formuls-YYYY-MM-DD-HHMMSS.wav" in the folder
        above. */
    static juce::File makeRecordingFile();

    /** Starts writing to `destination`. Returns Result::ok(), or a failure
        with a user-displayable message. */
    juce::Result start (const juce::File& destination, int numChannels, double sampleRate);

    /** Finishes the file and flushes it to disk. Safe to call when not
        recording. The file stays available from getFile(). */
    void stop();

    bool isRecording() const noexcept   { return activeWriter.load() != nullptr; }

    /** The file currently being written, or the last one completed. */
    juce::File getFile() const          { return file; }

    /** How many channels the current file has (0 when never started). */
    int getNumChannels() const noexcept { return numRecordChannels; }

    /** Called from the audio thread with one block of de-interleaved output.
        Blocks whose channel count doesn't match the file are ignored. */
    void write (const float* const* channelData, int numChannels, int numSamples);

private:
    // Size of the ThreadedWriter's FIFO, in samples per channel: enough to
    // cover a slow disk hiccup without ever making the audio thread wait.
    static constexpr int fifoSizeSamples = 32768;

    // Write buffer for the output file. Much smaller than JUCE's 16 KB
    // default, so a take reaches the disk as it is encoded rather than only
    // when the file is closed -- see the comment in start() for why that
    // matters on the appliance.
    static constexpr size_t writeBufferBytes = 1024;

    juce::TimeSliceThread backgroundThread { "formuls recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;

    // Read by the audio thread; only written on the message thread, and only
    // with writerLock held.
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    juce::CriticalSection writerLock;

    juce::File file;
    int numRecordChannels = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRecorder)
};

} // namespace formuls

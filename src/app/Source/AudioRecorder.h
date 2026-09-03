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
 * getDefaultRecordingDirectory() (Music/formuls). The "save as" dialog the
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

    /** Bit depth of the recorded WAV files. 24-bit PCM is the most widely
        readable choice; set this to 32 to write 32-bit float instead, which
        juce::WavAudioFormat does automatically at that depth and which
        cannot clip. */
    static constexpr int bitsPerSample = 24;

    /** The folder new recordings are written to (Music/formuls, falling back
        to Documents and then the temp folder if that cannot be created). */
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

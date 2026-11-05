/*
 * AudioRecorder.cpp -- see AudioRecorder.h for an overview.
 */

#include "AudioRecorder.h"
#include "RecordingLocation.h"

namespace formuls
{

AudioRecorder::~AudioRecorder()
{
    stop();
}

//==============================================================================
juce::File AudioRecorder::getDefaultRecordingDirectory()
{
    // One place decides this now, because the appliance can answer it with a
    // USB stick -- see RecordingLocation.h. Takes used to land in
    // Music/formuls; they go to ~/formulsAudioRecordings instead, so the
    // folder a tablet user is told about is the same one the desktop Record
    // button writes to.
    return recording::chooseDirectory();
}

juce::File AudioRecorder::makeRecordingFile()
{
    return recording::makeFile();
}

//==============================================================================
juce::Result AudioRecorder::start (const juce::File& destination,
                                   int numChannels, double sampleRate)
{
    stop();   // finish anything still open before starting a new take

    if (numChannels < 1)
        return juce::Result::fail ("Nothing to record: the engine has no output channels.");

    if (sampleRate <= 0.0)
        return juce::Result::fail ("Nothing to record: the audio device is not running.");

    destination.getParentDirectory().createDirectory();
    destination.deleteFile();

    // A small write buffer, deliberately: JUCE's default is 16 KB, and nothing
    // in this path flushes before the file is closed. OggWriter writes its
    // three Vorbis header packets in its constructor and calls output->flush()
    // only in its destructor, and it does not override flush(), so there is no
    // way to ask for one mid-take.
    //
    // With the default buffer a short take therefore reaches the disk only at
    // stop(): a 25 s take of near-silence is about 3.9 KB, so the whole thing
    // -- headers included -- sits in the buffer and the file on disk is 0
    // bytes. If the process does not get to run its shutdown, the take is not
    // truncated, it is lost entirely. That was measured on a booted image on
    // 4 Nov 2026: 3 of 7 power-offs left a 0-byte .ogg, in every case where
    // systemd gave the app ~1.9 s to stop instead of the ~5-6 s it needs.
    //
    // At this size the headers land immediately -- so the file is valid from
    // the first moment -- and encoded pages land as libvorbis emits them. What
    // is still at risk is only the page the encoder has not emitted yet, not
    // the session. The cost is one write() per kilobyte of compressed audio,
    // which at a realistic ~20 KB/s is about 20 syscalls a second on the
    // recorder's own background thread.
    auto fileStream = std::make_unique<juce::FileOutputStream> (destination,
                                                                writeBufferBytes);

    if (! fileStream->openedOk())
        return juce::Result::fail ("Could not create " + destination.getFullPathName());

    // createWriterFor() takes the stream over on success and leaves it alone
    // on failure, so nothing here leaks either way.
    std::unique_ptr<juce::OutputStream> stream { std::move (fileStream) };

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (numChannels)
                                                   .withBitsPerSample (bitsPerSample));

    if (writer == nullptr)
    {
        stream.reset();
        destination.deleteFile();
        return juce::Result::fail ("Could not write a "
                                   + juce::String (numChannels) + "-channel WAV file at "
                                   + juce::String ((int) sampleRate) + " Hz.");
    }

    backgroundThread.startThread (juce::Thread::Priority::normal);

    threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
        writer.release(), backgroundThread, fifoSizeSamples);

    file = destination;
    numRecordChannels = numChannels;

    // Publishing the pointer is the moment the audio thread starts writing.
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter = threadedWriter.get();
    }

    juce::Logger::writeToLog ("recording started: " + destination.getFullPathName()
                              + ", " + juce::String (numChannels) + " channels @ "
                              + juce::String ((int) sampleRate) + " Hz, "
                              + juce::String (bitsPerSample) + "-bit");
    return juce::Result::ok();
}

void AudioRecorder::stop()
{
    // Detach the audio thread first, then let the ThreadedWriter destructor
    // drain the FIFO and close the file.
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter = nullptr;
    }

    const bool wasRecording = threadedWriter != nullptr;
    threadedWriter.reset();
    backgroundThread.stopThread (2000);

    if (wasRecording)
        juce::Logger::writeToLog ("recording stopped: " + file.getFullPathName()
                                  + " (" + juce::File::descriptionOfSizeInBytes (file.getSize()) + ")");
}

//==============================================================================
void AudioRecorder::write (const float* const* channelData, int numChannels, int numSamples)
{
    const juce::ScopedLock sl (writerLock);

    if (auto* writer = activeWriter.load();
        writer != nullptr && numChannels == numRecordChannels && numSamples > 0)
        writer->write (channelData, numSamples);
}

} // namespace formuls

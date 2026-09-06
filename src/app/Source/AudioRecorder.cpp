/*
 * AudioRecorder.cpp -- see AudioRecorder.h for an overview.
 */

#include "AudioRecorder.h"

namespace formuls
{

// Subfolder created inside the user's Music folder for new recordings.
static const juce::String kRecordingFolderName { "formuls" };

AudioRecorder::~AudioRecorder()
{
    stop();
}

//==============================================================================
juce::File AudioRecorder::getDefaultRecordingDirectory()
{
    const juce::File candidates[] =
    {
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
    };

    for (const auto& parent : candidates)
    {
        if (! parent.isDirectory())
            continue;

        auto folder = parent.getChildFile (kRecordingFolderName);

        if (folder.isDirectory() || folder.createDirectory().wasOk())
            return folder;
    }

    return juce::File::getSpecialLocation (juce::File::tempDirectory);
}

juce::File AudioRecorder::makeRecordingFile()
{
    const auto now = juce::Time::getCurrentTime();
    const auto name = "formuls-" + now.formatted ("%Y-%m-%d-%H%M%S") + ".wav";

    return getDefaultRecordingDirectory().getChildFile (name)
                                         .getNonexistentSibling (false);
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

    auto fileStream = std::make_unique<juce::FileOutputStream> (destination);

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

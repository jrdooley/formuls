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

juce::File AudioRecorder::makeRecordingFile (const juce::File& folder,
                                             const juce::String& prefix,
                                             const juce::String& extension)
{
    const auto now = juce::Time::getCurrentTime();
    const auto name = prefix + "-" + now.formatted ("%Y-%m-%d-%H%M%S") + "." + extension;

    return folder.getChildFile (name).getNonexistentSibling (false);
}

//==============================================================================
juce::Result AudioRecorder::start (const juce::File& destination,
                                   int numChannels, double sampleRate,
                                   Format format)
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

    const bool ogg = format == Format::oggVorbis;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (numChannels)
                             // Vorbis encodes from float; 32 is the only depth it accepts
                             .withBitsPerSample (ogg ? 32 : bitsPerSample)
                             .withQualityOptionIndex (ogg ? oggQualityIndex : 0);

    std::unique_ptr<juce::AudioFormatWriter> writer;

    if (ogg)
        writer = juce::OggVorbisAudioFormat().createWriterFor (stream, options);
    else
        writer = juce::WavAudioFormat().createWriterFor (stream, options);

    if (writer == nullptr)
    {
        stream.reset();
        destination.deleteFile();
        return juce::Result::fail ("Could not write a "
                                   + juce::String (numChannels) + "-channel "
                                   + (ogg ? "Ogg Vorbis" : "WAV") + " file at "
                                   + juce::String ((int) sampleRate) + " Hz.");
    }

    firstBlockMs = 0;
    samplesWritten = 0;

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
                              + (ogg ? "Ogg Vorbis q0." + juce::String (oggQualityIndex)
                                     : juce::String (bitsPerSample) + "-bit WAV"));
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
    {
        // currentTimeMillis() is a clock read: no lock, no allocation.
        if (firstBlockMs.load (std::memory_order_relaxed) == 0)
            firstBlockMs = juce::Time::currentTimeMillis();

        writer->write (channelData, numSamples);
        samplesWritten.fetch_add (numSamples, std::memory_order_relaxed);
    }
}

} // namespace formuls

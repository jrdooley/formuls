/*
 * RecordingLocation.h
 *
 * Where a take is written, and whether the tablet may start one.
 *
 * Two things decide the folder:
 *
 *   - a USB stick, if the appliance has mounted one. The box runs headless,
 *     so a recording the user cannot carry away is of limited use: when a
 *     stick is there the take is written straight onto it, and when the take
 *     ends the stick can be pulled without copying anything first.
 *   - otherwise the user's own folder, ~/formulsAudioRecordings. On a desktop
 *     that is the only case; on the appliance it is the fallback that stops a
 *     forgotten stick costing a take.
 *
 * The stick is found by looking in the directory the appliance mounts
 * removable media under, NOT by talking to udev or udisks. The app is
 * unprivileged and must behave identically on a desktop, where that directory
 * simply does not exist -- the same reasoning as the trigger directory in
 * PowerOff.h. The appliance side of this is formuls-usb-mount in the
 * formuls-embedded overlay, which mounts each stick as
 * <removableRoot()>/<label>.
 *
 * Writing straight to the stick is a deliberate trade, chosen over recording
 * locally and copying on stop:
 *   + the file is on removable media the whole time, so a take survives the
 *     machine losing power, and nothing has to be copied when it ends;
 *   - pulling the stick mid-take truncates that file, and a FAT32 stick
 *     cannot hold a single file over 4 GB (about 3 hours of 32-bit float
 *     stereo at 48 kHz, or 27 minutes of 14 channels).
 *     juce::WavAudioFormat switches to RF64 past that point, which FAT32
 *     still cannot store, so a take that long wants an exFAT stick or the
 *     internal disk.
 */

#pragma once

#include <JuceHeader.h>

namespace formuls
{
namespace recording
{

/** The id of the GUI's record button in gui/_main.json. Hidden by the GUI
    server wherever nothing is listening for it -- the plugin, which shares
    this GUI but has no transport of its own to record. */
static constexpr const char* guiWidgetId = "audiorecord";

/** The OSC address that button sends on. */
static constexpr const char* oscAddress = "/audiorecord";

/** The folder takes are written into, inside the home directory or the
    stick. */
static constexpr const char* folderName = "formulsAudioRecordings";

/** Where the appliance mounts USB sticks, one directory per stick.

    FORMULS_RECORDING_USB_ROOT overrides it, so the whole route -- including
    "a stick appeared" and "the stick went away" -- can be exercised on a
    machine with no USB stick in it. */
inline juce::File removableRoot()
{
    return juce::File (juce::SystemStats::getEnvironmentVariable (
                           "FORMULS_RECORDING_USB_ROOT", "/media/formuls"));
}

/** ~/formulsAudioRecordings, created if it is not there yet. Returns the
    temp folder only if even the home directory cannot be written, which
    means something is badly wrong but is still better than failing to
    record. */
inline juce::File homeDirectory()
{
    const juce::File candidates[] =
    {
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
    };

    for (const auto& parent : candidates)
    {
        if (! parent.isDirectory())
            continue;

        auto folder = parent.getChildFile (folderName);

        if (folder.isDirectory() || folder.createDirectory().wasOk())
            return folder;
    }

    return juce::File::getSpecialLocation (juce::File::tempDirectory);
}

/** The recording folder on a mounted USB stick, or a default-constructed
    File when there is no usable stick.

    "Usable" is tested by actually creating the folder, not by asking whether
    the path looks writable: a stick can be mounted read-only (a dirty NTFS
    volume, or one with its write-protect switch on) and still answer yes to
    every permission check, because the refusal comes from the mount rather
    than from the file mode. The first stick that accepts the folder wins --
    with two plugged in there is no way to tell which one the user meant, and
    picking the first consistently beats picking an arbitrary one. */
inline juce::File removableDirectory()
{
    const auto root = removableRoot();

    if (! root.isDirectory())
        return {};

    juce::Array<juce::File> mounts;
    root.findChildFiles (mounts, juce::File::findDirectories, false);

    // findChildFiles does not promise an order, and the user should get the
    // same stick every time rather than one that depends on readdir.
    std::sort (mounts.begin(), mounts.end(),
               [] (const juce::File& a, const juce::File& b)
               { return a.getFileName() < b.getFileName(); });

    for (const auto& mount : mounts)
    {
        auto folder = mount.getChildFile (folderName);

        if (folder.isDirectory() || folder.createDirectory().wasOk())
            return folder;
    }

    return {};
}

/** The folder the next take should go in: the stick when there is one, the
    user's folder otherwise. */
inline juce::File chooseDirectory()
{
    if (auto stick = removableDirectory(); stick != juce::File())
        return stick;

    return homeDirectory();
}

/** Whether a host that says it can record should still be believed.

    FORMULS_RECORDING_AVAILABLE=0 forces the GUI's record button to be
    hidden on a build that would otherwise serve it. That is how the gate
    gets tested: the only host that genuinely hides it is the plugin, which
    needs a plugin host to run, so without this the one case the gate exists
    for could never be observed on a booted machine. PowerOff.h carries
    FORMULS_POWEROFF_FILE for the same reason.

    Anything other than "0" leaves the host's own answer alone. */
inline bool overriddenUnavailable()
{
    return juce::SystemStats::getEnvironmentVariable (
               "FORMULS_RECORDING_AVAILABLE", {}).trim() == "0";
}

/** How much room to leave free on the destination's filesystem, in bytes.

    This is not about losing the end of a take. On the appliance the fallback
    destination is the internal disk, which is also the root filesystem: a
    take that fills it does not merely fail, it takes journald, logging and
    anything else that needs to write down with it, on a headless box with no
    screen to say so. 32-bit float stereo at 48 kHz is 384 KB/s, about
    1.4 GB an hour, and the appliance's root is 5.5 GB -- so "record and
    forget" is well within reach of filling it.

    256 MB by default, which is roughly 11 minutes of stereo headroom -- but
    only about 100 seconds at 14 channels (2.7 MB/s), so a multichannel take
    is stopped much sooner after the warning than a stereo one.
    FORMULS_RECORDING_MIN_FREE_MB overrides it; 0 disables the guard. */
inline juce::int64 minimumFreeBytes()
{
    const auto setting = juce::SystemStats::getEnvironmentVariable (
                             "FORMULS_RECORDING_MIN_FREE_MB", {}).trim();

    const auto megabytes = setting.isNotEmpty() ? setting.getLargeIntValue() : (juce::int64) 256;

    return juce::jmax ((juce::int64) 0, megabytes) * 1024 * 1024;
}

/** Free space on the volume `destination` lives on, or -1 when it cannot be
    determined (JUCE returns 0 for an unknown volume, which is indivisible
    from a genuinely full one -- treat "unknown" as "do not block"). */
inline juce::int64 freeBytesFor (const juce::File& destination)
{
    auto folder = destination.isDirectory() ? destination : destination.getParentDirectory();

    const auto free = folder.getBytesFreeOnVolume();

    return free > 0 ? free : (juce::int64) -1;
}

/** Whether there is room to start, or to keep going. True when the guard is
    switched off or the free space cannot be read: this decides whether to
    refuse a recording, and refusing on a number we do not trust is worse
    than the risk it guards against. */
inline bool hasRoomFor (const juce::File& destination)
{
    const auto reserve = minimumFreeBytes();

    if (reserve <= 0)
        return true;

    const auto free = freeBytesFor (destination);

    return free < 0 || free >= reserve;
}

/** A fresh, non-clashing "formuls-YYYY-MM-DD-HHMMSS.wav" in that folder. */
inline juce::File makeFile()
{
    const auto now = juce::Time::getCurrentTime();
    const auto name = "formuls-" + now.formatted ("%Y-%m-%d-%H%M%S") + ".wav";

    return chooseDirectory().getChildFile (name).getNonexistentSibling (false);
}

} // namespace recording
} // namespace formuls

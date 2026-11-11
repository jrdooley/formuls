// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2014-2026 James Dooley (aka formuls)

/*
 * PowerOff.h
 *
 * One place to ask "can this build power the machine off?", shared by the
 * code that wires the GUI's request (MainComponent) and the code that serves
 * the GUI (EmbeddedGuiServer).
 *
 * Powering the machine down is an appliance feature, not an app feature. The
 * app never does it itself: it creates /run/formuls-ctl/poweroff, and a
 * root-side systemd path unit in the formuls-embedded overlay is watching for
 * that file and runs "systemctl poweroff". Off the appliance there is no such
 * watcher, so the request cannot be honoured -- and a POWER OFF button that
 * cannot power anything off should not be on screen. On a desktop the user
 * starts and stops formuls from their operating system.
 *
 * The test is whether the trigger directory exists, rather than a build-time
 * flag, because the directory is the thing that marks a machine as able to
 * act on the request: the overlay creates it from tmpfiles.d, so it is there
 * whether or not the engine is running, and it is absent everywhere else. A
 * compile-time flag would be wrong the first time an appliance binary ran on
 * a desktop, or the reverse.
 */

#pragma once

#include <JuceHeader.h>

namespace formuls
{
namespace poweroff
{

/** The id of the GUI's POWER OFF modal in gui/_main.json. Hidden by the GUI
    server when isAvailable() is false. */
static constexpr const char* guiWidgetId = "poweroffmodal";

/** The file whose creation asks root to power the machine off.

    FORMULS_POWEROFF_FILE overrides the path so the whole route can be
    exercised -- and this header's answer flipped -- without powering a
    machine off. */
inline juce::File triggerFile()
{
    return juce::File (juce::SystemStats::getEnvironmentVariable (
                           "FORMULS_POWEROFF_FILE", "/run/formuls-ctl/poweroff"));
}

/** True when something is in a position to act on a power-off request, i.e.
    the directory the trigger file goes in exists. False on every desktop
    build, where the POWER OFF control is hidden and never wired. */
inline bool isAvailable()
{
    return triggerFile().getParentDirectory().isDirectory();
}

} // namespace poweroff
} // namespace formuls

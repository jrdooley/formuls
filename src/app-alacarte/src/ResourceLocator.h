/*
 * ResourceLocator.h -- the à la carte counterpart of the JUCE app's
 * ResourceLocator.h, using std::filesystem instead of juce::File.
 *
 * Finds the folder holding the formuls runtime resources (pd/_main.pd,
 * gui/node, gui/open-stage-control, gui/_main.json).
 *
 * Search order:
 *   1. the FORMULS_RESOURCE_ROOT environment variable, if set (handy for
 *      testing against an already-assembled bundle's Resources folder),
 *   2. <bundle>/Contents/Resources when running from an .app bundle,
 *   3. walking up from the executable, accepting any ancestor that contains
 *      pd/_main.pd directly or inside a "src" child (the repository layout).
 *
 * Returns an empty path when nothing is found.
 */

#pragma once

#include <filesystem>

namespace formuls
{

std::filesystem::path findResourceRoot();

} // namespace formuls

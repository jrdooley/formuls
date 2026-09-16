/*
 * ResourceLocator.h
 *
 * Finds the folder that holds the formuls runtime resources:
 *
 *   <resource root>/pd/_main.pd            the Pd patch loaded by libpd
 *   <resource root>/pd/externals/          compiled Pd externals (faust, abl_link~)
 *   <resource root>/gui/node               bundled node.js binary
 *   <resource root>/gui/open-stage-control the Open Stage Control node package
 *   <resource root>/gui/_main.json         the Open Stage Control layout
 *
 * In the packaged app this is <formuls.app>/Contents/Resources (populated by
 * build-macOS.sh). During development -- running the binary straight out of
 * src/app/Builds/... -- the locator walks up from the executable until it
 * finds a folder containing pd/_main.pd (which matches the repository's
 * src/ folder), so the app also runs unbundled.
 *
 * On Android the resources travel inside the APK and are unpacked into the
 * app's private storage (see android::extractResources() in
 * AndroidPlatform.h); this returns that folder once unpacking has finished.
 */

#pragma once

#include <JuceHeader.h>

#if JUCE_ANDROID
 #include "AndroidPlatform.h"
#endif

namespace formuls
{

/** Returns the resource root folder, or an invalid (default-constructed)
    juce::File if it cannot be found. Check the result with
    juce::File::isDirectory(). */
inline juce::File findResourceRoot()
{
    auto containsResources = [] (const juce::File& dir)
    {
        return dir.getChildFile ("pd").getChildFile ("_main.pd").existsAsFile();
    };

   #if JUCE_ANDROID
    // There is no bundle or source tree to search on Android: only the
    // folder the APK's resources were extracted to.
    if (const auto root = android::getResourceRoot();
        android::areResourcesReady() && containsResources (root))
        return root;

    return {};
   #else

    // 1. The packaged app bundle: formuls.app/Contents/Resources
    auto appFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    auto bundleResources = appFile.getChildFile ("Contents").getChildFile ("Resources");

    if (containsResources (bundleResources))
        return bundleResources;

    // 2. Development fallback: walk up from the executable looking for the
    //    resources directly (e.g. a "build" staging folder) or inside a
    //    "src" child (the repository layout).
    auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                   .getParentDirectory();

    for (int i = 0; i < 10 && dir.isDirectory(); ++i)
    {
        if (containsResources (dir))
            return dir;

        if (containsResources (dir.getChildFile ("src")))
            return dir.getChildFile ("src");

        auto parent = dir.getParentDirectory();

        if (parent == dir)  // reached the filesystem root
            break;

        dir = parent;
    }

    return {};
   #endif
}

} // namespace formuls

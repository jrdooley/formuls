/*
 * AndroidPlatform.h
 *
 * Everything the Android build of formuls needs that the desktop builds do
 * not. Only compiled on Android (JUCE_ANDROID); the rest of the app calls
 * into it from #if JUCE_ANDROID blocks, so the macOS and Linux builds are
 * unaffected.
 *
 * The C++ half lives here and in AndroidPlatform.cpp; the Java half is in
 * src/android/java/net/formuls/android (reached through JNI). How the pieces
 * fit together:
 *
 *   Desktop                               Android
 *   -------                               -------
 *   resources inside the .app bundle      resources packed into the APK's
 *   (pd/, gui/)                           assets/formuls, extracted to app
 *                                         storage on first launch
 *                                         (extractResources)
 *
 *   Pd externals loaded from              externals linked in statically
 *   pd/externals                          (src/android/native)
 *
 *   node child process running            nodejs-mobile in the app's
 *   Open Stage Control                    separate ":gui" process
 *                                         (startGuiServer / stopGuiServer)
 *
 *   stray-server sweep over all           the ":gui" process is found and
 *   processes on the machine              killed by name
 *
 *   app keeps running when its            a foreground service keeps the
 *   window is in the background           engine and GUI server alive
 *                                         (startAudioService)
 *
 *   control GUI in a browser              control GUI in an embedded WebView
 *                                         (GuiWebView), or a browser app
 *
 *   recordings/presets in ~/Music,        app-specific shared storage;
 *   ~/Documents, via file dialogs         recordings are copied into the
 *                                         shared Music/formuls folder
 */

#pragma once

#include <JuceHeader.h>

#if JUCE_ANDROID

namespace formuls::android
{

//==============================================================================
// Resources

/** Where the extracted resources live: a folder laid out exactly like the
    desktop resource root (pd/_main.pd, gui/_main.json, gui/open-stage-control
    ...). May not exist until extractResources() has run. */
juce::File getResourceRoot();

/** True when getResourceRoot() holds a complete extraction of the resources
    in the installed APK. */
bool areResourcesReady();

/** Unpacks assets/formuls from the APK into getResourceRoot(), unless an
    extraction of this exact APK is already there. Reinstalling or updating
    the app replaces it. Slow the first time (Open Stage Control is several
    MB of files), so call it from a background thread. */
juce::Result extractResources();

//==============================================================================
// Services

/** Starts / stops the foreground service that keeps formuls alive in the
    background (see FormulsAudioService.java). */
void startAudioService();
void stopAudioService();

/** Starts Open Stage Control on nodejs-mobile in the ":gui" process.
    @param nodeArgs  the argument list a desktop node would be run with,
                     starting with "node"
    @param env       "NAME=value" environment entries for node */
juce::Result startGuiServer (const juce::StringArray& nodeArgs, const juce::StringArray& env);

/** Stops the GUI server, including one left over from an earlier run of the
    app that crashed, and waits up to timeoutMs for its process to be gone.
    Returns the number of processes killed. */
int stopGuiServer (int timeoutMs);

bool isGuiServerRunning();

//==============================================================================
// Storage

/** The app's private files folder (internal storage). */
juce::File getFilesDir();

/** The app's private cache folder. */
juce::File getCacheDir();

/** The app's own folder on shared storage for a type such as "Music", or its
    root for an empty type. Readable and writable by both formuls processes
    without any permission. */
juce::File getExternalFilesDir (const juce::String& type);

/** Copies a finished recording to the shared Music/formuls folder. Returns
    the path to show the user, or an empty string if it could not be done. */
juce::String publishRecording (const juce::File& recording);

//==============================================================================
// Activity

/** Asks, once, for permission to show the "formuls is running" notification
    (Android 13 and later). */
void requestNotificationPermission();

/** Sends formuls to the background without closing it, like pressing Home. */
void moveToBackground();

/** Called when the Back button is pressed while the embedded GUI has focus
    (the WebView receives the key, not JUCE's own view). Wired to
    JUCEApplication::backButtonPressed() by AndroidPlatform.cpp. */
extern std::function<bool()> onBackButtonFromWebView;

//==============================================================================
/** The control GUI, shown inside the app in a native Android WebView.

    A native view is always drawn on top of JUCE's own drawing, so nothing
    JUCE paints can overlap this component -- lay controls out around it. */
class GuiWebView : public juce::AndroidViewComponent
{
public:
    GuiWebView();
    ~GuiWebView() override;

    void loadUrl (const juce::String& url);
    void reload();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuiWebView)
};

} // namespace formuls::android

#endif // JUCE_ANDROID

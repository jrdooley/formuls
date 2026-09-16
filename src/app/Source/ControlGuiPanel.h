/*
 * ControlGuiPanel.h
 *
 * Android only: shows the formuls control GUI inside the app, so a single
 * tablet can run the engine and play it without switching to a browser.
 *
 *   +--------------------------------------------------------------+
 *   | < formuls   Reload   Open in browser        status text      |  toolbar
 *   +--------------------------------------------------------------+
 *   |                                                              |
 *   |          Open Stage Control, in a native WebView             |
 *   |                                                              |
 *   +--------------------------------------------------------------+
 *
 * "< formuls" (or the Back button) returns to the settings screen without
 * stopping anything; the GUI stays loaded and reappears as it was.
 *
 * The GUI server takes a few seconds to come up after Start (node has to
 * boot in its own process first), so open() does not load the page straight
 * away: it polls the server's port and loads the page once it answers,
 * rather than showing the WebView's "page not available" error.
 *
 * The WebView is thrown away by discard() when the engine stops, so the
 * next Start begins with a fresh page -- a fresh client is what makes Open
 * Stage Control send the patch its full state again.
 */

#pragma once

#include <JuceHeader.h>

#if JUCE_ANDROID

#include "AndroidPlatform.h"

namespace formuls
{

class ControlGuiPanel : public juce::Component,
                        private juce::Timer
{
public:
    ControlGuiPanel();
    ~ControlGuiPanel() override;

    /** Called when the user asks to go back to the settings screen. */
    std::function<void()> onClose;

    /** Shows the panel, creating the WebView and loading the GUI from
        http://127.0.0.1:<port> as soon as the server answers. */
    void open (int guiPort);

    /** Hides the panel; the page stays loaded. */
    void close();

    /** Hides the panel and destroys the WebView (used when the engine stops). */
    void discard();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void setToolbarStatus (const juce::String& text);
    juce::String getUrl() const;

    juce::TextButton backButton    { "< formuls" };
    juce::TextButton reloadButton  { "Reload" };
    juce::TextButton browserButton { "Open in browser" };
    juce::Label statusLabel;

    std::unique_ptr<android::GuiWebView> webView;

    int port = 0;
    bool pageLoaded = false;
    juce::uint32 waitStartedMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlGuiPanel)
};

} // namespace formuls

#endif // JUCE_ANDROID

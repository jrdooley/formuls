/*
 * ControlGuiPanel.cpp -- see ControlGuiPanel.h for an overview.
 *
 * Compiles to nothing outside Android.
 */

#include "ControlGuiPanel.h"

#if JUCE_ANDROID

#include "FormulsLookAndFeel.h"

namespace formuls
{

// How often to check whether the GUI server is accepting connections, and
// how long to wait before saying something is wrong. node on a tablet can
// take several seconds to boot on first launch.
static constexpr int kServerPollIntervalMs = 250;
static constexpr int kServerWaitWarningMs  = 20000;

ControlGuiPanel::ControlGuiPanel()
{
    backButton.onClick = [this]
    {
        if (onClose != nullptr)
            onClose();
    };

    reloadButton.onClick = [this]
    {
        if (webView != nullptr && pageLoaded)
            webView->reload();
    };

    browserButton.onClick = [this]
    {
        juce::URL (getUrl()).launchInDefaultBrowser();
    };

    statusLabel.setFont (juce::FontOptions (style::labelFontHeight));
    statusLabel.setColour (juce::Label::textColourId, style::statusColour);
    statusLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (backButton);
    addAndMakeVisible (reloadButton);
    addAndMakeVisible (browserButton);
    addAndMakeVisible (statusLabel);

    setVisible (false);
}

ControlGuiPanel::~ControlGuiPanel()
{
    stopTimer();
}

juce::String ControlGuiPanel::getUrl() const
{
    return "http://127.0.0.1:" + juce::String (port);
}

void ControlGuiPanel::open (int guiPort)
{
    port = guiPort;
    setVisible (true);
    toFront (false);

    if (webView == nullptr)
    {
        webView = std::make_unique<android::GuiWebView>();
        addAndMakeVisible (*webView);
        pageLoaded = false;
        resized();
    }

    if (! pageLoaded)
    {
        setToolbarStatus ("Starting the control GUI...");
        waitStartedMs = juce::Time::getMillisecondCounter();
        startTimer (kServerPollIntervalMs);
        timerCallback();
    }
}

void ControlGuiPanel::close()
{
    setVisible (false);
}

void ControlGuiPanel::discard()
{
    stopTimer();
    setVisible (false);

    if (webView != nullptr)
    {
        removeChildComponent (webView.get());
        webView.reset();
    }

    pageLoaded = false;
}

void ControlGuiPanel::timerCallback()
{
    if (webView == nullptr || pageLoaded)
    {
        stopTimer();
        return;
    }

    // A refused connection to the loopback address fails immediately, so this
    // does not hold up the message thread while the server is still booting.
    juce::StreamingSocket probe;

    if (probe.connect ("127.0.0.1", port, 100))
    {
        probe.close();
        stopTimer();

        webView->loadUrl (getUrl());
        pageLoaded = true;
        setToolbarStatus ({});
        return;
    }

    if (juce::Time::getMillisecondCounter() - waitStartedMs > (juce::uint32) kServerWaitWarningMs)
        setToolbarStatus ("Still waiting for the GUI server (adb logcat -s formuls-gui)");
}

void ControlGuiPanel::setToolbarStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void ControlGuiPanel::paint (juce::Graphics& g)
{
    g.fillAll (style::guiBackground);
    g.setColour (style::background);
    g.fillRect (getLocalBounds().removeFromTop (style::guiToolbarHeight));
}

void ControlGuiPanel::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (style::guiToolbarHeight).reduced (6, 5);

    backButton.setBounds (toolbar.removeFromLeft (style::guiToolbarButtonWidth));
    toolbar.removeFromLeft (6);
    reloadButton.setBounds (toolbar.removeFromLeft (style::guiToolbarButtonWidth));
    toolbar.removeFromLeft (6);
    browserButton.setBounds (toolbar.removeFromLeft (style::guiToolbarButtonWidth));
    toolbar.removeFromLeft (6);
    statusLabel.setBounds (toolbar);

    // The native view is drawn over everything JUCE draws, so it gets exactly
    // the area below the toolbar and nothing more.
    if (webView != nullptr)
        webView->setBounds (area);
}

} // namespace formuls

#endif // JUCE_ANDROID

/*
 * Main.cpp
 *
 * Application entry point for formuls. Creates the single main window and
 * installs the app-wide look and feel.
 *
 * Lifecycle:
 *   - The window's close button quits the whole application
 *     (MainWindow::closeButtonPressed -> systemRequestedQuit -> quit).
 *   - Quitting destroys MainComponent, whose destructor stops the audio
 *     engine and kills the Open Stage Control child process, so nothing is
 *     left running behind.
 *   - The Pd patch can also quit the app by sending to "formuls-quit"
 *     (see FormulsEngine.h); that ends up in the exact same code path.
 *
 * Styling: window title, size and all colours are defined in
 * FormulsLookAndFeel.h -- edit that file to restyle the app.
 */

#include <JuceHeader.h>
#include "MainComponent.h"
#include "FormulsLookAndFeel.h"

namespace formuls
{

class FormulsApplication : public juce::JUCEApplication
{
public:
    FormulsApplication() = default;

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;   // stops the engine and the o-s-c process
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              style::background,
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (style::windowResizable, style::windowResizable);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        // Closing the window quits the whole app (which in turn stops the
        // audio engine and the Open Stage Control server).
        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    FormulsLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace formuls

// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (formuls::FormulsApplication)

/*
 * FormulsLookAndFeel.h
 *
 * ============================================================================
 *  STYLING THE FORMULS APP -- START HERE
 * ============================================================================
 *
 * Everything that controls how the main formuls window looks lives in this
 * one file, so you never need to hunt through the rest of the code to
 * restyle the app:
 *
 *   - formuls::style   : plain constants for the window (title, size,
 *                        resizability) and the layout metrics used by
 *                        MainComponent (control sizes, margins, fonts).
 *
 *   - FormulsLookAndFeel : a juce::LookAndFeel subclass that sets the
 *                        colour palette for every widget (buttons, combo
 *                        boxes, popup menus, labels...). It is installed
 *                        app-wide in Main.cpp with
 *                        juce::LookAndFeel::setDefaultLookAndFeel().
 *
 * HOW TO...
 *
 *   Change the window title      -> edit style::windowTitle
 *   Change the window size       -> edit style::windowWidth / windowHeight
 *   Make the window resizable    -> set style::windowResizable to true
 *   Change the background colour -> edit style::background (keep it the
 *                                   same as PluginEditor::paint)
 *   Restyle the address panel    -> edit style::addressText /
 *                                   addressFontHeight (the read-only text
 *                                   listing the control GUI's web addresses)
 *   Change the "recording" red    -> edit style::recordActive (the colour the
 *                                   Record button turns while a take is in
 *                                   progress)
 *   Change widget colours        -> add setColour() calls to the
 *                                   FormulsLookAndFeel constructor below
 *                                   (widgets use JUCE's dark scheme, which
 *                                   matches the plugin's window).
 *                                   Each juce widget documents its own
 *                                   ColourIds (e.g. juce::TextButton::
 *                                   buttonColourId) -- see the JUCE headers
 *                                   in modules/juce_gui_basics/widgets/.
 *   Change fonts                 -> edit style::labelFontHeight /
 *                                   buttonFontHeight, or override
 *                                   LookAndFeel methods such as
 *                                   getTextButtonFont() / getComboBoxFont()
 *                                   in FormulsLookAndFeel for full control.
 *   Restyle a single widget      -> call widget.setColour (ColourId, colour)
 *                                   on that widget in MainComponent.cpp;
 *                                   per-widget colours override the
 *                                   LookAndFeel.
 *
 * Going further: for completely custom drawing (gradient buttons, custom
 * combo-box arrows, etc.) override the relevant drawXXX() methods of
 * juce::LookAndFeel_V4 here, e.g. drawButtonBackground(), drawComboBox().
 * See modules/juce_gui_basics/lookandfeel/juce_LookAndFeel_V4.h in JUCE.
 */

#pragma once

#include <JuceHeader.h>

namespace formuls
{

namespace style
{
    // ------------------------------------------------------------------ window
    static const juce::String windowTitle   { "formuls" };
    constexpr int  windowWidth      = 450;
    constexpr int  windowHeight     = 560;
    constexpr bool windowResizable  = false;

    // ------------------------------------------------------------------ colours
    // The same as the plugin's window (src/plugin/Source/PluginEditor.cpp):
    // a near-black background, white text, and JUCE's own dark widgets
    // (LookAndFeel_V4's default scheme), which is what the plugin gets too.
    static const juce::Colour background    { 0xff1e1f22 };   // window background
    static const juce::Colour textColour    { juce::Colours::white };   // title, labels
    static const juce::Colour statusColour  { juce::Colours::white.withAlpha (0.85f) };   // status line
    static const juce::Colour addressText   { juce::Colours::white.withAlpha (0.85f) };   // GUI addresses
    static const juce::Colour meterTrack    { 0xff2a2c30 };   // VU meter bar background
    static const juce::Colour recordActive  { 0xffb3271e };   // Record button while recording

    // ------------------------------------------------------------------ layout
    constexpr int margin          = 20;    // outer margin, as the plugin's window
    constexpr int titleHeight     = 40;    // the "formuls" title
    constexpr int controlHeight   = 32;    // height of combo boxes
    constexpr int controlSpacing  = 14;    // vertical gap between controls
    constexpr int comboWidth      = 280;   // width of the device combo box
    constexpr int buttonWidth     = 150;   // width of the start/stop button
    constexpr int buttonHeight    = 32;    // height of start/stop and record, as the plugin's button
    constexpr int recordButtonWidth = 150; // width of the record button, which
                                           // sits to the right of start/stop
    constexpr int meterHeight     = 20;    // height of each VU meter bar (stereo = 2x + gap)
    constexpr int meterLabelWidth = 12;    // gutter on the left holding the L / R labels
    constexpr int meterLabelGap   = 6;     // gap between a channel label and its bar

    // ------------------------------------------------------------------ fonts
    constexpr float titleFontHeight   = 28.0f;   // bold, as the plugin's title
    constexpr float labelFontHeight   = 15.0f;
    constexpr float buttonFontHeight  = 16.0f;
    constexpr float addressFontHeight = 16.0f;   // GUI addresses, monospaced
    constexpr float meterFontHeight   = 12.0f;   // VU meter L / R channel labels
} // namespace style

/**
 * App-wide look and feel. Installed once in Main.cpp; every widget picks
 * these colours up automatically unless it sets its own overrides.
 */
class FormulsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FormulsLookAndFeel()
    {
        // Buttons, combo boxes and menus keep LookAndFeel_V4's dark scheme,
        // as in the plugin. Only the window and the text are set here.
        setColour (juce::ResizableWindow::backgroundColourId, style::background);
        setColour (juce::DocumentWindow::textColourId,        style::textColour);
        setColour (juce::Label::textColourId,                 style::textColour);

        // The GUI addresses read as plain text on the window, as in the
        // plugin; still a TextEditor so they can be selected and copied.
        setColour (juce::TextEditor::backgroundColourId,      juce::Colours::transparentBlack);
        setColour (juce::TextEditor::textColourId,            style::addressText);
        setColour (juce::TextEditor::outlineColourId,         juce::Colours::transparentBlack);
        setColour (juce::TextEditor::focusedOutlineColourId,  juce::Colours::transparentBlack);
    }
};

} // namespace formuls

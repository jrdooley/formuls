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
 *   Change the background colour -> edit style::background
 *   Restyle the address panel    -> edit style::addressFill / addressText /
 *                                   addressFontHeight (the read-only box
 *                                   listing the control GUI's web addresses)
 *   Change the "recording" red    -> edit style::recordActive (the colour the
 *                                   Record button turns while a take is in
 *                                   progress)
 *   Change widget colours        -> edit the setColour() calls in the
 *                                   FormulsLookAndFeel constructor below.
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
    constexpr int  windowHeight     = 553;
    constexpr bool windowResizable  = false;

    // ------------------------------------------------------------------ colours
    // The green used by the original Tk front end. All other widget colours
    // are derived from these in the FormulsLookAndFeel constructor.
    static const juce::Colour background    { 0xff007a33 };   // window background
    static const juce::Colour widgetFill    { 0xff005c26 };   // buttons / combo boxes
    static const juce::Colour widgetOutline { 0xff003d19 };   // widget borders
    static const juce::Colour textColour    { 0xfff2f2f2 };   // all text
    static const juce::Colour statusColour  { 0xffdcf0dc };   // status line text
    static const juce::Colour addressFill   { 0xff00491e };   // GUI address panel background
    static const juce::Colour addressText   { 0xffeafbea };   // GUI address panel text
    static const juce::Colour recordActive  { 0xffb3271e };   // Record button while recording

    // ------------------------------------------------------------------ layout
    constexpr int margin          = 24;    // outer margin around all controls
    constexpr int controlHeight   = 32;    // height of combo boxes
    constexpr int controlSpacing  = 14;    // vertical gap between controls
    constexpr int comboWidth      = 280;   // width of the two combo boxes
    constexpr int buttonWidth     = 150;   // width of the start/stop button
    constexpr int buttonHeight    = 44;    // height of the start/stop button
    constexpr int recordButtonWidth = 150; // width of the record button, which
                                           // sits to the right of start/stop
    constexpr int buttonGap       = 14;    // horizontal gap between the two buttons
    constexpr int screenshotButtonWidth = 150; // width of the screenshot button
    constexpr int screenshotButtonHeight = 28; // height of the screenshot button
    constexpr int meterHeight     = 20;    // height of each VU meter bar (stereo = 2x + gap)

    // ------------------------------------------------------------------ fonts
    constexpr float labelFontHeight   = 15.0f;
    constexpr float buttonFontHeight  = 16.0f;
    constexpr float addressFontHeight = 13.0f;   // GUI address panel
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
        // window / generic
        setColour (juce::ResizableWindow::backgroundColourId, style::background);
        setColour (juce::DocumentWindow::textColourId,        style::textColour);

        // combo boxes
        setColour (juce::ComboBox::backgroundColourId, style::widgetFill);
        setColour (juce::ComboBox::textColourId,       style::textColour);
        setColour (juce::ComboBox::outlineColourId,    style::widgetOutline);
        setColour (juce::ComboBox::arrowColourId,      style::textColour);

        // popup menus (the drop-down lists of the combo boxes)
        setColour (juce::PopupMenu::backgroundColourId,            style::widgetFill);
        setColour (juce::PopupMenu::textColourId,                  style::textColour);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, style::background);
        setColour (juce::PopupMenu::highlightedTextColourId,       style::textColour);

        // buttons
        setColour (juce::TextButton::buttonColourId,   style::widgetFill);
        setColour (juce::TextButton::buttonOnColourId, style::widgetOutline);
        setColour (juce::TextButton::textColourOffId,  style::textColour);
        setColour (juce::TextButton::textColourOnId,   style::textColour);

        // labels
        setColour (juce::Label::textColourId, style::textColour);

        // the read-only panel listing the control GUI's web addresses
        setColour (juce::TextEditor::backgroundColourId,      style::addressFill);
        setColour (juce::TextEditor::textColourId,            style::addressText);
        setColour (juce::TextEditor::outlineColourId,         style::widgetOutline);
        setColour (juce::TextEditor::focusedOutlineColourId,  style::widgetOutline);
        setColour (juce::TextEditor::highlightColourId,       style::textColour);
        setColour (juce::TextEditor::highlightedTextColourId, style::widgetFill);
    }
};

} // namespace formuls

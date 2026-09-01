/*
 * main.mm -- the à la carte formuls launcher window, in plain Cocoa.
 *
 * This one Objective-C++ file is the entire GUI layer: it replaces
 * juce_gui_basics, juce_graphics, juce_events, Main.cpp, MainComponent and
 * FormulsLookAndFeel from the JUCE app. The engine, child-process and
 * network code it drives (Engine, OpenStageControl, ChildProcess,
 * ResourceLocator) are portable C++ with no Cocoa in them -- porting the
 * launcher to Linux/Windows means rewriting only this file (or swapping it
 * for FLTK/Dear ImGui).
 *
 * Window layout (top to bottom): audio output device pop-up, channel-count
 * pop-up, sample-rate pop-up, Start/Stop button, read-only address panel,
 * status line. Same behaviour as the JUCE app:
 *
 *   - Start boots libpd + Open Stage Control; Stop shuts both down while
 *     the window stays open.
 *   - Closing the window quits the app (and stops everything).
 *   - The Pd patch can quit the app by sending to "formuls-quit".
 *
 * STYLING: the constants right below control the window size and the
 * formuls green palette. Widgets are native Cocoa controls, so they keep
 * the macOS look; the window background, address panel and status line
 * carry the branding.
 *
 * Test hooks (all runtime environment variables, no special build needed):
 *   FORMULS_AUTOSTART=1        press Start automatically 1 s after launch
 *   FORMULS_TEST_SAMPLERATE=N  with AUTOSTART: choose N in the rate menu
 *   FORMULS_SNAPSHOT_PATH=...  write a PNG of the window after 4 s
 *   FORMULS_RESOURCE_ROOT=...  use this resource folder (see ResourceLocator.h)
 */

#import <Cocoa/Cocoa.h>

#include "Engine.h"
#include "OpenStageControl.h"
#include "ResourceLocator.h"

#include <cstdlib>
#include <string>

// ---------------------------------------------------------------- styling
static const CGFloat kWindowWidth  = 450;
static const CGFloat kWindowHeight = 446;

#define GREEN(r, g, b) [NSColor colorWithCalibratedRed:(r) / 255.0 \
                                green:(g) / 255.0 blue:(b) / 255.0 alpha:1.0]

static NSColor* backgroundColour() { return GREEN (0x00, 0x7a, 0x33); }  // window
static NSColor* panelColour()      { return GREEN (0x00, 0x49, 0x1e); }  // address panel
static NSColor* panelTextColour()  { return GREEN (0xea, 0xfb, 0xea); }  // its text
static NSColor* statusColour()     { return GREEN (0xdc, 0xf0, 0xdc); }  // status line

// Menu choices, matching the JUCE app.
static const int kChannelChoices[]    = { 2, 14 };
static const int kSampleRateChoices[] = { 44100, 48000, 88200, 96000 };
static const int kDefaultSampleRate   = 48000;

//==============================================================================
@interface FormulsController : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation FormulsController
{
    NSWindow* window;
    NSPopUpButton* devicePopup;
    NSPopUpButton* channelsPopup;
    NSPopUpButton* sampleRatePopup;
    NSButton* startStopButton;
    NSTextView* addressPanel;
    NSTextField* statusLabel;
    NSTimer* messagePump;

    formuls::Engine engine;
    formuls::OpenStageControl openStageControl;
    std::vector<formuls::Engine::Device> devices;
}

//------------------------------------------------------------------ UI setup

- (void)applicationDidFinishLaunching:(NSNotification*)note
{
    [self buildWindow];

    // Drain libpd's queued patch->app messages on the main thread; this is
    // how the "formuls-quit" message reaches us (see Engine.h).
    engine.onQuitRequested = [] { [NSApp terminate:nil]; };
    messagePump = [NSTimer scheduledTimerWithTimeInterval:0.03
                                                  repeats:YES
                                                    block:^(NSTimer*) {
        self->engine.receiveMessages();
    }];

    [self installTestHooks];

    [NSApp activateIgnoringOtherApps:YES];
}

- (void)buildWindow
{
    window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect (0, 0, kWindowWidth, kWindowHeight)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = @"formuls";
    window.backgroundColor = backgroundColour();
    window.releasedWhenClosed = NO;
    window.delegate = self;
    [window center];

    NSView* content = window.contentView;

    // Paint the content view itself (not just the window) so the formuls
    // green survives every appearance mode and window snapshotting.
    content.wantsLayer = YES;
    content.layer.backgroundColor = backgroundColour().CGColor;
    const CGFloat margin = 24, spacing = 14;
    CGFloat y = kWindowHeight - margin;   // laying out from the top down

    // audio output device pop-up
    y -= 26;
    devicePopup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect (margin, y, 280, 26) pullsDown:NO];
    devices = formuls::Engine::listOutputDevices();
    for (auto& device : devices)
        [devicePopup addItemWithTitle:
            [NSString stringWithUTF8String:device.name.c_str()]];
    [content addSubview:devicePopup];

    // channel count pop-up
    y -= 26 + spacing;
    channelsPopup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect (margin, y, 140, 26) pullsDown:NO];
    for (int channels : kChannelChoices)
        [channelsPopup addItemWithTitle:
            [NSString stringWithFormat:@"%d channels", channels]];
    [content addSubview:channelsPopup];

    // sample rate pop-up
    y -= 26 + spacing;
    sampleRatePopup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect (margin, y, 140, 26) pullsDown:NO];
    for (int rate : kSampleRateChoices)
    {
        [sampleRatePopup addItemWithTitle:[NSString stringWithFormat:@"%d Hz", rate]];
        if (rate == kDefaultSampleRate)
            [sampleRatePopup selectItemAtIndex:sampleRatePopup.numberOfItems - 1];
    }
    [content addSubview:sampleRatePopup];

    // start/stop button
    y -= 34 + spacing;
    startStopButton = [[NSButton alloc]
        initWithFrame:NSMakeRect (margin, y, 150, 34)];
    startStopButton.title = @"Start formuls";
    startStopButton.bezelStyle = NSBezelStyleRounded;
    startStopButton.target = self;
    startStopButton.action = @selector (startStopClicked:);
    [content addSubview:startStopButton];

    // status line (bottom), then the address panel fills what is left
    statusLabel = [NSTextField labelWithString:@"Ready."];
    statusLabel.frame = NSMakeRect (margin, margin - 6,
                                    kWindowWidth - 2 * margin, 20);
    statusLabel.textColor = statusColour();
    statusLabel.font = [NSFont systemFontOfSize:13];
    [content addSubview:statusLabel];

    const CGFloat panelTop = y - spacing;
    const CGFloat panelBottom = margin + 24;
    NSScrollView* scroll = [[NSScrollView alloc]
        initWithFrame:NSMakeRect (margin, panelBottom,
                                  kWindowWidth - 2 * margin,
                                  panelTop - panelBottom)];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = YES;
    scroll.backgroundColor = panelColour();

    addressPanel = [[NSTextView alloc]
        initWithFrame:NSMakeRect (0, 0, scroll.contentSize.width,
                                  scroll.contentSize.height)];
    addressPanel.editable = NO;          // read-only...
    addressPanel.selectable = YES;       // ...but the addresses can be copied
    addressPanel.backgroundColor = panelColour();
    addressPanel.textColor = panelTextColour();
    addressPanel.font = [NSFont monospacedSystemFontOfSize:12
                                                    weight:NSFontWeightRegular];
    addressPanel.textContainerInset = NSMakeSize (8, 8);
    scroll.documentView = addressPanel;
    [content addSubview:scroll];

    [self updateAddressPanel:false];

    [window makeKeyAndOrderFront:nil];
}

//------------------------------------------------------------ start and stop

- (void)startStopClicked:(id)sender
{
    if (engine.isRunning())
        [self stopEverything];
    else
        [self startEverything];
}

- (void)startEverything
{
    const auto deviceIndex = devicePopup.indexOfSelectedItem;

    if (deviceIndex < 0 || deviceIndex >= (NSInteger) devices.size())
    {
        [self setStatus:@"Please select an audio output device first."];
        return;
    }

    const auto resourceRoot = formuls::findResourceRoot();

    if (resourceRoot.empty())
    {
        [self setStatus:@"Could not locate the formuls resources (pd/_main.pd)."];
        return;
    }

    const int numChannels = kChannelChoices[channelsPopup.indexOfSelectedItem];
    const int requestedRate = kSampleRateChoices[sampleRatePopup.indexOfSelectedItem];

    // 1. the audio engine (libpd running _main.pd)
    if (auto error = engine.start (devices[(size_t) deviceIndex].rtIndex,
                                   numChannels, requestedRate, resourceRoot);
        ! error.empty())
    {
        [self setStatus:[NSString stringWithUTF8String:error.c_str()]];
        return;
    }

    // 2. the Open Stage Control GUI server
    const auto oscError = openStageControl.start (resourceRoot);

    startStopButton.title = @"Stop formuls";
    devicePopup.enabled = NO;
    channelsPopup.enabled = NO;
    sampleRatePopup.enabled = NO;

    [self updateAddressPanel:oscError.empty()];

    // If the device couldn't do the requested rate, say what it's really at.
    std::string status = oscError.empty() ? "Running."
                                          : "Engine running. GUI failed: " + oscError;
    if (engine.actualSampleRate() != requestedRate)
        status += " Device runs at " + std::to_string (engine.actualSampleRate())
                + " Hz (not " + std::to_string (requestedRate) + " Hz).";

    [self setStatus:[NSString stringWithUTF8String:status.c_str()]];
}

- (void)stopEverything
{
    openStageControl.stop();
    engine.stop();

    startStopButton.title = @"Start formuls";
    devicePopup.enabled = YES;
    channelsPopup.enabled = YES;
    sampleRatePopup.enabled = YES;
    [self updateAddressPanel:false];
    [self setStatus:@"Stopped."];
}

//----------------------------------------------------------------- utilities

- (void)setStatus:(NSString*)message
{
    statusLabel.stringValue = message;
}

- (void)updateAddressPanel:(bool)guiIsRunning
{
    if (! guiIsRunning)
    {
        addressPanel.string = @"The control GUI's web addresses will be listed"
                              @" here\nonce formuls is running.";
        return;
    }

    std::string text = "Open the control GUI in a web browser:\n\n"
                       "On this machine:\n";
    const auto addresses = formuls::OpenStageControl::getBrowserAddresses();
    text += "   " + addresses[0] + "\n\n";
    text += "On a tablet or phone on the same network:\n";

    if (addresses.size() > 1)
        for (size_t i = 1; i < addresses.size(); ++i)
            text += "   " + addresses[i] + "\n";
    else
        text += "   (no network connection found -- connect this\n"
                "    machine to a network and press Stop, then Start)\n";

    addressPanel.string = [NSString stringWithUTF8String:text.c_str()];
}

//------------------------------------------------------------------ lifecycle

// Closing the window quits the whole app...
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app
{
    return YES;
}

// ...and quitting always stops the engine and the o-s-c child process.
- (void)applicationWillTerminate:(NSNotification*)note
{
    [messagePump invalidate];
    openStageControl.stop();
    engine.stop();
}

//------------------------------------------------------------------ test hooks

- (void)installTestHooks
{
    if (const char* autostart = std::getenv ("FORMULS_AUTOSTART");
        autostart != nullptr && autostart[0] == '1')
    {
        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, NSEC_PER_SEC),
                        dispatch_get_main_queue(), ^{
            if (const char* rate = std::getenv ("FORMULS_TEST_SAMPLERATE"))
                for (int i = 0; i < (int) std::size (kSampleRateChoices); ++i)
                    if (kSampleRateChoices[i] == atoi (rate))
                        [self->sampleRatePopup selectItemAtIndex:i];

            if (! self->engine.isRunning())
                [self startEverything];

            fprintf (stderr, "GUI addresses:\n%s\nstatus: %s\n",
                     self->addressPanel.string.UTF8String,
                     self->statusLabel.stringValue.UTF8String);
        });
    }

    if (const char* path = std::getenv ("FORMULS_SNAPSHOT_PATH"))
    {
        NSString* file = [NSString stringWithUTF8String:path];
        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 4 * NSEC_PER_SEC),
                        dispatch_get_main_queue(), ^{
            NSView* view = self->window.contentView;
            NSBitmapImageRep* rep =
                [view bitmapImageRepForCachingDisplayInRect:view.bounds];
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
            NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                            properties:@{}];
            [png writeToFile:file atomically:YES];
            fprintf (stderr, "wrote snapshot to %s\n", file.UTF8String);
        });
    }
}

@end

//==============================================================================
int main()
{
    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;

        FormulsController* controller = [FormulsController new];
        app.delegate = controller;
        [app run];
    }
    return 0;
}

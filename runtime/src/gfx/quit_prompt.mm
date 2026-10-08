// Quit prompt (issue #65): closing the TV window quits the app, as on Windows and Linux; while a save
// file is being played, closing it or Quit (Cmd+Q, the Dock's Quit, logout) first asks
// "Quit Wind Waker HD?" in a sheet on the TV window:
//   Quit                 quits (the default button, Return)
//   Cancel               keeps playing (Esc)
//   Save State and Quit  writes save state slot 1 (as Save States > Save to slot 1, Shift+F1), then quits
// Before that (boot, title screen, file select) and in test runs nothing asks (quit_prompt.h decides).
// Closing the GamePad window still only hides it.
//
// Test aids: WWHD_TEST_QUIT_ANSWER=quit|cancel|save shows the prompt also in scripted runs and answers
// it after 1.5 s; WWHD_TEST_CLOSE_TV_AT=frame closes the TV window (its close button) at that frame.
#import <AppKit/AppKit.h>

#include <cstdlib>
#include <string>

#include "../quit_prompt.h"
#include "../savestate.h"
#include "renderer.h"
#include "runtime.h"

namespace {
const char* env(const char* name) { return getenv(name); }
NSWindow* g_tv = nil;
bool g_confirmed = false;  // answered: the next terminate: goes through
bool g_asking = false;     // the prompt is open
int g_saving = 0;          // a save state for the quit is being written (its number; 0: none)

quitprompt::Request request() {
    quitprompt::Request r;
    r.confirmed = g_confirmed;
    r.busy = g_asking || g_saving;
    r.game_window = g_tv && render::frame_count() > 0;
    r.gameplay = r.game_window && ss::in_gameplay();
    r.suppressed = quitprompt::suppressed(env);
    r.test_answer = quitprompt::test_answer(env) != quitprompt::Answer::None;
    return r;
}

void quit_now() {
    g_confirmed = true;
    [NSApp terminate:nil];
}

void ask();

// the save state did not get written: quit anyway, or keep playing
void save_failed(const std::string& why) {
    LOG("[quit] save state not written (%s)", why.c_str());
    NSAlert* a = [NSAlert new];
    a.messageText = @"The save state could not be written.";
    a.informativeText = [NSString stringWithFormat:@"%s\n\nQuit anyway? Progress since your last in-game save is lost.", why.c_str()];
    a.alertStyle = NSAlertStyleWarning;
    [a addButtonWithTitle:@"Quit"];
    [a addButtonWithTitle:@"Cancel"];
    g_asking = true;
    [a beginSheetModalForWindow:g_tv completionHandler:^(NSModalResponse r) {
        g_asking = false;
        if (r == NSAlertFirstButtonReturn) quit_now();
    }];
}

void save_and_quit() {
    static int generation = 0;
    int gen = ++generation;
    g_saving = gen;
    LOG("[quit] saving state slot %d before quitting", quitprompt::kSaveSlot);
    ss::request_save(quitprompt::kSaveSlot, [gen](bool ok, const std::string& why) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (g_saving != gen) return;  // timed out before
            g_saving = 0;
            if (ok) {
                LOG("[quit] save state slot %d written; quitting", quitprompt::kSaveSlot);
                quit_now();
            } else {
                save_failed(why);
            }
        });
    });
    // the game normally saves within a frame or two and writes the file in a few seconds
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        if (g_saving != gen) return;
        g_saving = 0;
        save_failed("The game did not save within a minute.");
    });
}

void ask() {
    LOG("[quit] asking before quitting");
    NSAlert* a = [NSAlert new];
    a.messageText = @"Quit Wind Waker HD?";
    a.informativeText = [NSString stringWithFormat:@"Progress since your last in-game save is lost.\n\n"
                                                   @"Save State and Quit keeps it in save state slot %d (Save States menu), "
                                                   @"replacing what is there.",
                                                   quitprompt::kSaveSlot];
    a.alertStyle = NSAlertStyleWarning;
    [a addButtonWithTitle:@"Quit"];    // default (Return)
    [a addButtonWithTitle:@"Cancel"];  // Esc
    [a addButtonWithTitle:@"Save State and Quit"];
    g_asking = true;
    [a beginSheetModalForWindow:g_tv completionHandler:^(NSModalResponse r) {
        g_asking = false;
        if (r == NSAlertFirstButtonReturn) {
            LOG("[quit] answer: Quit");
            quit_now();
        } else if (r == NSAlertThirdButtonReturn) {
            LOG("[quit] answer: Save State and Quit");
            save_and_quit();
        } else {
            LOG("[quit] answer: Cancel; the game keeps running");
        }
    }];
    // test runs: answer by themselves (the sheet is shown for real, then ended like a click)
    quitprompt::Answer t = quitprompt::test_answer(env);
    if (t == quitprompt::Answer::None) return;
    LOG("[quit] test: TV window %ld, prompt window %ld", (long)g_tv.windowNumber, (long)a.window.windowNumber);
    NSModalResponse code = t == quitprompt::Answer::Quit ? NSAlertFirstButtonReturn
                           : t == quitprompt::Answer::SaveAndQuit ? NSAlertThirdButtonReturn
                                                                  : NSAlertSecondButtonReturn;
    NSWindow* sheet = a.window;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (g_tv.attachedSheet == sheet) [g_tv endSheet:sheet returnCode:code];
    });
}

// a quit request: true if the app may terminate now
bool quit_request(const char* from) {
    quitprompt::Request r = request();
    switch (quitprompt::decide(r)) {
    case quitprompt::Action::Quit:
        LOG("[quit] %s: quitting (%s)", from,
            r.confirmed ? "answered" : !r.game_window ? "game not started" : !r.gameplay ? "no game in progress" : "test run");
        return true;
    case quitprompt::Action::Ignore: return false;
    case quitprompt::Action::Ask:
        LOG("[quit] %s while a game is in progress", from);
        ask();
        return false;
    }
    return true;
}
}  // namespace

// the TV window: its close button and Close Window (Cmd+W) ask to quit; NSWindow's own performClose:
// would play the alert sound when windowShouldClose: declines
@interface WWTvWindow : NSWindow
@end
@implementation WWTvWindow
- (void)performClose:(id)sender {
    if (quit_request("TV window closed")) [NSApp terminate:nil];
}
@end

@interface WWQuitDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end
@implementation WWQuitDelegate
// Quit (Cmd+Q), the Dock's Quit, logout
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    return quit_request("Quit") ? NSTerminateNow : NSTerminateCancel;
}
// any other way of closing the TV window: the same; the window itself stays until the app quits
- (BOOL)windowShouldClose:(NSWindow*)w {
    if (quit_request("TV window closed")) [NSApp terminate:nil];
    return NO;
}
@end

namespace gfx {
Class tv_window_class() { return [WWTvWindow class]; }

// with the windows (display.mm): the TV window's delegate and the application's
void install_quit_prompt(NSWindow* tv) {
    static WWQuitDelegate* d = [WWQuitDelegate new];  // both delegate properties are weak
    g_tv = tv;
    tv.delegate = d;
    NSApp.delegate = d;
    if (const char* e = getenv("WWHD_TEST_CLOSE_TV_AT")) {
        uint64_t at = strtoull(e, nullptr, 10);
        [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer* t) {
            if (render::frame_count() < at) return;
            [t invalidate];
            LOG("[quit] test: closing the TV window at frame %llu (stage %s)", (unsigned long long)render::frame_count(),
                ss::in_gameplay() ? "in game" : "not in game");
            [tv performClose:nil];
        }];
    }
}
}  // namespace gfx

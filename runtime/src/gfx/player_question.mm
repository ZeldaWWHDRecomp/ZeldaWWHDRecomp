// A question for the player in a native macOS alert (player_question.h). The save folder asks at
// start-up, before the TV window exists and before AppKit's loop runs, so the alert is app-modal
// (runModal), not a sheet.
#import <AppKit/AppKit.h>

#include "player_question.h"

#include <cstdlib>
#include <cstring>

#include "runtime.h"

namespace gfx {
namespace {
bool flag(const char* name) {
    const char* v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

// WWHD_TEST_ASK_ANSWERS=a,b,...: the answer for the n-th question (-1: none left, the player answers)
int test_answer(int n) {
    const char* list = getenv("WWHD_TEST_ASK_ANSWERS");
    if (!list || !*list) return -1;
    for (const char* p = list;; n--) {
        char* end = nullptr;
        const long a = strtol(p, &end, 10);
        if (end == p) return -1;
        if (n == 0) return int(a);
        if (*end != ',') return -1;
        p = end + 1;
    }
}

int ask_on_main(const std::string& title, const std::string& text, const std::vector<std::string>& buttons) {
    static int asked = 0;
    const int answer = test_answer(asked++);
    if (flag("WWHD_HIDDEN_WINDOWS") && answer < 0) return -1;  // scripted runs: the safe answer
    @autoreleasepool {
        [NSApplication sharedApplication];
        // before the TV window: an app started from a terminal has no activation policy yet
        const bool test = getenv("WWHD_NO_HOST_INPUT") != nullptr;
        if (NSApp.activationPolicy == NSApplicationActivationPolicyProhibited)
            [NSApp setActivationPolicy:test ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular];
        if (!test) [NSApp activateIgnoringOtherApps:YES];

        NSAlert* alert = [NSAlert new];
        alert.alertStyle = NSAlertStyleWarning;
        alert.messageText = [NSString stringWithUTF8String:title.c_str()] ?: @"Wind Waker HD";
        alert.informativeText = [NSString stringWithUTF8String:text.c_str()] ?: @"";
        for (const auto& b : buttons) [alert addButtonWithTitle:[NSString stringWithUTF8String:b.c_str()] ?: @"?"];
        LOG("[question] %s (%zu buttons)", title.c_str(), buttons.size());
        NSTimer* shown = [NSTimer timerWithTimeInterval:0.1 repeats:NO block:^(NSTimer*) {
            LOG("[question] shown (window %ld)", (long)alert.window.windowNumber);
        }];
        [[NSRunLoop currentRunLoop] addTimer:shown forMode:NSModalPanelRunLoopMode];

        if (answer >= 0 && answer < int(alert.buttons.count)) {
            // test runs: the alert is shown for real, then its button is clicked
            const char* d = getenv("WWHD_TEST_ASK_DELAY_MS");
            const double delay = (d && *d ? atof(d) : 2000.0) / 1000.0;
            NSButton* button = alert.buttons[NSUInteger(answer)];
            NSTimer* click = [NSTimer timerWithTimeInterval:delay repeats:NO block:^(NSTimer*) { [button performClick:nil]; }];
            [[NSRunLoop currentRunLoop] addTimer:click forMode:NSModalPanelRunLoopMode];
        }
        const NSModalResponse r = [alert runModal];
        const long pick = long(r) - long(NSAlertFirstButtonReturn);
        const int out = pick >= 0 && pick < long(buttons.size()) ? int(pick) : -1;
        LOG("[question] answer %d (%s)", out, out >= 0 ? buttons[size_t(out)].c_str() : "none");
        return out;
    }
}
}  // namespace

int ask_player_alert(const std::string& title, const std::string& text, const std::vector<std::string>& buttons) {
    if (buttons.empty()) return -1;
    if ([NSThread isMainThread]) return ask_on_main(title, text, buttons);
    // another thread: the main thread shows it, but only while it runs AppKit's loop; a main
    // thread busy elsewhere (or waiting for this thread) would never answer
    if (!NSApp || !NSApp.running) return -1;
    __block int out = -1;
    dispatch_sync(dispatch_get_main_queue(), ^{ out = ask_on_main(title, text, buttons); });
    return out;
}
}  // namespace gfx

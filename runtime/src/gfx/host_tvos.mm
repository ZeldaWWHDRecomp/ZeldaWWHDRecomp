// The Apple TV host (tvOS, WWHD_TVOS): UIKit instead of the AppKit host. One full-screen view whose layer is
// the TV picture's CAMetalLayer (display.mm takes it through host_tvos_layer); the game's own start-up
// (main.cpp's wwhd_main) runs on a thread of its own, as UIKit owns the main thread and its run loop.
// Controllers come through GameController.framework (input.mm), as on the Mac. The Mac's windows, menus,
// dialogs, controls window, text input and mouse have no equivalent here: their entry points below do
// nothing or decline.
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

#include <mach/mach.h>
#include <os/proc.h>
#include <pthread.h>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "../input.h"
#include "../overlay/hostui.h"
#include "display_modes.h"
#include "renderer.h"
#include "../interp.h"

int wwhd_main(int argc, char** argv);  // main.cpp
void log_msg(const char* fmt, ...);

@interface WWMetalView : UIView
@end
@implementation WWMetalView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (void)layoutSubviews {
    [super layoutSubviews];
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    const CGFloat scale = self.window.screen.nativeScale ?: UIScreen.mainScreen.nativeScale;
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(MAX(1.0, self.bounds.size.width * scale), MAX(1.0, self.bounds.size.height * scale));
}
@end

@interface WWViewController : UIViewController
@end
@implementation WWViewController
- (void)loadView {
    self.view = [WWMetalView new];
    self.view.backgroundColor = UIColor.blackColor;
}
// the controller's Menu button goes to the game (its Home / Settings), not back to the Home screen
- (BOOL)controllerUserInteractionEnabled { return NO; }
@end

static WWMetalView* g_view = nil;
static int g_argc = 0;
static char** g_argv = nullptr;

// Where things are: the game files inside the app (game/, packaged with it), everything the game writes in
// the app's Caches folder (an Apple TV app may write nowhere else that lasts): settings and caches (the
// user folder), saves, and the log (relative to the working directory).
// The live shader cache packaged with the app (shaders.bin: every shader and pipeline the game is known to use,
// recorded where the game was prepared) goes where the renderer keeps it (the user folder's live shader caches,
// shader_prebuild.h bind_live_caches), when the
// copy there is missing or smaller: the first start, or after tvOS emptied the Caches folder. Its records are
// the game's state, valid on any GPU; with the packaged shader library none of them compiles here.
static void install_shader_cache(NSString* caches) {
    NSString* packaged = [[NSBundle mainBundle] pathForResource:@"shaders" ofType:@"bin"];
    if (!packaged) return;
    NSString* dir = [caches stringByAppendingPathComponent:@"user/shader-prebuild/live"];
    NSString* live = [dir stringByAppendingPathComponent:@"shaders.bin"];
    NSFileManager* fm = [NSFileManager defaultManager];
    const unsigned long long have = [[fm attributesOfItemAtPath:live error:nil] fileSize];
    const unsigned long long ship = [[fm attributesOfItemAtPath:packaged error:nil] fileSize];
    if (have >= ship) return;
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    [fm removeItemAtPath:live error:nil];
    NSError* err = nil;
    if ([fm copyItemAtPath:packaged toPath:live error:&err]) log_msg("[tvos] shader cache installed from the app (%llu MB)", ship >> 20);
    else log_msg("[tvos] shader cache not installed: %s", err.localizedDescription.UTF8String);
}

// Heat guard: the Apple TV 4K (A15) has no fan, and under the game's load it reaches tvOS's "serious" thermal
// state, where the system slows the chip until the game runs at a third of its speed (30 -> 10 fps, measured).
// From "serious" on, the game lowers its own load instead: 60 fps pauses (30), the occlusion depth goes to
// half size and the internal resolution drops to 0.75x. After a minute at "fair" or "nominal" the player's settings come
// back as they were. Nothing it changes is saved. (Android pauses only 60 fps: platform/heat_guard.h.)
struct HeatGuard {
    bool active = false;
    double cool_since = -1;
    int interp_mode = 0;
    bool ao_hires = false;
    float res = 1.0f;

    // main thread, every 10 s
    void tick(NSInteger thermal, double now) {
        const bool hot = thermal >= NSProcessInfoThermalStateSerious;
        if (hot) cool_since = -1;
        else if (cool_since < 0) cool_since = now;
        if (hot && !active) {
            active = true;
            interp_mode = interp::mode(), ao_hires = render::ao_hires(), res = render::res_scale();
            if (interp_mode) interp::set_mode(0);
            render::set_ao_hires(false);
            if (res > 0.75f) render::set_res_scale(0.75f);
            log_msg("[tvos] heat guard: the Apple TV is hot: 30 fps, half-size occlusion depth, 0.75x resolution until it cools");
        } else if (!hot && active && now - cool_since >= 60) {
            active = false;
            interp::set_mode(interp_mode);
            render::set_ao_hires(ao_hires);
            render::set_res_scale(res);
            log_msg("[tvos] heat guard: cooled down: the settings are back");
        }
    }
};

// Memory: what tvOS counts against the app (its physical footprint) and what it still allows, and the thermal
// state, every 10 s
// (an Apple TV app has a fixed limit, about 2 GB on an A15, and is ended when it reaches it)
static void start_memory_log() {
    static dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), 10 * NSEC_PER_SEC, NSEC_PER_SEC);
    dispatch_source_set_event_handler(timer, ^{
        task_vm_info_data_t info{};
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return;
        static uint64_t peak = 0;
        peak = std::max<uint64_t>(peak, info.phys_footprint);
        // and how hot the Apple TV is: tvOS slows the chip down from "serious" (the A15 Apple TV has no fan)
        static const char* const kThermal[] = {"nominal", "fair", "serious", "critical"};
        const NSInteger t = [NSProcessInfo processInfo].thermalState;
        log_msg("[tvos] memory: footprint %llu MB (peak %llu MB), %zu MB more allowed; thermal state %s",
                (unsigned long long)(info.phys_footprint >> 20), (unsigned long long)(peak >> 20), os_proc_available_memory() >> 20,
                t >= 0 && t < 4 ? kThermal[t] : "?");
        // (the renderer's settings change on the main thread, as the settings overlay changes them)
        static HeatGuard guard;
        const double now = CACurrentMediaTime();
        if (render::g_backend) dispatch_async(dispatch_get_main_queue(), ^{ guard.tick(t, now); });
    });
    dispatch_resume(timer);
}

static void* run_game(void*) {
    NSString* caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    install_shader_cache(caches);
    start_memory_log();
    NSString* user = [caches stringByAppendingPathComponent:@"user"];
    NSString* save = [caches stringByAppendingPathComponent:@"save"];
    for (NSString* d in @[ user, save ])
        [[NSFileManager defaultManager] createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
    chdir(caches.fileSystemRepresentation);
    setenv("WWHD_USER_DIR", user.fileSystemRepresentation, 0);
    const std::string game = std::string([NSBundle mainBundle].bundlePath.fileSystemRepresentation) + "/game";
    std::vector<std::string> args = {g_argc ? g_argv[0] : "WindWakerHD", "--game", game, "--save", save.fileSystemRepresentation};
    for (int i = 1; i < g_argc; i++) args.push_back(g_argv[i]);  // (devicectl launch arguments come after)
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    wwhd_main(int(args.size()), argv.data());
    return nullptr;
}

@interface WWSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow* window;
@end
@implementation WWSceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    UIWindowScene* ws = (UIWindowScene*)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:ws];
    WWViewController* vc = [WWViewController new];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    g_view = (WWMetalView*)vc.view;
    [g_view layoutIfNeeded];
    static bool started = false;
    if (started) return;
    started = true;
    // the game's start-up and its guest thread want deep stacks, as the Mac's main thread has (8 MB)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    pthread_t t;
    pthread_create(&t, &attr, run_game, nullptr);
    pthread_attr_destroy(&attr);
}
@end

@interface WWAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation WWAppDelegate
- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    UISceneConfiguration* c = [[UISceneConfiguration alloc] initWithName:nil sessionRole:session.role];
    c.delegateClass = [WWSceneDelegate class];
    return c;
}
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
    application.idleTimerDisabled = YES;  // a game: no screen saver while it is played with a controller
    return YES;
}
@end

namespace gfx {
// display.mm: the TV picture's layer (main thread; the view exists once the scene connected)
CAMetalLayer* host_tvos_layer() { return g_view ? (CAMetalLayer*)g_view.layer : nil; }
bool host_setting(const char* key, std::string& value);
void set_host_setting(const char* key, const std::string& value);

// The graphics options the Mac's menus save (menu.mm, not built here), in the same settings file display.mm keeps
// and under the same names. The occlusion depth starts at half size: the A15's GPU is the limit, as a phone's.
static bool saved_number(const char* key, double& v) {
    std::string s;
    if (!host_setting(key, s) || s.empty()) return false;
    char* end = nullptr;
    v = strtod(s.c_str(), &end);
    return end && end != s.c_str();
}
void host_tvos_load_graphics() {
    double v;
    if (!getenv("WWHD_RES_SCALE") && saved_number("resScale", v) && v > 0) render::set_res_scale(float(v));
    if (!getenv("WWHD_AO_HIRES")) render::set_ao_hires(saved_number("aoHires", v) && v != 0);  // (half size unless chosen)
    if (saved_number("fxaa", v)) render::set_fxaa(v != 0);
    if (saved_number("aniso", v)) render::set_aniso(v != 0);
    if (saved_number("cloudShadows", v)) render::set_cloud_shadows(v != 0);
    if (saved_number("interpFps", v)) interp::set_fps(int(v));
    if (saved_number("fps60", v)) interp::set_mode(int(v));
}
static void save_graphics() {
    set_host_setting("resScale", std::to_string(render::res_scale()));
    set_host_setting("aoHires", render::ao_hires() ? "1" : "0");
    set_host_setting("fxaa", render::fxaa() ? "1" : "0");
    set_host_setting("aniso", render::aniso() ? "1" : "0");
    set_host_setting("cloudShadows", render::cloud_shadows() ? "1" : "0");
    set_host_setting("interpFps", std::to_string(interp::fps()));
    set_host_setting("fps60", std::to_string(interp::mode()));
}

// display.mm
int display_filter();
void display_set_filter(int f);
int display_drc_mode();
void set_drc_mode(int m);
void display_set_pip(int corner, float size, float opacity);
bool drc_window_available();
bool drc_window_shown();
void show_drc_window(bool on);

// player_question.h: no native alert here; main.cpp's ask_player takes its default
int ask_player_alert(const std::string&, const std::string&, const std::vector<std::string>&) { return -1; }
}  // namespace gfx

// mods/mouse.mm: no mouse on an Apple TV
namespace mods {
bool mouse_captured() { return false; }
void mouse_release() {}
void update_gyro_mouse() {}
}  // namespace mods

// main.cpp's crash handler asks the Vulkan renderer's compile journal (not built here)
extern "C" void wwhd_compile_journal_on_crash() {}

// ---------------------------------------------------------------- the settings overlay's host (hostui.h)
// As the AppKit host's (overlay_appkit.mm) for what display.mm keeps; no file panels, folders or clipboard.
namespace hostui {
void post(std::function<void()> fn) {
    dispatch_async(dispatch_get_main_queue(), ^{ fn(); });
}
void choose_game_source(bool, std::function<void(std::string)>) {}
void choose_mod_source(bool, std::function<void(std::string)>) {}
void choose_save_folder(std::function<void(std::string)>) {}
void choose_file(const char*, const char*, bool, std::function<void(std::string)>) {}
bool get(const char* key, std::string& value) { return gfx::host_setting(key, value); }
void set(const char* key, const std::string& value) { gfx::set_host_setting(key, value); }
float res_scale() { return render::res_scale(); }
void set_res_scale(float s) {
    render::set_res_scale(s);
    gfx::set_host_setting("resScale", std::to_string(s));
}
void graphics_changed() { gfx::save_graphics(); }  // (an option of Settings > Graphics changed)
int scale_filter() { return gfx::display_filter(); }
void set_scale_filter(int f) { gfx::display_set_filter(f); }
bool scale_filter_available() { return true; }
bool fullscreen() { return true; }
void set_fullscreen(bool) {}
int drc_modes() { return gfx::kDrcModeCount; }
bool drc_mode_offered(int m) { return m >= 0 && m < gfx::kDrcModeCount && m != gfx::kDrcWindow; }  // (no second window)
int drc_mode() { return gfx::display_drc_mode(); }
void set_drc_mode(int m) { gfx::set_drc_mode(m); }
void save_pip_position() { gfx::display_set_pip(gfx::g_pip_anchor, gfx::g_pip_size, gfx::g_pip_opacity); }
int pip_anchor() { return gfx::g_pip_anchor; }
void set_pip_anchor(int c) { gfx::display_set_pip(c, gfx::g_pip_size, gfx::g_pip_opacity); }
float pip_size() { return gfx::g_pip_size; }
void set_pip_size(float s) { gfx::display_set_pip(gfx::g_pip_anchor, s, gfx::g_pip_opacity); }
float pip_opacity() { return gfx::g_pip_opacity; }
void set_pip_opacity(float o) { gfx::display_set_pip(gfx::g_pip_anchor, gfx::g_pip_size, o); }
bool drc_available() { return gfx::drc_window_available(); }
bool drc_shown() { return gfx::drc_window_shown(); }
void show_drc(bool on) { gfx::show_drc_window(on); }
void set_pro_controller(bool on) {
    input::set_pro_controller(on);
    gfx::show_drc_window(!on);
}
const char* name() { return "tvOS"; }
void set_clipboard(const std::string&) {}
bool can_open_folder() { return false; }
void open_folder(const std::string&) {}
// Settings > Quit Game: an Apple TV app does not quit itself; back to the Home screen keeps it as it is
void quit() {}
}  // namespace hostui

int main(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([WWAppDelegate class]));
    }
}

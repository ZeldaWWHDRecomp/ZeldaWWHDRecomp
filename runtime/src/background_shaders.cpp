// Shader compile levels (background_shaders.h).
#include "background_shaders.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

void log_msg(const char* fmt, ...);
#define LOG(...) log_msg(__VA_ARGS__)

namespace bgshaders {

namespace {
std::atomic<uint8_t> g_requested{kLevelOff};  // the setting
std::mutex g_counterMutex;                    // the counters are read from the UI thread
Counters g_counters;
uint64_t g_budgetFrame = ~uint64_t{0}, g_budgetSpent = 0;  // render thread
uint64_t g_skipFrame = ~uint64_t{0};
}

const char* level_name(Level l) { return l == kLevelFast ? "fast" : "off"; }
const char* level_value(Level l) { return l == kLevelFast ? "fast" : "0"; }
Level parse_level(const char* v) { return v && !strcmp(v, "fast") ? kLevelFast : kLevelOff; }
const char* env_override() {
    static const char* const e = getenv("WWHD_BG_SHADERS");
    return e && *e ? e : nullptr;
}
static std::string source() {
    return env_override() ? std::string("WWHD_BG_SHADERS=") + env_override() : std::string("setting");
}
Level requested() { return Level(g_requested.load(std::memory_order_relaxed)); }
Level level() { return env_override() ? parse_level(env_override()) : requested(); }
void set_level(Level l) {
    if (l >= kLevels) l = kLevelOff;
    if (Level(g_requested.exchange(l)) == l) return;
    if (env_override())
        LOG("[shaders] shader compile: setting %s; %s decides for this start", level_name(l), source().c_str());
    else
        LOG("[shaders] shader compile %s (setting)", level_name(l));
}
void start(Level saved) {
    g_requested = saved < kLevels ? saved : kLevelOff;
    if (env_override() && !strcmp(env_override(), "safe"))
        LOG("[shaders] WWHD_BG_SHADERS=safe: the Safe level was removed; using 0 (Off)");
    LOG("[shaders] shader compile %s (%s); wait budget %.1f ms per frame", level_name(level()), source().c_str(),
        wait_budget_ms());
}
double wait_budget_ms() {
    static const double ms = [] {
        const char* e = getenv("WWHD_BG_SHADER_WAIT_MS");
        const double v = e && *e ? atof(e) : kDefaultWaitMs;
        return v < 0 ? 0.0 : v > 1000 ? 1000.0 : v;
    }();
    return ms;
}

const char* verdict_name(Verdict v) {
    static const char* const names[kVerdicts] = {"left out", "option off", "new or one-off target", "read back",
                                                 "copied"};
    return v < kVerdicts ? names[v] : "?";
}

Verdict classify(Level level, const TargetFacts* colors, int colorCount, const TargetFacts* depth) {
    if (level != kLevelFast) return kOff;
    // only what could change the game or leave a lasting wrong picture waits
    auto check = [](const TargetFacts& t) {
        if (t.readBack || t.peeked) return kReadBack;
        if (t.copiedOccasionally) return kCopied;
        if (!t.redrawn && !t.regular) return kFirstFrame;
        return kSkippable;
    };
    for (int i = 0; i < colorCount; i++)
        if (Verdict v = check(colors[i]); v != kSkippable) return v;
    if (depth)
        if (Verdict v = check(*depth); v != kSkippable) return v;
    return kSkippable;
}

void Tracker::drawn(uint32_t addr, uint64_t frame) {
    Use& u = use_[addr];
    const bool continues = u.history.valid && (u.history.last_frame == frame || u.history.last_frame + 1 == frame);
    if (!continues) u.drawRunStart = frame;
    u.history.drawn(frame);
    if (u.drawRunStart != kNever && frame - u.drawRunStart + 1 >= kRegularFrames) u.regular = true;
}
void Tracker::forget() {
    for (auto it = use_.begin(); it != use_.end();) {
        if (!it->second.regular) { it = use_.erase(it); continue; }
        it->second = Use{};
        it->second.regular = true;
        ++it;
    }
}
bool Tracker::copied(uint32_t addr, uint64_t frame) {
    Use& u = use_[addr];
    if (u.copied != frame) {
        if (u.copied == kNever || u.copied + 1 != frame) u.copyRunStart = frame;
        u.copied = frame;
    }
    const bool afterSkip = u.leftOut == frame;
    if (afterSkip) {
        std::lock_guard lock(g_counterMutex);
        g_counters.copiesAfterSkip++;
    }
    return afterSkip;
}
TargetFacts Tracker::facts(uint32_t addr, uint64_t frame) const {
    TargetFacts f;
    auto it = use_.find(addr);
    if (it == use_.end()) return f;
    const Use& u = it->second;
    auto within = [frame](uint64_t at, uint64_t frames) { return at != kNever && at <= frame && frame - at <= frames; };
    f.redrawn = !u.history.must_wait(frame);
    f.readBack = within(u.readBack, kQuietFrames);
    f.peeked = within(u.peeked, kQuietFrames);
    f.regular = u.regular;
    // copies in consecutive frames up to the previous one refresh their result every frame
    const bool everyFrame = within(u.copied, 1) && u.copyRunStart != kNever && u.copyRunStart < u.copied;
    f.copiedOccasionally = within(u.copied, kQuietFrames) && !everyFrame;
    return f;
}
Tracker& tracker() {
    static Tracker t;
    return t;
}

uint64_t budget_left_ns(uint64_t frame) {
    if (g_budgetFrame != frame) { g_budgetFrame = frame; g_budgetSpent = 0; }
    const uint64_t budget = uint64_t(wait_budget_ms() * 1e6);
    return g_budgetSpent >= budget ? 0 : budget - g_budgetSpent;
}
void spent(uint64_t frame, uint64_t ns, bool ranOut) {
    if (g_budgetFrame != frame) { g_budgetFrame = frame; g_budgetSpent = 0; }
    if (ranOut || ns >= kFastWaitNs) g_budgetSpent += ns;  // fast results (a warm cache) are free
    std::lock_guard lock(g_counterMutex);
    g_counters.budgetWaits++;
    g_counters.budgetWaitNs += ns;
    if (ranOut) g_counters.budgetRanOut++;
}
void skipped(uint64_t frame) {
    std::lock_guard lock(g_counterMutex);
    g_counters.skipped++;
    if (g_skipFrame != frame) { g_skipFrame = frame; g_counters.skipFrames++; }
}
bool skipped_in_frame(uint64_t frame) { return g_skipFrame == frame; }
void peek_withheld() {
    std::lock_guard lock(g_counterMutex);
    g_counters.peeksWithheld++;
}
void exact(Verdict v) {
    if (v >= kVerdicts) return;
    std::lock_guard lock(g_counterMutex);
    g_counters.exact[v]++;
}
Counters counters() {
    std::lock_guard lock(g_counterMutex);
    return g_counters;
}
void reset_counters() {
    std::lock_guard lock(g_counterMutex);
    g_counters = {};
    g_budgetFrame = g_skipFrame = ~uint64_t{0};
    g_budgetSpent = 0;
}
std::string report_line() {
    const Counters c = counters();
    char buf[512];
    int n = snprintf(buf, sizeof buf,
        "Shader compile: %s (%s). Draws left out while compiling: %llu in %llu frames; "
        "budget waits %llu (%.0f ms, %llu ran out); depth peeks kept: %llu; copies after a left-out draw: %llu. "
        "Exact waits:",
        level_name(level()), source().c_str(), (unsigned long long)c.skipped, (unsigned long long)c.skipFrames,
        (unsigned long long)c.budgetWaits, c.budgetWaitNs / 1e6, (unsigned long long)c.budgetRanOut,
        (unsigned long long)c.peeksWithheld, (unsigned long long)c.copiesAfterSkip);
    std::string line(buf, n > 0 && n < int(sizeof buf) ? size_t(n) : strlen(buf));
    bool any = false;
    for (int v = kOff; v < kVerdicts; v++)
        if (c.exact[v]) {
            line += std::string(any ? ", " : " ") + verdict_name(Verdict(v)) + " " + std::to_string(c.exact[v]);
            any = true;
        }
    if (!any) line += " none";
    return line + "\n";
}

}  // namespace bgshaders

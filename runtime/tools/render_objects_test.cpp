// Renderer object counts and memory mappings (runtime/src/render_objects.h): line counting of a
// /proc/self/maps text, the one-line format with and without a readable limit, and the live line.
#include <cstdio>
#include <cstring>
#include <string>

#include "render_objects.h"

static int failures = 0;
static void expect(const std::string& got, const std::string& want, const char* what) {
    if (got != want) {
        fprintf(stderr, "FAIL %s:\n  got  \"%s\"\n  want \"%s\"\n", what, got.c_str(), want.c_str());
        failures++;
    }
}

int main() {
    const char maps[] = "00400000-00452000 r-xp 00000000 08:02 173521 /usr/bin/dbus-daemon\n"
                        "00651000-00652000 r--p 00051000 08:02 173521 /usr/bin/dbus-daemon\n"
                        "7fff1a5ff000-7fff1a600000 r-xp 00000000 00:00 0 [vdso]\n";
    expect(std::to_string(render_objects::count_lines(maps, strlen(maps))), "3", "maps lines");
    expect(std::to_string(render_objects::count_lines(maps, 0)), "0", "empty");
    expect(render_objects::format(572, 337, -1, -1), "renderer objects: 572 shaders, 337 pipelines", "no mappings");
    expect(render_objects::format(572, 337, 23456, 65530),
           "renderer objects: 572 shaders, 337 pipelines; memory mappings 23456 (limit 65530)", "mappings and limit");
    expect(render_objects::format(1, 2, 300, -1),
           "renderer objects: 1 shaders, 2 pipelines; memory mappings 300 (limit not readable, usually 65530)",
           "unreadable limit");
    render_objects::set_shaders(12);
    render_objects::set_pipelines(34);
    const std::string live = render_objects::line();
    if (live.rfind("renderer objects: 12 shaders, 34 pipelines", 0) != 0) {
        fprintf(stderr, "FAIL live line: %s\n", live.c_str());
        failures++;
    }
#if defined(__linux__) || defined(__ANDROID__)
    if (live.find("; memory mappings ") == std::string::npos) {
        fprintf(stderr, "FAIL live line has no mapping count: %s\n", live.c_str());
        failures++;
    }
#endif
    printf("%s\n", live.c_str());
    if (failures) fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}

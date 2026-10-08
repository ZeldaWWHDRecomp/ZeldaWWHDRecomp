#include "full_state_header.h"
#include <cassert>
#include <cstdio>
int main() {
    for (bool legacy : {false, true}) for (unsigned mode : {0u, 1u, 2u, 3u}) {
        ss::FullStateHeader h{};
        memcpy(h.magic, "WWHDSTAT", 8);
        h.version = 1;
        h.header_size = legacy ? ss::kLegacyHeaderSize : sizeof h;
        h.controller = mode;
        FILE* file = tmpfile();
        assert(file);
        assert(fwrite(&h, h.header_size, 1, file) == 1);
        fputc(0x42, file); // payload must start at exactly the old/new header end
        rewind(file);
        ss::FullStateHeader loaded{};
        std::string why;
        assert(ss::read_full_state_header(file, loaded, why) == (legacy || mode <= 2));
        if (legacy || mode <= 2) {
            assert(loaded.controller == (legacy ? 0 : mode));
            assert(fgetc(file) == 0x42);
            bool pro = true; // save in Pro, then switch to GamePad, load
            pro = false;
            pro = ss::restored_pro_controller(loaded.controller, pro);
            assert(pro == (!legacy && mode == 2));
            if (legacy) assert(ss::restored_pro_controller(loaded.controller, true));
        }
        fclose(file);
    }
    ss::FullStateHeader h{};
    memcpy(h.magic, "WWHDSTAT", 8);
    h.header_size = sizeof h;
    FILE* file = tmpfile();
    fwrite(&h, ss::kLegacyHeaderSize, 1, file);
    rewind(file);
    std::string why;
    assert(!ss::read_full_state_header(file, h, why));
    fclose(file);
    puts("legacy/new full-state headers, controller round trip and truncation pass");
}

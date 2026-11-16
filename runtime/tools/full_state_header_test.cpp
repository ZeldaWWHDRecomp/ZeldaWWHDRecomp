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
    // the build kind (bits 8-9 of reserved): 0 normal, 1 decomp plain, 2 decomp fast are read; 3 and unknown bits are not
    for (uint32_t kind : {0u, 1u, 2u, 3u}) for (uint32_t profile : {0u, ss::kRequiresSaveProfile}) {
        ss::FullStateHeader k{};
        memcpy(k.magic, "WWHDSTAT", 8);
        k.header_size = sizeof k;
        k.reserved = profile | (kind << ss::kBuildKindShift);
        FILE* f = tmpfile();
        assert(f && fwrite(&k, sizeof k, 1, f) == 1);
        rewind(f);
        ss::FullStateHeader loaded{};
        assert(ss::read_full_state_header(f, loaded, why) == (kind <= 2));
        if (kind <= 2) assert(ss::build_kind(loaded.reserved) == kind && (loaded.reserved & ss::kRequiresSaveProfile) == profile);
        fclose(f);
    }
    {
        ss::FullStateHeader k{};
        memcpy(k.magic, "WWHDSTAT", 8);
        k.header_size = sizeof k;
        k.reserved = 1u << 12;  // an unknown bit
        FILE* f = tmpfile();
        assert(f && fwrite(&k, sizeof k, 1, f) == 1);
        rewind(f);
        assert(!ss::read_full_state_header(f, k, why));
        fclose(f);
    }
    assert(std::string(ss::build_kind_name(0)) == "normal game");
    puts("legacy/new full-state headers, controller metadata round trip, build kinds and truncation pass");
}

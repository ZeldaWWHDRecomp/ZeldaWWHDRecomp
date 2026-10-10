// Reuse the existing test-only archive writer; no game files or keys are used.
#define main upstream_wua_test_main
#include "../wudextract/wua_test.cpp"
#undef main

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: extraction_fixture SYNTHETIC_RPX OUTPUT\n");
        return 2;
    }
    auto rpx = read_file(argv[1]);
    if (rpx.empty()) return 3;
    ZWriter writer;
    const std::string base = "0005000010143500_v0/";
    std::map<std::string, std::vector<uint8_t>> files{
        {"code/cking.rpx", rpx},
        {"meta/meta.xml", {'<', 'm', 'e', 'n', 'u', '/', '>'}},
        {"content/authored.bin", mixed(1300000, 42)}};
    std::ofstream manifest(fs::path(argv[2]).replace_extension(".json"));
    manifest << "{";
    bool first = true;
    for (const auto& [name, data] : files) {
        writer.add(base + name, data);
        wudcrypto::Sha256 hash;
        hash.update(data.data(), data.size());
        uint8_t bytes[32];
        hash.final(bytes);
        if (!first) manifest << ",";
        first = false;
        manifest << "\n\"" << name << "\":\"";
        for (uint8_t byte : bytes) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02x", byte);
            manifest << hex;
        }
        manifest << "\"";
    }
    manifest << "\n}\n";
    write_file(argv[2], writer.finish());
    return manifest.good() ? 0 : 4;
}

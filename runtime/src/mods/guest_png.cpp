#include "guest_png.h"
#include "mod_archive.h"
#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 2048
#include "../../third_party/stb/stb_image.h"

namespace guestmods::hud {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
}
Pixels decode_png(std::span<const uint8_t> bytes) {
    constexpr std::array<uint8_t,8> signature{137,80,78,71,13,10,26,10};
    require(bytes.size()>=signature.size()&&bytes.size()<=kMaxTextureBytes&&
            std::equal(signature.begin(),signature.end(),bytes.begin()),"HUD texture must be a PNG of at most 16 MiB");
    int width=0,height=0,channels=0;
    require(stbi_info_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels)&&
            width>0&&height>0&&width<=int(kMaxTextureDimension)&&height<=int(kMaxTextureDimension)&&
            uint64_t(width)*height*4<=kMaxTextureBytes,"HUD PNG dimensions are invalid or exceed 2048 pixels");
    std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels,4),stbi_image_free);
    require(bool(pixels),"HUD PNG could not be decoded");
    return {uint32_t(width),uint32_t(height),{pixels.get(),pixels.get()+size_t(width)*height*4}};
}
Pixels load_png(const std::filesystem::path& root,const std::string& relative) try {
    namespace fs=std::filesystem;
    require(mods::archive::relative_path(relative),"Invalid HUD texture path");
    auto path=root;
    require(fs::is_directory(root)&&!fs::is_symlink(root),"HUD texture folder is unavailable");
    for(const auto& part:fs::path(relative)) {
        path/=part;require(!fs::is_symlink(path),"HUD texture paths may not use symlinks");
    }
    require(fs::is_regular_file(path),"HUD texture is missing");
    auto size=fs::file_size(path);
    require(size<=kMaxTextureBytes,"HUD PNG exceeds 16 MiB");
    std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes(size);
    require(bool(file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size))),"HUD texture could not be read");
    return decode_png(bytes);
} catch(const std::filesystem::filesystem_error&) {
    throw std::runtime_error("HUD texture filesystem could not be read");
}
} // namespace guestmods::hud

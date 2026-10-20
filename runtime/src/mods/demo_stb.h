// SPDX-License-Identifier: MPL-2.0
// JStudio STB / FVB files (the game's demo data, played by dDemo_manager_c): a lossless model with a
// parser and a writer (any STB the game ships parses and writes back byte for byte), a readable dump,
// and a builder that mod demo scenes compile into. Layout from the JStudio sources of the GameCube
// decomp (stb-data-parse, fvb, jstudio-object) and checked against the retail demos.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace demo_stb {
using Bytes = std::vector<uint8_t>;

// One paragraph: a 16- or 32-bit header (size, type) and its data (padded to 4 bytes in the file).
// type = (code << 5) | operation for track paragraphs, 0x01..0xFF reserved paragraphs (0x81: data).
struct Paragraph {
    uint32_t type = 0;
    Bytes data;
    bool long_header = false;  // 32-bit header form (size >= 0x8000 or as found in the file)
    bool operator==(const Paragraph&) const = default;
};
// One sequence item: type 0 end, 1 flag, 2 wait (param frames), 3 jump, 4 suspend, 0x80 paragraphs.
struct Item {
    uint8_t type = 0;
    uint32_t param = 0;  // for 0x80 the byte length of the paragraphs (recomputed by the writer)
    std::vector<Paragraph> paragraphs;
    bool operator==(const Item&) const = default;
};
struct Object {
    uint32_t type = 0;  // 'JACT', 'JCMR', 'JSND', 'JPTC', 'JMSG', 'JLIT', 'JFOG', 'JABL', 0xFFFFFFFF control
    uint16_t flag = 0;
    Bytes id;  // id_size bytes as stored (usually the name and its NUL)
    std::vector<Item> sequence;
    std::string name() const;
    bool operator==(const Object&) const = default;
};
// FVB function value: 1 composite, 2 constant, 3 transition, 4 list, 5 list_parameter, 6 hermite.
struct Function {
    uint16_t type = 0;
    Bytes id;
    std::vector<Paragraph> paragraphs;  // including the type-0 terminator when present
    bool operator==(const Function&) const = default;
};
struct File {
    uint16_t version = 3;
    Bytes target = Bytes{'j', 's', 't', 'u', 'd', 'i', 'o', 0, 0, 0, 0, 0, 0, 0, 0, 3};  // 16 bytes
    bool has_fvb = false;
    uint16_t fvb_version = 0x0100;
    std::vector<Function> functions;
    size_t fvb_position = 0;  // block index of the JFVB block
    std::vector<Object> objects;
    bool operator==(const File&) const = default;
};

bool parse(const Bytes& data, File& out, std::string& why);
Bytes write(const File& file);
std::string dump(const File& file);  // one line per value, for tests and diagnostics

// operations of a track paragraph (JStudio data::TEOperationData)
enum Op : uint32_t { kVoid = 1, kImmediate = 2, kTime = 3, kCurveName = 0x10, kCurveIndex = 0x12, kName = 0x18, kValue = 0x19 };
// paragraph codes (type >> 5) per object kind
namespace actor {
enum : uint32_t { TX = 9, TY = 10, TZ = 11, TXYZ = 12, RX = 13, RY = 14, RZ = 15, RXYZ = 16, SX = 17, SY = 18, SZ = 19, SXYZ = 20,
                  Parent = 48, ParentNode = 49, ParentEnable = 50, Relation = 51, RelationNode = 52, RelationEnable = 53,
                  Shape = 57, Animation = 58, AnimationFrame = 59, AnimationMode = 67, TextureFrame = 75, Texture = 76, TextureMode = 78 };
}
namespace camera {
enum : uint32_t { EyeX = 21, EyeY = 22, EyeZ = 23, Eye = 24, TargetX = 25, TargetY = 26, TargetZ = 27, Target = 28,
                  Roll = 38, Fovy = 39, Near = 40, Far = 41, NearFar = 42, Parent = 48, ParentNode = 49, ParentEnable = 50 };
}
namespace sound {
enum : uint32_t { X = 21, Y = 22, Z = 23, XYZ = 24, Volume = 46, Pan = 47, Located = 56, Sound = 60, Pitch = 61, Tempo = 62, Echo = 63, Dolby = 64, Direct = 65 };
}
namespace particle {
enum : uint32_t { TX = 9, TY = 10, TZ = 11, TXYZ = 12, RXYZ = 16, SXYZ = 20, R = 29, G = 30, B = 31, A = 32,
                  Parent = 48, ParentNode = 49, ParentEnable = 50, Particle = 68 };
}
namespace message {
enum : uint32_t { Message = 66 };
}
constexpr uint32_t fourcc(const char (&s)[5]) { return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]); }
constexpr uint32_t kActor = fourcc("JACT"), kCamera = fourcc("JCMR"), kSound = fourcc("JSND"), kParticle = fourcc("JPTC"), kMessage = fourcc("JMSG"), kControl = 0xFFFFFFFFu;

// Builder for written demos: curves in seconds (JStudio evaluates curves at frames / 30),
// events at 30 Hz demo frames.
enum class Interpolation : uint32_t { None = 0, Linear = 1, Plateau = 2, BSpline = 3 };
struct HermiteKey { float time, value, in, out; };
class Builder {
public:
    uint32_t constant(float value);
    uint32_t list(const std::vector<std::pair<float, float>>& keys, Interpolation interpolation);  // (seconds, value)
    uint32_t hermite(const std::vector<HermiteKey>& keys);
    size_t object(uint32_t type, const std::string& name);  // object index
    void immediate(size_t object, uint32_t frame, uint32_t code, std::initializer_list<float> values);
    void immediate_word(size_t object, uint32_t frame, uint32_t code, uint32_t value);  // e.g. animation mode
    void value(size_t object, uint32_t frame, uint32_t code, uint32_t value);           // operation 0x19 (ids)
    void time(size_t object, uint32_t frame, uint32_t code, float rate);
    void curves(size_t object, uint32_t frame, uint32_t code, std::initializer_list<uint32_t> functions);
    void name(size_t object, uint32_t frame, uint32_t code, const std::string& text);
    void data(size_t object, uint32_t frame, uint32_t id, const Bytes& payload);  // reserved paragraph 0x81
    void end(size_t object, uint32_t frame);  // the object's sequence ends at this frame
    void suspend(uint32_t frame);             // the demo waits here until the port resumes it
    size_t function_count() const { return functions_.size(); }
    Bytes finish() const;

private:
    struct Event { uint32_t frame; uint32_t order; bool suspend; Paragraph paragraph; };
    struct Pending { uint32_t type; std::string name; std::vector<Event> events; uint32_t end = 0; };
    void add(size_t object, uint32_t frame, Paragraph p);
    std::vector<Function> functions_;
    std::vector<Pending> objects_;
    std::vector<uint32_t> suspends_;
    uint32_t order_ = 0;
};
}  // namespace demo_stb

// Which phones may be the GamePad (remote_pad.h): pairing with a code, as with Bluetooth.
//
// A phone has a random 16-byte id. The first time it says Hello, the computer shows a 6-digit code
// (Settings opens with it); the player types it on the phone, which sends it back (protocol.h kPair).
// The right code makes the phone trusted: it gets a random 16-byte token, which its later Hellos carry.
// A code allows kAttempts tries and lasts kCodeLifetimeMs; one phone pairs at a time. After a code is
// used up, refused or expires, the next one comes kCooldownMs later at the earliest: guessing would take
// weeks, and every new code opens Settings on the computer, so it is seen.
//
// What this protects: another device on the network cannot take the GamePad, and a phone cannot pair
// without someone at the computer. What it does not: the token travels in the clear, so someone who
// captures the network traffic could replay it (there is no encryption).
//
// Thread-safe (one mutex); the time is passed in (milliseconds on any monotonic clock) for the tests.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace remote_pad {

using Key = std::array<uint8_t, 16>;

class Pairing {
public:
    static constexpr int kAttempts = 3;
    static constexpr uint64_t kCodeLifetimeMs = 120'000;
    static constexpr uint64_t kCooldownMs = 30'000;
    static constexpr size_t kMaxPhones = 16;  // the oldest is forgotten beyond this

    struct Phone { Key id{}, token{}; std::string name; };
    struct Pending { Key id{}; std::string name; uint32_t code = 0; int attempts = kAttempts; uint64_t until = 0; };

    // random bytes (tests pass their own); the default uses std::random_device
    explicit Pairing(std::function<void(uint8_t*, size_t)> random = {});

    bool trusted(const Key& id, const Key& token) const;

    // An unknown phone said Hello: its pending code (a new one unless it already has an unexpired one).
    // fresh: the code is new (the computer shows it now). busy: no code for it now (another phone is
    // pairing, or the cooldown runs).
    struct Request { uint32_t code = 0; int attempts = 0; bool fresh = false, busy = false; };
    Request request(const Key& id, const std::string& name, uint64_t now);

    // The phone sent a code. kPaired: trusted from now on, *token is its token. kWrong: *attempts_left
    // tries remain (0: the code is gone, the phone must ask again). kNone: no code is pending for it
    // (expired, refused at the computer, or another phone's).
    enum Result { kPaired, kWrong, kNone };
    Result answer(const Key& id, uint32_t code, uint64_t now, Key* token, int* attempts_left);

    std::optional<Pending> pending(uint64_t now) const;  // what the computer shows, if anything
    void refuse(uint64_t now);                            // "Don't pair"

    size_t size() const;
    std::vector<std::string> names() const;
    void forget_all();

    // the trusted phones as one settings value (one line): "id-hex:token-hex:name" joined by ';' (names
    // lose control characters and ';'); load() skips anything malformed
    std::string save() const;
    void load(const std::string& saved);

private:
    Key random_key();
    void end_pending(uint64_t now);  // mu_ held
    mutable std::mutex mu_;
    std::function<void(uint8_t*, size_t)> random_;
    std::vector<Phone> phones_;  // oldest first
    std::optional<Pending> pending_;
    uint64_t next_code_ = 0;     // no new code before this
};

std::string hex(const Key& k);
bool parse_hex(const std::string& s, Key& out);

}  // namespace remote_pad

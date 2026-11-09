// Pairing with a code (pairing.h).
#include "pairing.h"

#include <algorithm>
#include <random>

namespace remote_pad {

std::string hex(const Key& k) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : k) { s += d[b >> 4]; s += d[b & 15]; }
    return s;
}
bool parse_hex(const std::string& s, Key& out) {
    if (s.size() != 32) return false;
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < 16; i++) {
        const int hi = v(s[2 * i]), lo = v(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = uint8_t(hi << 4 | lo);
    }
    return true;
}

Pairing::Pairing(std::function<void(uint8_t*, size_t)> random) : random_(std::move(random)) {
    if (!random_)
        random_ = [](uint8_t* p, size_t n) {
            static std::random_device dev;  // (the system's secure generator on Apple platforms and Linux)
            for (size_t i = 0; i < n; i += 4) {
                const uint32_t r = dev();
                for (size_t k = 0; k < 4 && i + k < n; k++) p[i + k] = uint8_t(r >> (8 * k));
            }
        };
}

Key Pairing::random_key() {
    Key k{};
    random_(k.data(), k.size());
    return k;
}

bool Pairing::trusted(const Key& id, const Key& token) const {
    std::lock_guard lk(mu_);
    return std::any_of(phones_.begin(), phones_.end(), [&](const Phone& p) { return p.id == id && p.token == token; });
}

void Pairing::end_pending(uint64_t now) {
    pending_.reset();
    next_code_ = now + kCooldownMs;
}

Pairing::Request Pairing::request(const Key& id, const std::string& name, uint64_t now) {
    std::lock_guard lk(mu_);
    if (pending_ && now >= pending_->until) end_pending(pending_->until);
    if (pending_) {
        if (pending_->id == id) return {pending_->code, pending_->attempts, false, false};
        return {0, 0, false, true};
    }
    if (now < next_code_) return {0, 0, false, true};
    uint8_t r[4];
    random_(r, 4);
    const uint32_t code = (uint32_t(r[0]) | uint32_t(r[1]) << 8 | uint32_t(r[2]) << 16 | uint32_t(r[3]) << 24) % 1'000'000;
    pending_ = Pending{id, name, code, kAttempts, now + kCodeLifetimeMs};
    return {code, kAttempts, true, false};
}

Pairing::Result Pairing::answer(const Key& id, uint32_t code, uint64_t now, Key* token, int* attempts_left) {
    std::lock_guard lk(mu_);
    if (pending_ && now >= pending_->until) end_pending(pending_->until);
    if (!pending_ || pending_->id != id) return kNone;
    if (code != pending_->code) {
        const int left = --pending_->attempts;
        if (attempts_left) *attempts_left = left;
        if (left <= 0) end_pending(now);
        return kWrong;
    }
    Phone p{id, random_key(), pending_->name};
    std::erase_if(phones_, [&](const Phone& q) { return q.id == id; });  // paired again: the new token
    phones_.push_back(p);
    if (phones_.size() > kMaxPhones) phones_.erase(phones_.begin());
    pending_.reset();
    if (token) *token = p.token;
    return kPaired;
}

std::optional<Pairing::Pending> Pairing::pending(uint64_t now) const {
    std::lock_guard lk(mu_);
    if (pending_ && now < pending_->until) return pending_;
    return std::nullopt;
}
void Pairing::refuse(uint64_t now) {
    std::lock_guard lk(mu_);
    if (pending_) end_pending(now);
}

size_t Pairing::size() const {
    std::lock_guard lk(mu_);
    return phones_.size();
}
std::vector<std::string> Pairing::names() const {
    std::lock_guard lk(mu_);
    std::vector<std::string> out;
    for (const auto& p : phones_) out.push_back(p.name);
    return out;
}
void Pairing::forget_all() {
    std::lock_guard lk(mu_);
    phones_.clear();
    pending_.reset();
}

std::string Pairing::save() const {
    std::lock_guard lk(mu_);
    std::string out;
    for (const auto& p : phones_) {
        std::string name;
        for (char c : p.name) if (uint8_t(c) >= 0x20 && c != ';') name += c;  // one line, ';' separates phones
        if (!out.empty()) out += ';';
        out += hex(p.id) + ":" + hex(p.token) + ":" + name;
    }
    return out;
}
void Pairing::load(const std::string& saved) {
    std::lock_guard lk(mu_);
    phones_.clear();
    size_t at = 0;
    while (at < saved.size()) {
        size_t end = saved.find(';', at);
        if (end == std::string::npos) end = saved.size();
        const std::string line = saved.substr(at, end - at);
        at = end + 1;
        Phone p;
        if (line.size() < 66 || line[32] != ':' || line[65] != ':') continue;
        if (!parse_hex(line.substr(0, 32), p.id) || !parse_hex(line.substr(33, 32), p.token)) continue;
        p.name = line.substr(66);
        phones_.push_back(p);
    }
    if (phones_.size() > kMaxPhones) phones_.erase(phones_.begin(), phones_.end() - kMaxPhones);
}

}  // namespace remote_pad

// SPDX-License-Identifier: MPL-2.0
// The online protocol's cryptography behind one small interface (docs/online-server.md, "Crypto backends").
//
// Two backends share this API:
//  - "plain" (crypto_plain.cpp, the default today): NO secrecy and NO real authentication. Keys are derived so that
//    the protocol, the handshake, friend codes and session resumption all work end to end, but anyone who sees the
//    traffic can read and forge it. For LAN tests and development only; the server refuses to start in public mode
//    with it unless `allow_insecure = true` is set.
//  - "monocypher" (planned; Ed25519 signatures, X25519 key exchange, ChaCha20-Poly1305, BLAKE2b): waits for the
//    user's OK on the dependency. It drops in behind this header without protocol changes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace online::proto::crypto {

constexpr size_t kKeyBytes = 32, kSigBytes = 64, kTagBytes = 16;

const char* backend_name();
bool backend_secure();  // false for "plain"

void random_bytes(void* out, size_t n);

// the long-lived identity of an install (or a server): a signing keypair
struct Identity {
    uint8_t secret[kKeyBytes]{};
    uint8_t pub[kKeyBytes]{};
    static Identity generate();
    static Identity from_secret(const uint8_t secret[kKeyBytes]);
    // reads `path` (64 hex chars of the secret) or makes a new identity there; false + why on an I/O error
    static bool load_or_create(const std::string& path, Identity& out, std::string& why);
    bool save(const std::string& path, std::string& why) const;
};

void sign(const Identity& id, const void* msg, size_t n, uint8_t sig[kSigBytes]);
bool verify(const uint8_t pub[kKeyBytes], const void* msg, size_t n, const uint8_t sig[kSigBytes]);

// a per-session key exchange
struct Ephemeral {
    uint8_t secret[kKeyBytes]{};
    uint8_t pub[kKeyBytes]{};
    static Ephemeral generate();
};
// both sides derive the same pair: the client's tx is the server's rx. False if `their_pub` is unusable.
bool session_keys(const Ephemeral& mine, const uint8_t their_pub[kKeyBytes], bool i_am_client,
                  uint8_t tx[kKeyBytes], uint8_t rx[kKeyBytes]);

// authenticated encryption of `data` in place; `nonce` must never repeat for one key (the packet number + direction)
void seal(const uint8_t key[kKeyBytes], uint64_t nonce, const void* ad, size_t ad_n, uint8_t* data, size_t n,
          uint8_t tag[kTagBytes]);
bool open(const uint8_t key[kKeyBytes], uint64_t nonce, const void* ad, size_t ad_n, uint8_t* data, size_t n,
          const uint8_t tag[kTagBytes]);

// a general-purpose 32-byte hash (SHA-256 for both backends today; it is not part of the security boundary)
void hash(const void* data, size_t n, uint8_t out[32]);

}  // namespace online::proto::crypto

// SPDX-License-Identifier: MPL-2.0
// The "monocypher" crypto backend (crypto.h), on the vendored Monocypher 4.0.2 (third_party/monocypher, BSD-2 or
// CC0):
//  - identities: EdDSA over Curve25519 with BLAKE2b (Monocypher's crypto_eddsa_*); the identity file keeps the
//    32-byte seed, the public key is derived from it;
//  - the session: X25519 between the two sides' fresh ephemeral keys; the shared secret and both public keys go
//    through keyed BLAKE2b into one key per direction;
//  - packets: XChaCha20-Poly1305 (crypto_aead_lock), the 64-bit packet nonce (packet number + direction) in the first
//    8 bytes of the 24-byte nonce, the packet header as additional data.
// The wire format is the plain backend's: same sizes (32-byte keys, 64-byte signatures, 16-byte tags).
#include <cstring>

#include "crypto.h"
#include "sha256.h"
#include "third_party/monocypher/monocypher.h"

namespace online::proto::crypto {

const char* backend_name() { return "monocypher (X25519, XChaCha20-Poly1305, EdDSA)"; }
bool backend_secure() { return true; }

Identity Identity::from_secret(const uint8_t secret[kKeyBytes]) {
    Identity id;
    std::memcpy(id.secret, secret, kKeyBytes);
    uint8_t seed[32], sk[64];
    std::memcpy(seed, secret, 32);  // crypto_eddsa_key_pair wipes the seed it is given
    crypto_eddsa_key_pair(sk, id.pub, seed);
    crypto_wipe(sk, sizeof sk);
    return id;
}

void sign(const Identity& id, const void* msg, size_t n, uint8_t sig[kSigBytes]) {
    uint8_t seed[32], sk[64], pk[32];
    std::memcpy(seed, id.secret, 32);
    crypto_eddsa_key_pair(sk, pk, seed);
    crypto_eddsa_sign(sig, sk, static_cast<const uint8_t*>(msg), n);
    crypto_wipe(sk, sizeof sk);
}

bool verify(const uint8_t pub[kKeyBytes], const void* msg, size_t n, const uint8_t sig[kSigBytes]) {
    return crypto_eddsa_check(sig, pub, static_cast<const uint8_t*>(msg), n) == 0;
}

Ephemeral Ephemeral::generate() {
    Ephemeral e;
    random_bytes(e.secret, kKeyBytes);
    crypto_x25519_public_key(e.pub, e.secret);
    return e;
}

bool session_keys(const Ephemeral& mine, const uint8_t their_pub[kKeyBytes], bool i_am_client, uint8_t tx[kKeyBytes],
                  uint8_t rx[kKeyBytes]) {
    uint8_t shared[32];
    crypto_x25519(shared, mine.secret, their_pub);
    // a low-order point gives an all-zero secret: refuse it
    uint8_t zero[32] = {};
    if (crypto_verify32(shared, zero) == 0) return false;
    const uint8_t* c = i_am_client ? mine.pub : their_pub;
    const uint8_t* s = i_am_client ? their_pub : mine.pub;
    uint8_t c2s[32], s2c[32];
    for (int dir = 0; dir < 2; ++dir) {
        crypto_blake2b_ctx ctx;
        crypto_blake2b_keyed_init(&ctx, 32, shared, 32);
        const char* label = dir == 0 ? "wwhd-online c2s" : "wwhd-online s2c";
        crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(label), std::strlen(label));
        crypto_blake2b_update(&ctx, c, 32);
        crypto_blake2b_update(&ctx, s, 32);
        crypto_blake2b_final(&ctx, dir == 0 ? c2s : s2c);
    }
    std::memcpy(tx, i_am_client ? c2s : s2c, kKeyBytes);
    std::memcpy(rx, i_am_client ? s2c : c2s, kKeyBytes);
    crypto_wipe(shared, sizeof shared);
    crypto_wipe(c2s, sizeof c2s);
    crypto_wipe(s2c, sizeof s2c);
    return true;
}

namespace {
void nonce24(uint64_t nonce, uint8_t out[24]) {
    std::memset(out, 0, 24);
    for (int i = 0; i < 8; ++i) out[i] = uint8_t(nonce >> (8 * i));
}
}  // namespace

void seal(const uint8_t key[kKeyBytes], uint64_t nonce, const void* ad, size_t ad_n, uint8_t* data, size_t n,
          uint8_t tag[kTagBytes]) {
    uint8_t nn[24];
    nonce24(nonce, nn);
    crypto_aead_lock(data, tag, key, nn, static_cast<const uint8_t*>(ad), ad_n, data, n);
}

bool open(const uint8_t key[kKeyBytes], uint64_t nonce, const void* ad, size_t ad_n, uint8_t* data, size_t n,
          const uint8_t tag[kTagBytes]) {
    uint8_t nn[24];
    nonce24(nonce, nn);
    return crypto_aead_unlock(data, tag, key, nn, static_cast<const uint8_t*>(ad), ad_n, data, n) == 0;
}

void hash(const void* data, size_t n, uint8_t out[32]) { sha256(data, n, out); }

}  // namespace online::proto::crypto

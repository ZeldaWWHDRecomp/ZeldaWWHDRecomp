// SPDX-License-Identifier: MPL-2.0
// The handshake packets (docs/online-server.md, "Handshake"):
//   C->S CONNECT   (padded to a full datagram, so the server never answers with more than it got)
//   S->C CHALLENGE (salt + difficulty for the proof-of-work, the server's key and info, signed by the server)
//   C->S HELLO     (proto, game data, identity key, session key, proof-of-work, name, resume token; signed)
//   S->C WELCOME   (connection id, the server's session key, signed; a sealed part with the player id and token)
//   S->C REFUSE    (reason)
// The client's random 32-bit nonce runs through all of them: replies are matched by it (a bot process demuxes many
// clients on one socket by it) and a blind attacker cannot forge a REFUSE.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "crypto.h"
#include "protocol.h"
#include "wire.h"

namespace online::proto {

struct ServerInfo {
    std::string name;      // <= 48
    std::string contact;   // <= 96 (operator's name / contact, shown before the first connection)
    std::string rules;     // <= 600
    uint16_t players = 0, capacity = 0;
    uint8_t flags = 0;     // bit 0: secure crypto backend, bit 1: invite-only, bit 2: public
    uint8_t pub[32]{};     // the server's identity key (pinned by the client: trust on first use)
};

struct Connect {
    uint16_t proto = kProtoVersion;
    uint32_t nonce = 0;
};
struct Challenge {
    uint16_t proto = kProtoVersion;
    uint32_t nonce = 0;
    uint8_t salt[16]{};
    uint32_t bucket = 0;
    uint8_t difficulty = 0;
    ServerInfo info;
    uint8_t sig[64]{};
};
enum DeviceClass : uint8_t { kDesktop = 0, kPhone = 1, kBot = 2 };
struct Hello {
    uint16_t proto = kProtoVersion;
    uint32_t nonce = 0;
    uint64_t game_data = 0;
    uint8_t salt[16]{};
    uint32_t bucket = 0;
    uint8_t pub[32]{};
    uint8_t eph[32]{};
    uint64_t pow = 0;
    std::string name;  // <= 24
    uint32_t look = 0;
    uint8_t device = kDesktop;
    uint8_t near_cap = 0, mid_cap = 0;  // 0: the server's default for the device class
    bool resume = false;
    uint8_t token[32]{};  // resume token, or a shard handover ticket
    std::string tag;      // client build tag, <= 16 (logs only)
    uint8_t sig[64]{};
};
struct Welcome {
    uint32_t nonce = 0;
    uint32_t conn_id = 0;
    uint8_t eph[32]{};
    uint8_t sig[64]{};
    // sealed with the server->client session key, nonce 0
    uint16_t id = 0;
    uint8_t token[32]{};
    uint8_t tick_hz = 15;
    bool resumed = false;
};
struct Refuse {
    uint32_t nonce = 0;
    Refusal reason = Refusal::kMalformed;
    uint16_t retry_s = 0;
    std::string text;
};

Bytes encode_connect(const Connect& c);
bool decode_connect(const uint8_t* d, size_t n, Connect& c);

Bytes encode_challenge(const Challenge& c, const crypto::Identity& server);
// checks the server's signature over it
bool decode_challenge(const uint8_t* d, size_t n, Challenge& c);

Bytes encode_hello(const Hello& h, const crypto::Identity& me);
// parses; `signed_ok` tells whether the signature checks (the server checks the cheap things first)
bool decode_hello(const uint8_t* d, size_t n, Hello& h, size_t& signed_len);
bool hello_signature_ok(const uint8_t* d, size_t signed_len, const Hello& h);

Bytes encode_welcome(const Welcome& w, const crypto::Identity& server, const uint8_t client_eph[32],
                     const uint8_t s2c_key[32]);
// the outer part (no keys needed): nonce, conn id, eph
bool decode_welcome_outer(const uint8_t* d, size_t n, Welcome& w);
// checks the server signature and opens the sealed part
bool decode_welcome_inner(const uint8_t* d, size_t n, const uint8_t server_pub[32], const uint8_t client_eph[32],
                          const uint8_t s2c_key[32], Welcome& w);

Bytes encode_refuse(const Refuse& r);
bool decode_refuse(const uint8_t* d, size_t n, Refuse& r);

// a 32-bit nonce from a handshake packet (any of them), for demuxing
bool handshake_nonce(const uint8_t* d, size_t n, uint32_t& nonce);

// the server's key fingerprint shown to players: "3F9A 0C12 77E4 B801"
std::string fingerprint(const uint8_t pub[32]);

}  // namespace online::proto

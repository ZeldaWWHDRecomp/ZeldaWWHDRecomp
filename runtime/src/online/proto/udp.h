// SPDX-License-Identifier: MPL-2.0
// A small non-blocking UDP socket (POSIX and Winsock), IPv4 and IPv6.
#pragma once
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace online::proto {

struct Address {
    uint8_t storage[28]{};  // sockaddr_in or sockaddr_in6
    uint8_t len = 0;
    bool valid() const { return len != 0; }
    bool operator==(const Address& o) const;
    std::string text() const;  // "1.2.3.4:7777" (logs never print it unless asked; see the server)
    // bytes of the IP only (for per-IP limits), port excluded
    std::string ip_bytes() const;
    bool is_loopback() const;
    uint64_t hash() const;
};

// resolves "host" + port (numeric or names; no network lookups for numeric addresses)
bool resolve(const std::string& host, uint16_t port, Address& out, std::string& why);

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // bind to host:port (port 0: any); host "" binds to 127.0.0.1
    bool open(const std::string& host, uint16_t port, std::string& why, bool reuse_port = false);
    void close();
    bool is_open() const;
    uint16_t local_port() const;

    bool send_to(const Address& to, const uint8_t* d, size_t n);
    // several datagrams to one address: one sendmmsg() on Linux, a loop elsewhere; the count sent
    size_t send_many(const Address& to, const std::vector<std::vector<uint8_t>>& dgs);
    // one datagram, or -1 when none is waiting
    int recv_from(Address& from, uint8_t* buf, size_t cap);
    // waits until readable or timeout (ms); true if readable
    bool wait(int timeout_ms);
    void set_buffers(int bytes);

    std::atomic<uint64_t> send_errors{0};  // send_to may be called from several threads

private:
#ifdef _WIN32
    uintptr_t s_ = ~uintptr_t(0);
#else
    int s_ = -1;
#endif
};

uint32_t monotonic_ms();  // milliseconds since first call (never 0)
uint64_t monotonic_us();

}  // namespace online::proto

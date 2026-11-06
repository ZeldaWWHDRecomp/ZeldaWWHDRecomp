// SPDX-License-Identifier: MPL-2.0
// The TCP fallback for networks that block UDP (C25): a non-blocking stream that carries the protocol's datagrams
// unchanged, each as a frame of a u16 big-endian length and the datagram's bytes (at most kMaxTcpFrame).
// The same handshake, crypto, channel and limits run on top; only the transport differs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "udp.h"

namespace online::proto {

constexpr size_t kMaxTcpFrame = 1500;
constexpr size_t kTcpBackedUp = 64 * 1024;     // above this, unreliable data waits for the next tick
constexpr size_t kTcpMaxQueued = 1024 * 1024;  // above this the stream is closed

class TcpStream {
public:
    TcpStream() = default;
    ~TcpStream();
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    // starts a non-blocking connect; frames sent before it completes are queued
    bool connect(const Address& to, std::string& why);
    // takes an accepted socket
    void adopt(intptr_t fd, const Address& peer);
    void close();
    bool is_open() const { return fd_ >= 0; }
    bool closed() const { return closed_; }  // the peer closed it, or it failed
    bool connected() const { return connected_; }
    const Address& peer() const { return peer_; }

    // queues one datagram as a frame; false if the stream is closed or over kTcpMaxQueued
    bool send_frame(const uint8_t* d, size_t n);
    // the next whole datagram received, or -1
    int recv_frame(uint8_t* buf, size_t cap);
    // writes what is queued and reads what has arrived (call often)
    void pump();
    size_t queued() const { return out_.size() - out_at_; }
    intptr_t fd() const { return fd_; }

private:
    intptr_t fd_ = -1;
    bool connected_ = false, closed_ = false;
    Address peer_;
    std::vector<uint8_t> out_, in_;
    size_t out_at_ = 0, in_at_ = 0;
};

class TcpListener {
public:
    ~TcpListener();
    bool open(const std::string& host, uint16_t port, std::string& why);
    void close();
    bool is_open() const { return fd_ >= 0; }
    // the next waiting connection (non-blocking), or false
    bool accept(intptr_t& fd, Address& peer);

private:
    intptr_t fd_ = -1;
};

}  // namespace online::proto

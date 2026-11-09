// The computer's side of the remote GamePad protocol (protocol.h): one UDP socket on a background
// thread, one phone at a time (like the one Wii U GamePad).
//
// A Hello takes the GamePad when nobody has it, when it comes from the phone that has it (it
// reconnected), or when the phone that has it stopped sending (kTimeout). Inputs are kept only from the
// phone with the session and only when newer. Silence for kTimeout or a Bye ends the session.
//
// Threads: the callbacks run on the server thread and must be short (they only store a sample).
// snapshot() is safe from any thread. stop() joins the thread.
#pragma once
#include "protocol.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace remote_pad {

class Server {
public:
    static constexpr std::chrono::milliseconds kTimeout{1500};

    struct Snapshot {
        bool connected = false;
        uint32_t session = 0;
        std::string device, controller, address;
        bool video = false;          // the phone asked for the GamePad screen (kWantsVideo)
        Input input;                 // the latest (all zero until the first Input)
        uint64_t inputs = 0;         // Inputs taken this session
    };
    struct Callbacks {
        std::function<void(const Snapshot&)> connected;  // a phone took the GamePad
        std::function<void(const Input&)> input;         // a newer Input from it
        std::function<void(const std::string& why)> disconnected;
        std::function<void()> keyframe;                  // the phone lost a piece of video
    };

    explicit Server(Callbacks cb, std::string host_name = {});
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // binds every address (IPv6 and IPv4) on `port` (0: any free port, see port()); false on failure
    bool start(uint16_t port);
    void stop();
    bool running() const { return running_.load(); }
    uint16_t port() const { return port_; }
    Snapshot snapshot() const;
    // Any thread: one encoded frame to the phone (protocol.h kVideo) if it asked for video; false when
    // nobody takes it. Sent from the caller's thread, outside the server's lock.
    bool send_video(const uint8_t* data, size_t size, bool key, uint64_t time_ns, uint16_t width, uint16_t height);

private:
    void run();
    void end_session(const char* why);  // mu_ held

    Callbacks cb_;
    std::string host_;
    int sock_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex mu_;
    std::mutex send_mu_;             // held while sending video: stop() closes the socket under it
    Snapshot state_;
    uint8_t peer_[128] = {};         // sockaddr_storage of the phone with the session
    uint32_t peer_len_ = 0;
    std::chrono::steady_clock::time_point seen_{}, keepalive_{};
    uint32_t hello_nonce_ = 0;
    uint32_t video_frame_ = 0;       // mu_
};

}  // namespace remote_pad

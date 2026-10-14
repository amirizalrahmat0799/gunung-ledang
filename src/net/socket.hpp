#pragma once
// Minimal non-blocking IPv4 UDP socket for Windows (Winsock) and POSIX.

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ledang {

struct Address {
    uint32_t ip = 0;     // host byte order
    uint16_t port = 0;   // host byte order

    bool operator==(const Address&) const = default;
    std::string to_string() const;
    /// Resolves "host:port" or "host" (with default_port). Returns nullopt if it can't be resolved.
    static std::optional<Address> parse(const std::string& text, uint16_t default_port);
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    /// Binds to the port on all interfaces (port 0 picks a free one). Returns false on failure.
    bool open(uint16_t port);
    void close();
    bool is_open() const { return handle_ != invalid_handle; }
    uint16_t local_port() const { return local_port_; }

    bool send(const Address& to, std::span<const uint8_t> data);
    /// Receives one datagram if one is waiting; returns its size, or 0 if none.
    size_t receive(Address& from, std::span<uint8_t> buffer);

private:
#ifdef _WIN32
    using Handle = uintptr_t;
#else
    using Handle = int;
#endif
    static constexpr Handle invalid_handle = static_cast<Handle>(-1);
    Handle handle_ = invalid_handle;
    uint16_t local_port_ = 0;
};

/// Call once before using sockets (initialises Winsock on Windows).
bool init_sockets();
void shutdown_sockets();

}  // namespace ledang

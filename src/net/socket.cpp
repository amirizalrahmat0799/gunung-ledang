#include "net/socket.hpp"

#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ledang {

bool init_sockets() {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void shutdown_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

std::string Address::to_string() const {
    return std::to_string((ip >> 24) & 0xFF) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF) + ":" + std::to_string(port);
}

std::optional<Address> Address::parse(const std::string& text, uint16_t default_port) {
    std::string host = text;
    uint16_t port = default_port;
    const auto colon = text.rfind(':');
    if (colon != std::string::npos) {
        host = text.substr(0, colon);
        try {
            const int p = std::stoi(text.substr(colon + 1));
            if (p <= 0 || p > 65535) return std::nullopt;
            port = static_cast<uint16_t>(p);
        } catch (...) {
            return std::nullopt;
        }
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) return std::nullopt;
    const auto* in = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    Address a{ntohl(in->sin_addr.s_addr), port};
    freeaddrinfo(result);
    return a;
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(uint16_t port) {
    close();
    const auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return false;
    handle_ = static_cast<Handle>(s);
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    // Windows reports an ICMP "port unreachable" (a peer that just quit) as an error on the *next* recvfrom,
    // which would make one departed client stall everyone else's packets. Turn that off.
    BOOL report_reset = FALSE;
    DWORD returned = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &report_reset, sizeof report_reset, nullptr, 0,
             &returned, nullptr, nullptr);
#else
    if (s < 0) return false;
    handle_ = s;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int buffer = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof buffer);
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof buffer);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
        close();
        return false;
    }
    sockaddr_in bound{};
    socklen_t len = sizeof bound;
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    local_port_ = ntohs(bound.sin_port);
    return true;
}

void UdpSocket::close() {
    if (handle_ == invalid_handle) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(handle_));
#else
    ::close(handle_);
#endif
    handle_ = invalid_handle;
}

bool UdpSocket::send(const Address& to, std::span<const uint8_t> data) {
    if (handle_ == invalid_handle) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);
    const auto sent = ::sendto(handle_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                               reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
    return sent >= 0 && static_cast<size_t>(sent) == data.size();
}

size_t UdpSocket::receive(Address& from, std::span<uint8_t> buffer) {
    if (handle_ == invalid_handle) return 0;
    sockaddr_in addr{};
    socklen_t len = sizeof addr;
    const auto got = ::recvfrom(handle_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                                reinterpret_cast<sockaddr*>(&addr), &len);
    if (got <= 0) return 0;
    from.ip = ntohl(addr.sin_addr.s_addr);
    from.port = ntohs(addr.sin_port);
    return static_cast<size_t>(got);
}

}  // namespace ledang

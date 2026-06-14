#include "socket.hpp"

#include <array>
#include <cstring>
#include <memory>
#include <sstream>

#if defined(_WIN32)
#include <mstcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

namespace ioxx {

bool is_would_block(const std::error_code& error) noexcept {
    return detail::is_would_block_error(error);
}

} // namespace ioxx

namespace ioxx::detail {

namespace {

#if defined(_WIN32)
class WinsockRuntime {
public:
    WinsockRuntime() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            throw socket_exception("WSAStartup", socket_error_from_code(result));
        }
    }

    ~WinsockRuntime() { WSACleanup(); }
};
#endif

[[nodiscard]] int last_error_code() noexcept {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

} // namespace

void ensure_socket_runtime() {
#if defined(_WIN32)
    static WinsockRuntime runtime;
    (void)runtime;
#endif
}

std::error_code socket_error_from_code(int code) noexcept {
#if defined(_WIN32)
    return {code, std::system_category()};
#else
    return {code, std::generic_category()};
#endif
}

std::error_code last_socket_error() noexcept {
    return socket_error_from_code(last_error_code());
}

bool is_would_block_error(const std::error_code& error) noexcept {
#if defined(_WIN32)
    return error.value() == WSAEWOULDBLOCK;
#else
    return error == std::errc::operation_would_block || error.value() == EAGAIN ||
           error.value() == EWOULDBLOCK;
#endif
}

bool is_connect_in_progress(const std::error_code& error) noexcept {
    if (is_would_block_error(error)) {
        return true;
    }
#if defined(_WIN32)
    return error.value() == WSAEINPROGRESS || error.value() == WSAEALREADY ||
           error.value() == WSAEINVAL;
#else
    return error.value() == EINPROGRESS || error.value() == EALREADY;
#endif
}

native_handle_type to_native(socket_handle handle) noexcept {
#if defined(_WIN32)
    return static_cast<native_handle_type>(handle);
#else
    return handle;
#endif
}

socket_handle from_native(native_handle_type handle) noexcept {
#if defined(_WIN32)
    return static_cast<SOCKET>(handle);
#else
    return handle;
#endif
}

bool socket_is_valid(socket_handle handle) noexcept {
    return handle != invalid_socket_handle;
}

void close_socket(socket_handle handle) noexcept {
    if (!socket_is_valid(handle)) {
        return;
    }
#if defined(_WIN32)
    closesocket(handle);
#else
    close(handle);
#endif
}

void set_non_blocking(socket_handle handle, bool enabled, std::error_code& ec) noexcept {
#if defined(_WIN32)
    u_long mode = enabled ? 1UL : 0UL;
    if (ioctlsocket(handle, FIONBIO, &mode) == SOCKET_ERROR) {
        ec = last_socket_error();
        return;
    }
#else
    const int flags = fcntl(handle, F_GETFL, 0);
    if (flags == -1) {
        ec = last_socket_error();
        return;
    }

    const int next = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(handle, F_SETFL, next) == -1) {
        ec = last_socket_error();
        return;
    }
#endif
    ec.clear();
}

void set_non_blocking(socket_handle handle, bool enabled) {
    std::error_code ec;
    set_non_blocking(handle, enabled, ec);
    if (ec) {
        throw socket_exception("set_non_blocking", ec);
    }
}

void set_close_on_exec(socket_handle handle, std::error_code& ec) noexcept {
#if defined(_WIN32)
    (void)handle;
    ec.clear();
#else
    const int flags = fcntl(handle, F_GETFD, 0);
    if (flags == -1) {
        ec = last_socket_error();
        return;
    }
    if (fcntl(handle, F_SETFD, flags | FD_CLOEXEC) == -1) {
        ec = last_socket_error();
        return;
    }
    ec.clear();
#endif
}

void set_close_on_exec(socket_handle handle) {
    std::error_code ec;
    set_close_on_exec(handle, ec);
    if (ec) {
        throw socket_exception("set_close_on_exec", ec);
    }
}

void set_reuse_addr(socket_handle handle, bool enabled, std::error_code& ec) noexcept {
    set_socket_bool_option(handle, SOL_SOCKET, SO_REUSEADDR, enabled, ec);
}

void set_reuse_addr(socket_handle handle, bool enabled) {
    std::error_code ec;
    set_reuse_addr(handle, enabled, ec);
    if (ec) {
        throw socket_exception("set_reuse_addr", ec);
    }
}

void set_socket_bool_option(socket_handle handle, int level, int option, bool enabled, std::error_code& ec) noexcept {
    const int value = enabled ? 1 : 0;
    if (setsockopt(handle, level, option, reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
        ec = last_socket_error();
    } else {
        ec.clear();
    }
}

void set_socket_bool_option(socket_handle handle, int level, int option, bool enabled) {
    std::error_code ec;
    set_socket_bool_option(handle, level, option, enabled, ec);
    if (ec) {
        throw socket_exception("setsockopt", ec);
    }
}

bool get_socket_bool_option(socket_handle handle, int level, int option, std::error_code& ec) noexcept {
    return get_socket_int_option(handle, level, option, ec) != 0;
}

bool get_socket_bool_option(socket_handle handle, int level, int option) {
    std::error_code ec;
    bool res = get_socket_bool_option(handle, level, option, ec);
    if (ec) {
        throw socket_exception("getsockopt", ec);
    }
    return res;
}

void set_socket_int_option(socket_handle handle, int level, int option, int value, std::error_code& ec) noexcept {
    if (setsockopt(handle, level, option, reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
        ec = last_socket_error();
    } else {
        ec.clear();
    }
}

void set_socket_int_option(socket_handle handle, int level, int option, int value) {
    std::error_code ec;
    set_socket_int_option(handle, level, option, value, ec);
    if (ec) {
        throw socket_exception("setsockopt", ec);
    }
}

int get_socket_int_option(socket_handle handle, int level, int option, std::error_code& ec) noexcept {
    int value{};
    socklen_t length = sizeof(value);
    if (getsockopt(handle, level, option, reinterpret_cast<char*>(&value), &length) != 0) {
        ec = last_socket_error();
        return 0;
    }
    ec.clear();
    return value;
}

int get_socket_int_option(socket_handle handle, int level, int option) {
    std::error_code ec;
    int res = get_socket_int_option(handle, level, option, ec);
    if (ec) {
        throw socket_exception("getsockopt", ec);
    }
    return res;
}

void set_reuse_port(socket_handle handle, bool enabled, std::error_code& ec) noexcept {
#if defined(SO_REUSEPORT)
    set_socket_bool_option(handle, SOL_SOCKET, SO_REUSEPORT, enabled, ec);
#else
    (void)handle;
    (void)enabled;
    ec = std::make_error_code(std::errc::function_not_supported);
#endif
}

void set_reuse_port(socket_handle handle, bool enabled) {
    std::error_code ec;
    set_reuse_port(handle, enabled, ec);
    if (ec) throw socket_exception("setsockopt", ec);
}

void set_keepalive(socket_handle handle, bool enabled, std::error_code& ec) noexcept {
    set_socket_bool_option(handle, SOL_SOCKET, SO_KEEPALIVE, enabled, ec);
}

void set_keepalive(socket_handle handle, bool enabled) {
    std::error_code ec;
    set_keepalive(handle, enabled, ec);
    if (ec) throw socket_exception("setsockopt", ec);
}

void set_tcp_fastopen(socket_handle handle, int backlog, std::error_code& ec) noexcept {
#if defined(TCP_FASTOPEN)
    set_socket_int_option(handle, IPPROTO_TCP, TCP_FASTOPEN, backlog, ec);
#else
    (void)handle;
    (void)backlog;
    ec = std::make_error_code(std::errc::function_not_supported);
#endif
}

void set_tcp_fastopen(socket_handle handle, int backlog) {
    std::error_code ec;
    set_tcp_fastopen(handle, backlog, ec);
    if (ec) throw socket_exception("setsockopt", ec);
}

void set_tcp_fastopen_connect(socket_handle handle, bool enabled, std::error_code& ec) noexcept {
#if defined(TCP_FASTOPEN_CONNECT)
    set_socket_bool_option(handle, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, enabled, ec);
#elif defined(TCP_FASTOPEN) && defined(_WIN32)
    set_socket_bool_option(handle, IPPROTO_TCP, TCP_FASTOPEN, enabled, ec);
#elif defined(TCP_FASTOPEN) && defined(__APPLE__)
    set_socket_int_option(handle, IPPROTO_TCP, TCP_FASTOPEN, enabled ? 1 : 0, ec);
#else
    (void)handle;
    (void)enabled;
    ec = std::make_error_code(std::errc::function_not_supported);
#endif
}

void set_tcp_fastopen_connect(socket_handle handle, bool enabled) {
    std::error_code ec;
    set_tcp_fastopen_connect(handle, enabled, ec);
    if (ec) throw socket_exception("setsockopt", ec);
}

void set_dont_fragment(socket_handle handle, bool enabled, std::error_code& ec) noexcept {
#if defined(__linux__)
    int val = enabled ? IP_PMTUDISC_DO : IP_PMTUDISC_WANT;
    set_socket_int_option(handle, IPPROTO_IP, IP_MTU_DISCOVER, val, ec);
    if (ec) {
        ec.clear();
        set_socket_int_option(handle, IPPROTO_IPV6, IPV6_MTU_DISCOVER, val, ec);
    }
#elif defined(_WIN32)
    set_socket_bool_option(handle, IPPROTO_IP, IP_DONTFRAGMENT, enabled, ec);
    if (ec) {
        ec.clear();
        set_socket_bool_option(handle, IPPROTO_IPV6, IPV6_DONTFRAG, enabled, ec);
    }
#elif defined(__APPLE__)
    set_socket_int_option(handle, IPPROTO_IP, IP_DONTFRAG, enabled ? 1 : 0, ec);
    if (ec) {
        ec.clear();
        set_socket_int_option(handle, IPPROTO_IPV6, IPV6_DONTFRAG, enabled ? 1 : 0, ec);
    }
#else
    (void)handle;
    (void)enabled;
    ec = std::make_error_code(std::errc::function_not_supported);
#endif
}

void set_dont_fragment(socket_handle handle, bool enabled) {
    std::error_code ec;
    set_dont_fragment(handle, enabled, ec);
    if (ec) throw socket_exception("setsockopt", ec);
}

std::uint32_t get_mtu(socket_handle handle, std::error_code& ec) noexcept {
#if defined(__linux__) || defined(_WIN32)
    int mtu = get_socket_int_option(handle, IPPROTO_IP, IP_MTU, ec);
    if (ec) {
        ec.clear();
        mtu = get_socket_int_option(handle, IPPROTO_IPV6, IPV6_MTU, ec);
    }
    return ec ? 0 : static_cast<std::uint32_t>(mtu);
#else
    (void)handle;
    ec = std::make_error_code(std::errc::function_not_supported);
    return 0;
#endif
}

std::uint32_t get_mtu(socket_handle handle) {
    std::error_code ec;
    auto res = get_mtu(handle, ec);
    if (ec) throw socket_exception("getsockopt", ec);
    return res;
}

std::optional<std::error_code> get_socket_error(socket_handle handle, std::error_code& ec) noexcept {
    const int error = get_socket_int_option(handle, SOL_SOCKET, SO_ERROR, ec);
    if (ec) {
        return std::nullopt;
    }
    if (error == 0) {
        return std::nullopt;
    }
    return socket_error_from_code(error);
}

std::optional<std::error_code> get_socket_error(socket_handle handle) {
    std::error_code ec;
    auto res = get_socket_error(handle, ec);
    if (ec) {
        throw socket_exception("getsockopt", ec);
    }
    return res;
}

ResolvedAddress resolve_endpoint(const net::SocketAddr& endpoint, int socket_type, int protocol, std::error_code& ec) noexcept {
    ensure_socket_runtime();

    ResolvedAddress address{};
    address.type = socket_type;
    address.protocol = protocol;

    if (endpoint.is_v4()) {
        const auto* v4 = endpoint.as_v4();
        address.family = AF_INET;
        address.length = sizeof(sockaddr_in);
        auto* in = reinterpret_cast<sockaddr_in*>(&address.storage);
        in->sin_family = AF_INET;
        in->sin_port = htons(v4->port());
        std::memcpy(&in->sin_addr, v4->ip().octets().data(), 4);
    } else {
        const auto* v6 = endpoint.as_v6();
        address.family = AF_INET6;
        address.length = sizeof(sockaddr_in6);
        auto* in6 = reinterpret_cast<sockaddr_in6*>(&address.storage);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(v6->port());
        in6->sin6_flowinfo = htonl(v6->flowinfo());
        in6->sin6_scope_id = v6->scope_id();
        std::memcpy(&in6->sin6_addr, v6->ip().octets().data(), 16);
    }

    ec.clear();
    return address;
}

ResolvedAddress resolve_endpoint(const net::SocketAddr& endpoint, int socket_type, int protocol) {
    std::error_code ec;
    auto res = resolve_endpoint(endpoint, socket_type, protocol, ec);
    if (ec) {
        throw std::system_error(ec, "resolve endpoint");
    }
    return res;
}

net::SocketAddr endpoint_from_sockaddr(const sockaddr* address, socklen_t length, std::error_code& ec) noexcept {
    (void)length;
    ec.clear();

    if (address->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(address);
        std::array<std::uint8_t, 4> octets;
        std::memcpy(octets.data(), &in->sin_addr, 4);
        return net::SocketAddrV4{net::Ipv4Addr{octets}, ntohs(in->sin_port)};
    } else if (address->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(address);
        std::array<std::uint8_t, 16> octets;
        std::memcpy(octets.data(), &in6->sin6_addr, 16);
        return net::SocketAddrV6{net::Ipv6Addr{octets}, ntohs(in6->sin6_port), ntohl(in6->sin6_flowinfo), in6->sin6_scope_id};
    } else {
        ec = std::make_error_code(std::errc::address_family_not_supported);
        return net::SocketAddrV4{net::Ipv4Addr{0,0,0,0}, 0};
    }
}

net::SocketAddr endpoint_from_sockaddr(const sockaddr* address, socklen_t length) {
    std::error_code ec;
    auto res = endpoint_from_sockaddr(address, length, ec);
    if (ec) {
        throw std::system_error(ec, "unsupported socket address family");
    }
    return res;
}

net::SocketAddr local_endpoint(socket_handle handle, std::error_code& ec) noexcept {
    sockaddr_storage storage{};
    socklen_t length = sizeof(storage);
    if (getsockname(handle, reinterpret_cast<sockaddr*>(&storage), &length) != 0) {
        ec = last_socket_error();
        return net::SocketAddrV4{net::Ipv4Addr{0,0,0,0}, 0};
    }
    return endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&storage), length, ec);
}

net::SocketAddr local_endpoint(socket_handle handle) {
    std::error_code ec;
    auto res = local_endpoint(handle, ec);
    if (ec) {
        throw socket_exception("getsockname", ec);
    }
    return res;
}

net::SocketAddr peer_endpoint(socket_handle handle, std::error_code& ec) noexcept {
    sockaddr_storage storage{};
    socklen_t length = sizeof(storage);
    if (getpeername(handle, reinterpret_cast<sockaddr*>(&storage), &length) != 0) {
        ec = last_socket_error();
        return net::SocketAddrV4{net::Ipv4Addr{0,0,0,0}, 0};
    }
    return endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&storage), length, ec);
}

net::SocketAddr peer_endpoint(socket_handle handle) {
    std::error_code ec;
    auto res = peer_endpoint(handle, ec);
    if (ec) {
        throw socket_exception("getpeername", ec);
    }
    return res;
}

std::system_error socket_exception(const char* operation, std::error_code error) {
    return std::system_error(error, operation);
}

std::system_error socket_exception(const char* operation) {
    return socket_exception(operation, last_socket_error());
}

Socket::Socket(socket_handle handle) noexcept : handle_(handle) {}

Socket::Socket(Socket&& other) noexcept : handle_(other.release()) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

Socket::~Socket() {
    reset();
}

socket_handle Socket::release() noexcept {
    const auto handle = handle_;
    handle_ = invalid_socket_handle;
    return handle;
}

void Socket::reset(socket_handle handle) noexcept {
    if (handle_ != handle) {
        close_socket(handle_);
        handle_ = handle;
    }
}

} // namespace ioxx::detail

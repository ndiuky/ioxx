#pragma once

#include <ioxx/net.hpp>

#include <cstddef>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace ioxx::detail {

#if defined(_WIN32)
using socket_handle = SOCKET;
inline constexpr socket_handle invalid_socket_handle = INVALID_SOCKET;
#else
using socket_handle = int;
inline constexpr socket_handle invalid_socket_handle = -1;
#endif

struct ResolvedAddress {
    sockaddr_storage storage{};
    socklen_t length{};
    int family{};
    int type{};
    int protocol{};
};

void ensure_socket_runtime();
[[nodiscard]] std::error_code last_socket_error() noexcept;
[[nodiscard]] std::error_code socket_error_from_code(int code) noexcept;
[[nodiscard]] bool is_would_block_error(const std::error_code& error) noexcept;
[[nodiscard]] bool is_connect_in_progress(const std::error_code& error) noexcept;
[[nodiscard]] native_handle_type to_native(socket_handle handle) noexcept;
[[nodiscard]] socket_handle from_native(native_handle_type handle) noexcept;
[[nodiscard]] bool socket_is_valid(socket_handle handle) noexcept;
void close_socket(socket_handle handle) noexcept;
void set_non_blocking(socket_handle handle, bool enabled, std::error_code& ec) noexcept;
void set_non_blocking(socket_handle handle, bool enabled = true);
void set_close_on_exec(socket_handle handle, std::error_code& ec) noexcept;
void set_close_on_exec(socket_handle handle);
void set_reuse_addr(socket_handle handle, bool enabled, std::error_code& ec) noexcept;
void set_reuse_addr(socket_handle handle, bool enabled = true);
void set_socket_bool_option(socket_handle handle, int level, int option, bool enabled, std::error_code& ec) noexcept;
void set_socket_bool_option(socket_handle handle, int level, int option, bool enabled);
[[nodiscard]] bool get_socket_bool_option(socket_handle handle, int level, int option, std::error_code& ec) noexcept;
[[nodiscard]] bool get_socket_bool_option(socket_handle handle, int level, int option);
void set_socket_int_option(socket_handle handle, int level, int option, int value, std::error_code& ec) noexcept;
void set_socket_int_option(socket_handle handle, int level, int option, int value);
[[nodiscard]] int get_socket_int_option(socket_handle handle, int level, int option, std::error_code& ec) noexcept;
[[nodiscard]] int get_socket_int_option(socket_handle handle, int level, int option);
void set_reuse_port(socket_handle handle, bool enabled, std::error_code& ec) noexcept;
void set_reuse_port(socket_handle handle, bool enabled = true);
void set_keepalive(socket_handle handle, bool enabled, std::error_code& ec) noexcept;
void set_keepalive(socket_handle handle, bool enabled = true);
void set_tcp_fastopen(socket_handle handle, int backlog, std::error_code& ec) noexcept;
void set_tcp_fastopen(socket_handle handle, int backlog);
void set_tcp_fastopen_connect(socket_handle handle, bool enabled, std::error_code& ec) noexcept;
void set_tcp_fastopen_connect(socket_handle handle, bool enabled);
void set_dont_fragment(socket_handle handle, bool enabled, std::error_code& ec) noexcept;
void set_dont_fragment(socket_handle handle, bool enabled);
[[nodiscard]] std::uint32_t get_mtu(socket_handle handle, std::error_code& ec) noexcept;
[[nodiscard]] std::uint32_t get_mtu(socket_handle handle);
[[nodiscard]] std::optional<std::error_code> get_socket_error(socket_handle handle, std::error_code& ec) noexcept;
[[nodiscard]] std::optional<std::error_code> get_socket_error(socket_handle handle);
[[nodiscard]] ResolvedAddress resolve_endpoint(const net::SocketAddr& endpoint, int socket_type, int protocol, std::error_code& ec) noexcept;
[[nodiscard]] ResolvedAddress resolve_endpoint(const net::SocketAddr& endpoint, int socket_type, int protocol);
[[nodiscard]] net::SocketAddr endpoint_from_sockaddr(const sockaddr* address, socklen_t length, std::error_code& ec) noexcept;
[[nodiscard]] net::SocketAddr endpoint_from_sockaddr(const sockaddr* address, socklen_t length);
[[nodiscard]] net::SocketAddr local_endpoint(socket_handle handle, std::error_code& ec) noexcept;
[[nodiscard]] net::SocketAddr local_endpoint(socket_handle handle);
[[nodiscard]] net::SocketAddr peer_endpoint(socket_handle handle, std::error_code& ec) noexcept;
[[nodiscard]] net::SocketAddr peer_endpoint(socket_handle handle);
[[nodiscard]] std::system_error socket_exception(const char* operation, std::error_code error);
[[nodiscard]] std::system_error socket_exception(const char* operation);

class Socket {
public:
    Socket() noexcept = default;
    explicit Socket(socket_handle handle) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    ~Socket();

    [[nodiscard]] socket_handle get() const noexcept { return handle_; }
    [[nodiscard]] native_handle_type native() const noexcept { return to_native(handle_); }
    [[nodiscard]] bool valid() const noexcept { return socket_is_valid(handle_); }
    [[nodiscard]] socket_handle release() noexcept;
    void reset(socket_handle handle = invalid_socket_handle) noexcept;

private:
    socket_handle handle_{invalid_socket_handle};
};

} // namespace ioxx::detail

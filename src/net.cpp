#include <ioxx/net.hpp>

#include "socket.hpp"

#include <cstring>
#include <system_error>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#endif

namespace ioxx::net {

namespace {

std::unique_ptr<detail::Socket> make_socket(const detail::ResolvedAddress& address, std::error_code& ec) noexcept {
    detail::ensure_socket_runtime();
    std::unique_ptr<detail::Socket> socket(new (std::nothrow) detail::Socket(
        ::socket(address.family, address.type, address.protocol)));
    if (!socket) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return nullptr;
    }
    if (!socket->valid()) {
        ec = detail::last_socket_error();
        return nullptr;
    }
    detail::set_close_on_exec(socket->get(), ec);
    if (ec) return nullptr;
    detail::set_non_blocking(socket->get(), true, ec);
    if (ec) return nullptr;
    return socket;
}

std::unique_ptr<detail::Socket> make_socket(const detail::ResolvedAddress& address) {
    std::error_code ec;
    auto socket = make_socket(address, ec);
    if (ec) throw detail::socket_exception("socket", ec);
    return socket;
}

std::system_error no_address_error(const char* operation) {
    return std::system_error(std::make_error_code(std::errc::address_not_available), operation);
}

template <typename Action>
std::unique_ptr<detail::Socket> try_address(const SocketAddr& endpoint, int socket_type,
                                            int protocol, std::error_code& ec, Action action) noexcept {
    auto address = detail::resolve_endpoint(endpoint, socket_type, protocol, ec);
    if (ec) return nullptr;
    auto socket = make_socket(address, ec);
    if (ec) return nullptr;
    action(*socket, address, ec);
    if (ec) return nullptr;
    return socket;
}

template <typename Action>
std::unique_ptr<detail::Socket> try_address(const SocketAddr& endpoint, int socket_type,
                                            int protocol, Action action) {
    auto address = detail::resolve_endpoint(endpoint, socket_type, protocol);
    auto socket = make_socket(address);
    action(*socket, address);
    return socket;
}

void require_socket(const std::unique_ptr<detail::Socket>& socket, std::error_code& ec) noexcept {
    if (!socket || !socket->valid()) {
        ec = std::make_error_code(std::errc::bad_file_descriptor);
    } else {
        ec.clear();
    }
}

void require_socket(const std::unique_ptr<detail::Socket>& socket) {
    std::error_code ec;
    require_socket(socket, ec);
    if (ec) {
        throw std::system_error(ec, "invalid socket");
    }
}

} // namespace

TcpSocket::TcpSocket(std::unique_ptr<detail::Socket> socket) : socket_(std::move(socket)) {}

TcpSocket::~TcpSocket() = default;
TcpSocket::TcpSocket(TcpSocket&&) noexcept = default;
TcpSocket& TcpSocket::operator=(TcpSocket&&) noexcept = default;

TcpSocket TcpSocket::new_v4(std::error_code& ec) noexcept {
    detail::ensure_socket_runtime();
    std::unique_ptr<detail::Socket> socket(new (std::nothrow) detail::Socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)));
    if (!socket || !socket->valid()) {
        ec = detail::last_socket_error();
        return TcpSocket{nullptr};
    }
    detail::set_close_on_exec(socket->get(), ec);
    if (ec) return TcpSocket{nullptr};
    detail::set_non_blocking(socket->get(), true, ec);
    if (ec) return TcpSocket{nullptr};
    return TcpSocket{std::move(socket)};
}

TcpSocket TcpSocket::new_v4() {
    std::error_code ec;
    auto res = new_v4(ec);
    if (ec) throw detail::socket_exception("socket", ec);
    return res;
}

TcpSocket TcpSocket::new_v6(std::error_code& ec) noexcept {
    detail::ensure_socket_runtime();
    std::unique_ptr<detail::Socket> socket(new (std::nothrow) detail::Socket(::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)));
    if (!socket || !socket->valid()) {
        ec = detail::last_socket_error();
        return TcpSocket{nullptr};
    }
    detail::set_close_on_exec(socket->get(), ec);
    if (ec) return TcpSocket{nullptr};
    detail::set_non_blocking(socket->get(), true, ec);
    if (ec) return TcpSocket{nullptr};
    return TcpSocket{std::move(socket)};
}

TcpSocket TcpSocket::new_v6() {
    std::error_code ec;
    auto res = new_v6(ec);
    if (ec) throw detail::socket_exception("socket", ec);
    return res;
}

void TcpSocket::set_reuseaddr(bool reuse, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_reuse_addr(socket_->get(), reuse, ec);
}

void TcpSocket::set_reuseaddr(bool reuse) const {
    std::error_code ec;
    set_reuseaddr(reuse, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

bool TcpSocket::reuseaddr(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return false;
    return detail::get_socket_bool_option(socket_->get(), SOL_SOCKET, SO_REUSEADDR, ec);
}

bool TcpSocket::reuseaddr() const {
    std::error_code ec;
    auto res = reuseaddr(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

void TcpSocket::set_reuseport(bool reuse, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_reuse_port(socket_->get(), reuse, ec);
}

void TcpSocket::set_reuseport(bool reuse) const {
    std::error_code ec;
    set_reuseport(reuse, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

bool TcpSocket::reuseport(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return false;
#if defined(SO_REUSEPORT)
    return detail::get_socket_bool_option(socket_->get(), SOL_SOCKET, SO_REUSEPORT, ec);
#else
    ec = std::make_error_code(std::errc::function_not_supported);
    return false;
#endif
}

bool TcpSocket::reuseport() const {
    std::error_code ec;
    auto res = reuseport(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

void TcpSocket::set_keepalive(bool keepalive, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_keepalive(socket_->get(), keepalive, ec);
}

void TcpSocket::set_keepalive(bool keepalive) const {
    std::error_code ec;
    set_keepalive(keepalive, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

bool TcpSocket::keepalive(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return false;
    return detail::get_socket_bool_option(socket_->get(), SOL_SOCKET, SO_KEEPALIVE, ec);
}

bool TcpSocket::keepalive() const {
    std::error_code ec;
    auto res = keepalive(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

void TcpSocket::set_nodelay(bool nodelay, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_socket_bool_option(socket_->get(), IPPROTO_TCP, TCP_NODELAY, nodelay, ec);
}

void TcpSocket::set_nodelay(bool nodelay) const {
    std::error_code ec;
    set_nodelay(nodelay, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

bool TcpSocket::nodelay(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return false;
    return detail::get_socket_bool_option(socket_->get(), IPPROTO_TCP, TCP_NODELAY, ec);
}

bool TcpSocket::nodelay() const {
    std::error_code ec;
    auto res = nodelay(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

void TcpSocket::set_tcp_fastopen(int backlog, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_tcp_fastopen(socket_->get(), backlog, ec);
}

void TcpSocket::set_tcp_fastopen(int backlog) const {
    std::error_code ec;
    set_tcp_fastopen(backlog, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

void TcpSocket::set_tcp_fastopen_connect(bool enabled, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_tcp_fastopen_connect(socket_->get(), enabled, ec);
}

void TcpSocket::set_tcp_fastopen_connect(bool enabled) const {
    std::error_code ec;
    set_tcp_fastopen_connect(enabled, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

TcpListener TcpSocket::bind(const SocketAddr& endpoint, int backlog, std::error_code& ec) noexcept {
    require_socket(socket_, ec);
    if (ec) return TcpListener{};
    auto address = detail::resolve_endpoint(endpoint, SOCK_STREAM, IPPROTO_TCP, ec);
    if (ec) return TcpListener{};
    if (::bind(socket_->get(), reinterpret_cast<const sockaddr*>(&address.storage), address.length) != 0) {
        ec = detail::last_socket_error();
        return TcpListener{};
    }
    if (::listen(socket_->get(), backlog) != 0) {
        ec = detail::last_socket_error();
        return TcpListener{};
    }
    ec.clear();
    return TcpListener{std::move(socket_)};
}

TcpListener TcpSocket::bind(const SocketAddr& endpoint, int backlog) {
    std::error_code ec;
    auto res = bind(endpoint, backlog, ec);
    if (ec) throw detail::socket_exception("bind/listen", ec);
    return res;
}

TcpStream TcpSocket::connect(const SocketAddr& endpoint, std::error_code& ec) noexcept {
    require_socket(socket_, ec);
    if (ec) return TcpStream{};
    auto address = detail::resolve_endpoint(endpoint, SOCK_STREAM, IPPROTO_TCP, ec);
    if (ec) return TcpStream{};
    const int result = ::connect(socket_->get(), reinterpret_cast<const sockaddr*>(&address.storage), address.length);
    if (result == 0) {
        ec.clear();
    } else {
        const auto error = detail::last_socket_error();
        if (!detail::is_connect_in_progress(error)) {
            ec = error;
            return TcpStream{};
        } else {
            ec.clear();
        }
    }
    return TcpStream{std::move(socket_)};
}

TcpStream TcpSocket::connect(const SocketAddr& endpoint) {
    std::error_code ec;
    auto res = connect(endpoint, ec);
    if (ec) throw detail::socket_exception("connect", ec);
    return res;
}

TcpStream::TcpStream() = default;

TcpStream::TcpStream(native_handle_type handle)
    : socket_(std::make_unique<detail::Socket>(detail::from_native(handle))) {
    require_socket(socket_);
    detail::set_non_blocking(socket_->get());
}

TcpStream::TcpStream(std::unique_ptr<detail::Socket> socket) : socket_(std::move(socket)) {}

TcpStream TcpStream::connect(const SocketAddr& endpoint, std::error_code& ec) noexcept {
    auto socket = try_address(endpoint, SOCK_STREAM, IPPROTO_TCP, ec,
                                [](detail::Socket& socket, const detail::ResolvedAddress& address, std::error_code& ec) {
        const int result = ::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address.storage),
                                     address.length);
        if (result == 0) {
            ec.clear();
            return;
        }

        const auto error = detail::last_socket_error();
        if (!detail::is_connect_in_progress(error)) {
            ec = error;
        } else {
            ec.clear();
        }
    });
    return TcpStream{std::move(socket)};
}

TcpStream TcpStream::connect(const SocketAddr& endpoint) {
    std::error_code ec;
    auto res = connect(endpoint, ec);
    if (ec) throw detail::socket_exception("connect", ec);
    return res;
}

TcpStream::TcpStream(TcpStream&& other) noexcept = default;
TcpStream& TcpStream::operator=(TcpStream&& other) noexcept = default;
TcpStream::~TcpStream() = default;

native_handle_type TcpStream::native_handle() const noexcept {
    return socket_ ? socket_->native() : invalid_native_handle;
}

void TcpStream::on_registered(const Registry& registry, Token token, Interest interests) {
    registry_ = registry;
    token_ = token;
    interests_ = interests;
}

void TcpStream::on_reregistered(const Registry& registry, Token token, Interest interests) {
    on_registered(registry, token, interests);
}

void TcpStream::on_deregistered(const Registry& registry) {
    (void)registry;
    registry_.reset();
    interests_ = Interest{};
}

void TcpStream::rearm_after_would_block() const {
    if (registry_.has_value() && !interests_.empty()) {
        registry_->reregister_handle(native_handle(), source_kind(), token_, interests_);
    }
}

SocketAddr TcpStream::local_endpoint(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return SocketAddr{};
    return detail::local_endpoint(socket_->get(), ec);
}

SocketAddr TcpStream::local_endpoint() const {
    std::error_code ec;
    auto res = local_endpoint(ec);
    if (ec) throw detail::socket_exception("local_endpoint", ec);
    return res;
}

SocketAddr TcpStream::peer_endpoint(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return SocketAddr{};
    return detail::peer_endpoint(socket_->get(), ec);
}

SocketAddr TcpStream::peer_endpoint() const {
    std::error_code ec;
    auto res = peer_endpoint(ec);
    if (ec) throw detail::socket_exception("peer_endpoint", ec);
    return res;
}

std::size_t TcpStream::read(void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
#if defined(_WIN32)
    const int result = ::recv(socket_->get(), static_cast<char*>(data), static_cast<int>(size), 0);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#else
    const auto result = ::recv(socket_->get(), data, size, 0);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#endif
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t TcpStream::read(void* data, std::size_t size) const {
    std::error_code ec;
    auto res = read(data, size, ec);
    if (ec) throw detail::socket_exception("recv", ec);
    return res;
}

std::size_t TcpStream::read(std::span<std::byte> buffer, std::error_code& ec) const noexcept {
    return read(buffer.data(), buffer.size(), ec);
}

std::size_t TcpStream::read(std::span<std::byte> buffer) const {
    return read(buffer.data(), buffer.size());
}

std::size_t TcpStream::read_vectored(std::span<IoSliceMut> bufs, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
#if defined(_WIN32)
    DWORD bytes_received = 0;
    DWORD flags = 0;
    const int result = ::WSARecv(socket_->get(), reinterpret_cast<LPWSABUF>(bufs.data()), static_cast<DWORD>(bufs.size()), &bytes_received, &flags, nullptr, nullptr);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(bytes_received);
#else
    const auto result = ::readv(socket_->get(), reinterpret_cast<const iovec*>(bufs.data()), static_cast<int>(bufs.size()));
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
#endif
}

std::size_t TcpStream::read_vectored(std::span<IoSliceMut> bufs) const {
    std::error_code ec;
    auto res = read_vectored(bufs, ec);
    if (ec) throw detail::socket_exception("read_vectored", ec);
    return res;
}

std::size_t TcpStream::write(const void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
#if defined(_WIN32)
    const int result = ::send(socket_->get(), static_cast<const char*>(data), static_cast<int>(size), 0);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#else
    const auto result = ::send(socket_->get(), data, size, 0);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#endif
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t TcpStream::write(const void* data, std::size_t size) const {
    std::error_code ec;
    auto res = write(data, size, ec);
    if (ec) throw detail::socket_exception("send", ec);
    return res;
}

std::size_t TcpStream::write(std::span<const std::byte> buffer, std::error_code& ec) const noexcept {
    return write(buffer.data(), buffer.size(), ec);
}

std::size_t TcpStream::write(std::span<const std::byte> buffer) const {
    return write(buffer.data(), buffer.size());
}

std::size_t TcpStream::write(std::string_view text, std::error_code& ec) const noexcept {
    return write(text.data(), text.size(), ec);
}

std::size_t TcpStream::write(std::string_view text) const {
    return write(text.data(), text.size());
}

std::size_t TcpStream::write_vectored(std::span<IoSlice> bufs, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
#if defined(_WIN32)
    DWORD bytes_sent = 0;
    const int result = ::WSASend(socket_->get(), reinterpret_cast<LPWSABUF>(const_cast<IoSlice*>(bufs.data())), static_cast<DWORD>(bufs.size()), &bytes_sent, 0, nullptr, nullptr);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(bytes_sent);
#else
    const auto result = ::writev(socket_->get(), reinterpret_cast<const iovec*>(bufs.data()), static_cast<int>(bufs.size()));
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
#endif
}

std::size_t TcpStream::write_vectored(std::span<IoSlice> bufs) const {
    std::error_code ec;
    auto res = write_vectored(bufs, ec);
    if (ec) throw detail::socket_exception("write_vectored", ec);
    return res;
}

void TcpStream::shutdown_read(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
#if defined(_WIN32)
    if (::shutdown(socket_->get(), SD_RECEIVE) != 0) {
#else
    if (::shutdown(socket_->get(), SHUT_RD) != 0) {
#endif
        ec = detail::last_socket_error();
    } else {
        ec.clear();
    }
}

void TcpStream::shutdown_read() const {
    std::error_code ec;
    shutdown_read(ec);
    if (ec) throw detail::socket_exception("shutdown(read)", ec);
}

void TcpStream::shutdown_write(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
#if defined(_WIN32)
    if (::shutdown(socket_->get(), SD_SEND) != 0) {
#else
    if (::shutdown(socket_->get(), SHUT_WR) != 0) {
#endif
        ec = detail::last_socket_error();
    } else {
        ec.clear();
    }
}

void TcpStream::shutdown_write() const {
    std::error_code ec;
    shutdown_write(ec);
    if (ec) throw detail::socket_exception("shutdown(write)", ec);
}

void TcpStream::shutdown_both(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
#if defined(_WIN32)
    if (::shutdown(socket_->get(), SD_BOTH) != 0) {
#else
    if (::shutdown(socket_->get(), SHUT_RDWR) != 0) {
#endif
        ec = detail::last_socket_error();
    } else {
        ec.clear();
    }
}

void TcpStream::shutdown_both() const {
    std::error_code ec;
    shutdown_both(ec);
    if (ec) throw detail::socket_exception("shutdown(both)", ec);
}

void TcpStream::set_nodelay(bool enabled, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_socket_bool_option(socket_->get(), IPPROTO_TCP, TCP_NODELAY, enabled, ec);
}

void TcpStream::set_nodelay(bool enabled) const {
    std::error_code ec;
    set_nodelay(enabled, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

bool TcpStream::nodelay(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return false;
    return detail::get_socket_bool_option(socket_->get(), IPPROTO_TCP, TCP_NODELAY, ec);
}

bool TcpStream::nodelay() const {
    std::error_code ec;
    auto res = nodelay(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

void TcpStream::set_ttl(std::uint32_t ttl, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_socket_int_option(socket_->get(), IPPROTO_IP, IP_TTL, static_cast<int>(ttl), ec);
}

void TcpStream::set_ttl(std::uint32_t ttl) const {
    std::error_code ec;
    set_ttl(ttl, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

std::uint32_t TcpStream::ttl(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
    return static_cast<std::uint32_t>(detail::get_socket_int_option(socket_->get(), IPPROTO_IP, IP_TTL, ec));
}

std::uint32_t TcpStream::ttl() const {
    std::error_code ec;
    auto res = ttl(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

std::optional<std::error_code> TcpStream::take_error() const {
    require_socket(socket_);
    return detail::get_socket_error(socket_->get());
}

bool TcpStream::valid() const noexcept {
    return socket_ && socket_->valid();
}

TcpListener::TcpListener() = default;

TcpListener::TcpListener(native_handle_type handle)
    : socket_(std::make_unique<detail::Socket>(detail::from_native(handle))) {
    require_socket(socket_);
    detail::set_non_blocking(socket_->get());
}

TcpListener::TcpListener(std::unique_ptr<detail::Socket> socket) : socket_(std::move(socket)) {}

TcpListener TcpListener::bind(const SocketAddr& endpoint, int backlog, std::error_code& ec) noexcept {
    auto socket = TcpSocket::new_v4(ec);
    if (ec) return TcpListener{};
    socket.set_reuseaddr(true, ec);
    if (ec) return TcpListener{};
    return socket.bind(endpoint, backlog, ec);
}

TcpListener TcpListener::bind(const SocketAddr& endpoint, std::error_code& ec) noexcept {
    return bind(endpoint, 128, ec);
}

TcpListener TcpListener::bind(const SocketAddr& endpoint, int backlog) {
    std::error_code ec;
    auto res = bind(endpoint, backlog, ec);
    if (ec) throw detail::socket_exception("bind", ec);
    return res;
}

TcpListener::TcpListener(TcpListener&& other) noexcept = default;
TcpListener& TcpListener::operator=(TcpListener&& other) noexcept = default;
TcpListener::~TcpListener() = default;

native_handle_type TcpListener::native_handle() const noexcept {
    return socket_ ? socket_->native() : invalid_native_handle;
}

SourceKind TcpListener::source_kind() const noexcept {
    return SourceKind::socket;
}

void TcpListener::on_registered(const Registry& registry, Token token, Interest interests) {
    registry_ = registry;
    token_ = token;
    interests_ = interests;
}

void TcpListener::on_reregistered(const Registry& registry, Token token, Interest interests) {
    on_registered(registry, token, interests);
}

void TcpListener::on_deregistered(const Registry& registry) {
    (void)registry;
    registry_.reset();
    interests_ = Interest{};
}

void TcpListener::rearm_after_would_block() const {
    if (registry_.has_value() && !interests_.empty()) {
        registry_->reregister_handle(native_handle(), source_kind(), token_, interests_);
    }
}

SocketAddr TcpListener::local_endpoint(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return SocketAddr{};
    return detail::local_endpoint(socket_->get(), ec);
}

SocketAddr TcpListener::local_endpoint() const {
    std::error_code ec;
    auto res = local_endpoint(ec);
    if (ec) throw detail::socket_exception("local_endpoint", ec);
    return res;
}

std::pair<TcpStream, SocketAddr> TcpListener::accept(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return {TcpStream{}, SocketAddr{}};
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    const auto accepted = ::accept(socket_->get(), reinterpret_cast<sockaddr*>(&address), &length);
    if (!detail::socket_is_valid(accepted)) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return {TcpStream{}, SocketAddr{}};
    }

    std::unique_ptr<detail::Socket> stream_socket(new (std::nothrow) detail::Socket(accepted));
    if (!stream_socket) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        detail::close_socket(accepted);
        return {TcpStream{}, SocketAddr{}};
    }
    
    detail::set_close_on_exec(stream_socket->get(), ec);
    if (ec) return {TcpStream{}, SocketAddr{}};
    detail::set_non_blocking(stream_socket->get(), true, ec);
    if (ec) return {TcpStream{}, SocketAddr{}};

    auto endpoint = detail::endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&address), length, ec);
    if (ec) return {TcpStream{}, SocketAddr{}};
    
    ec.clear();
    return {TcpStream{std::move(stream_socket)}, endpoint};
}

std::pair<TcpStream, SocketAddr> TcpListener::accept() const {
    std::error_code ec;
    auto res = accept(ec);
    if (ec) throw detail::socket_exception("accept", ec);
    return res;
}

void TcpListener::set_ttl(std::uint32_t ttl, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_socket_int_option(socket_->get(), IPPROTO_IP, IP_TTL, static_cast<int>(ttl), ec);
}

void TcpListener::set_ttl(std::uint32_t ttl) const {
    std::error_code ec;
    set_ttl(ttl, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

std::uint32_t TcpListener::ttl(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
    return static_cast<std::uint32_t>(detail::get_socket_int_option(socket_->get(), IPPROTO_IP, IP_TTL, ec));
}

std::uint32_t TcpListener::ttl() const {
    std::error_code ec;
    auto res = ttl(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

std::optional<std::error_code> TcpListener::take_error() const {
    require_socket(socket_);
    return detail::get_socket_error(socket_->get());
}

bool TcpListener::valid() const noexcept {
    return socket_ && socket_->valid();
}

UdpSocket::UdpSocket() = default;

UdpSocket::UdpSocket(native_handle_type handle)
    : socket_(std::make_unique<detail::Socket>(detail::from_native(handle))) {
    require_socket(socket_);
    detail::set_non_blocking(socket_->get());
}

UdpSocket::UdpSocket(std::unique_ptr<detail::Socket> socket) : socket_(std::move(socket)) {}

UdpSocket UdpSocket::bind(const SocketAddr& endpoint, std::error_code& ec) noexcept {
    auto socket = try_address(endpoint, SOCK_DGRAM, IPPROTO_UDP, ec,
                                [](detail::Socket& socket, const detail::ResolvedAddress& address, std::error_code& ec) {
        detail::set_reuse_addr(socket.get(), true, ec);
        if (ec) return;
        if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&address.storage), address.length) != 0) {
            ec = detail::last_socket_error();
            return;
        }
        ec.clear();
    });
    return UdpSocket{std::move(socket)};
}

UdpSocket UdpSocket::bind(const SocketAddr& endpoint) {
    std::error_code ec;
    auto res = bind(endpoint, ec);
    if (ec) throw detail::socket_exception("bind(udp)", ec);
    return res;
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept = default;
UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept = default;
UdpSocket::~UdpSocket() = default;

native_handle_type UdpSocket::native_handle() const noexcept {
    return socket_ ? socket_->native() : invalid_native_handle;
}

SourceKind UdpSocket::source_kind() const noexcept {
    return SourceKind::socket;
}

void UdpSocket::on_registered(const Registry& registry, Token token, Interest interests) {
    registry_ = registry;
    token_ = token;
    interests_ = interests;
}

void UdpSocket::on_reregistered(const Registry& registry, Token token, Interest interests) {
    on_registered(registry, token, interests);
}

void UdpSocket::on_deregistered(const Registry& registry) {
    (void)registry;
    registry_.reset();
    interests_ = Interest{};
}

void UdpSocket::rearm_after_would_block() const {
    if (registry_.has_value() && !interests_.empty()) {
        registry_->reregister_handle(native_handle(), source_kind(), token_, interests_);
    }
}

SocketAddr UdpSocket::local_endpoint(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return SocketAddr{};
    return detail::local_endpoint(socket_->get(), ec);
}

SocketAddr UdpSocket::local_endpoint() const {
    std::error_code ec;
    auto res = local_endpoint(ec);
    if (ec) throw detail::socket_exception("local_endpoint", ec);
    return res;
}

SocketAddr UdpSocket::peer_endpoint(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return SocketAddr{};
    return detail::peer_endpoint(socket_->get(), ec);
}

SocketAddr UdpSocket::peer_endpoint() const {
    std::error_code ec;
    auto res = peer_endpoint(ec);
    if (ec) throw detail::socket_exception("peer_endpoint", ec);
    return res;
}

void UdpSocket::connect(const SocketAddr& endpoint, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    auto address = detail::resolve_endpoint(endpoint, SOCK_DGRAM, IPPROTO_UDP, ec);
    if (ec) return;
    if (::connect(socket_->get(), reinterpret_cast<const sockaddr*>(&address.storage), address.length) == 0) {
        ec.clear();
        return;
    }
    ec = detail::last_socket_error();
}

void UdpSocket::connect(const SocketAddr& endpoint) const {
    std::error_code ec;
    connect(endpoint, ec);
    if (ec) throw detail::socket_exception("connect(udp)", ec);
}

std::size_t UdpSocket::send(const void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
#if defined(_WIN32)
    const int result = ::send(socket_->get(), static_cast<const char*>(data), static_cast<int>(size), 0);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#else
    const auto result = ::send(socket_->get(), data, size, 0);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#endif
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t UdpSocket::send(const void* data, std::size_t size) const {
    std::error_code ec;
    auto res = send(data, size, ec);
    if (ec) throw detail::socket_exception("send(udp)", ec);
    return res;
}

std::size_t UdpSocket::send(std::span<const std::byte> buffer, std::error_code& ec) const noexcept {
    return send(buffer.data(), buffer.size(), ec);
}

std::size_t UdpSocket::send(std::span<const std::byte> buffer) const {
    return send(buffer.data(), buffer.size());
}

std::size_t UdpSocket::send(std::string_view text, std::error_code& ec) const noexcept {
    return send(text.data(), text.size(), ec);
}

std::size_t UdpSocket::send(std::string_view text) const {
    return send(text.data(), text.size());
}

std::size_t UdpSocket::recv(void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
#if defined(_WIN32)
    const int result = ::recv(socket_->get(), static_cast<char*>(data), static_cast<int>(size), 0);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#else
    const auto result = ::recv(socket_->get(), data, size, 0);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#endif
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t UdpSocket::recv(void* data, std::size_t size) const {
    std::error_code ec;
    auto res = recv(data, size, ec);
    if (ec) throw detail::socket_exception("recv(udp)", ec);
    return res;
}

std::size_t UdpSocket::recv(std::span<std::byte> buffer, std::error_code& ec) const noexcept {
    return recv(buffer.data(), buffer.size(), ec);
}

std::size_t UdpSocket::recv(std::span<std::byte> buffer) const {
    return recv(buffer.data(), buffer.size());
}

std::size_t UdpSocket::send_to(const void* data, std::size_t size, const SocketAddr& endpoint, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
    auto address = detail::resolve_endpoint(endpoint, SOCK_DGRAM, IPPROTO_UDP, ec);
    if (ec) return 0;
#if defined(_WIN32)
    const int result = ::sendto(socket_->get(), static_cast<const char*>(data), static_cast<int>(size), 0,
                                reinterpret_cast<const sockaddr*>(&address.storage), address.length);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#else
    const auto result = ::sendto(socket_->get(), data, size, 0,
                                 reinterpret_cast<const sockaddr*>(&address.storage), address.length);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
#endif
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t UdpSocket::send_to(const void* data, std::size_t size, const SocketAddr& endpoint) const {
    std::error_code ec;
    auto res = send_to(data, size, endpoint, ec);
    if (ec) throw detail::socket_exception("sendto", ec);
    return res;
}

std::size_t UdpSocket::send_to(std::span<const std::byte> buffer, const SocketAddr& endpoint, std::error_code& ec) const noexcept {
    return send_to(buffer.data(), buffer.size(), endpoint, ec);
}

std::size_t UdpSocket::send_to(std::span<const std::byte> buffer, const SocketAddr& endpoint) const {
    return send_to(buffer.data(), buffer.size(), endpoint);
}

std::size_t UdpSocket::send_vectored_to(std::span<const IoSlice> buffers, const SocketAddr& endpoint, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
    auto address = detail::resolve_endpoint(endpoint, SOCK_DGRAM, IPPROTO_UDP, ec);
    if (ec) return 0;

#if defined(_WIN32)
    DWORD bytes_sent = 0;
    const int result = ::WSASendTo(socket_->get(), const_cast<LPWSABUF>(reinterpret_cast<const WSABUF*>(buffers.data())), 
                                   static_cast<DWORD>(buffers.size()), &bytes_sent, 0,
                                   reinterpret_cast<const sockaddr*>(&address.storage), address.length, nullptr, nullptr);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(bytes_sent);
#else
    msghdr msg{};
    msg.msg_name = &address.storage;
    msg.msg_namelen = address.length;
    msg.msg_iov = reinterpret_cast<iovec*>(const_cast<IoSlice*>(buffers.data()));
    msg.msg_iovlen = buffers.size();

    const auto result = ::sendmsg(socket_->get(), &msg, 0);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
#endif
}

std::size_t UdpSocket::send_vectored_to(std::span<const IoSlice> buffers, const SocketAddr& endpoint) const {
    std::error_code ec;
    auto res = send_vectored_to(buffers, endpoint, ec);
    if (ec) throw detail::socket_exception("sendmsg", ec);
    return res;
}

std::pair<std::size_t, SocketAddr> UdpSocket::recv_from(void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return {0, SocketAddr{}};
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
#if defined(_WIN32)
    const int result = ::recvfrom(socket_->get(), static_cast<char*>(data), static_cast<int>(size), 0,
                                  reinterpret_cast<sockaddr*>(&address), &length);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return {0, SocketAddr{}};
    }
#else
    const auto result = ::recvfrom(socket_->get(), data, size, 0, reinterpret_cast<sockaddr*>(&address),
                                   &length);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return {0, SocketAddr{}};
    }
#endif
    auto endpoint = detail::endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&address), length, ec);
    if (ec) return {0, SocketAddr{}};
    return {static_cast<std::size_t>(result), endpoint};
}

std::pair<std::size_t, SocketAddr> UdpSocket::recv_from(void* data, std::size_t size) const {
    std::error_code ec;
    auto res = recv_from(data, size, ec);
    if (ec) throw detail::socket_exception("recvfrom", ec);
    return res;
}

std::pair<std::size_t, SocketAddr> UdpSocket::recv_from(std::span<std::byte> buffer, std::error_code& ec) const noexcept {
    return recv_from(buffer.data(), buffer.size(), ec);
}

std::pair<std::size_t, SocketAddr> UdpSocket::recv_from(std::span<std::byte> buffer) const {
    return recv_from(buffer.data(), buffer.size());
}

std::pair<std::size_t, SocketAddr> UdpSocket::recv_vectored_from(std::span<IoSliceMut> buffers, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return {0, SocketAddr{}};
    sockaddr_storage address{};
    socklen_t length = sizeof(address);

#if defined(_WIN32)
    DWORD bytes_received = 0;
    DWORD flags = 0;
    const int result = ::WSARecvFrom(socket_->get(), reinterpret_cast<LPWSABUF>(buffers.data()), 
                                     static_cast<DWORD>(buffers.size()), &bytes_received, &flags,
                                     reinterpret_cast<sockaddr*>(&address), &length, nullptr, nullptr);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return {0, SocketAddr{}};
    }
    auto endpoint = detail::endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&address), length, ec);
    if (ec) return {0, SocketAddr{}};
    return {static_cast<std::size_t>(bytes_received), endpoint};
#else
    msghdr msg{};
    msg.msg_name = &address;
    msg.msg_namelen = length;
    msg.msg_iov = reinterpret_cast<iovec*>(buffers.data());
    msg.msg_iovlen = buffers.size();

    const auto result = ::recvmsg(socket_->get(), &msg, 0);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return {0, SocketAddr{}};
    }
    auto endpoint = detail::endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&address), msg.msg_namelen, ec);
    if (ec) return {0, SocketAddr{}};
    return {static_cast<std::size_t>(result), endpoint};
#endif
}

std::pair<std::size_t, SocketAddr> UdpSocket::recv_vectored_from(std::span<IoSliceMut> buffers) const {
    std::error_code ec;
    auto res = recv_vectored_from(buffers, ec);
    if (ec) throw detail::socket_exception("recvmsg", ec);
    return res;
}

std::pair<std::size_t, SocketAddr> UdpSocket::peek_from(void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return {0, SocketAddr{}};
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
#if defined(_WIN32)
    const int result = ::recvfrom(socket_->get(), static_cast<char*>(data), static_cast<int>(size), MSG_PEEK,
                                  reinterpret_cast<sockaddr*>(&address), &length);
    if (result == SOCKET_ERROR) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return {0, SocketAddr{}};
    }
#else
    const auto result = ::recvfrom(socket_->get(), data, size, MSG_PEEK,
                                   reinterpret_cast<sockaddr*>(&address), &length);
    if (result == -1) {
        ec = detail::last_socket_error();
        if (detail::is_would_block_error(ec)) {
            rearm_after_would_block();
        }
        return {0, SocketAddr{}};
    }
#endif
    auto endpoint = detail::endpoint_from_sockaddr(reinterpret_cast<const sockaddr*>(&address), length, ec);
    if (ec) return {0, SocketAddr{}};
    return {static_cast<std::size_t>(result), endpoint};
}

std::pair<std::size_t, SocketAddr> UdpSocket::peek_from(void* data, std::size_t size) const {
    std::error_code ec;
    auto res = peek_from(data, size, ec);
    if (ec) throw detail::socket_exception("recvfrom(MSG_PEEK)", ec);
    return res;
}

void UdpSocket::set_ttl(std::uint32_t ttl, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_socket_int_option(socket_->get(), IPPROTO_IP, IP_TTL, static_cast<int>(ttl), ec);
}

void UdpSocket::set_ttl(std::uint32_t ttl) const {
    std::error_code ec;
    set_ttl(ttl, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

std::uint32_t UdpSocket::ttl(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
    return static_cast<std::uint32_t>(detail::get_socket_int_option(socket_->get(), IPPROTO_IP, IP_TTL, ec));
}

std::uint32_t UdpSocket::ttl() const {
    std::error_code ec;
    auto res = ttl(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

void UdpSocket::set_broadcast(bool enabled, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_socket_bool_option(socket_->get(), SOL_SOCKET, SO_BROADCAST, enabled, ec);
}

void UdpSocket::set_broadcast(bool enabled) const {
    std::error_code ec;
    set_broadcast(enabled, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

bool UdpSocket::broadcast(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return false;
    return detail::get_socket_bool_option(socket_->get(), SOL_SOCKET, SO_BROADCAST, ec);
}

bool UdpSocket::broadcast() const {
    std::error_code ec;
    auto res = broadcast(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

void UdpSocket::set_dont_fragment(bool enabled, std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return;
    detail::set_dont_fragment(socket_->get(), enabled, ec);
}

void UdpSocket::set_dont_fragment(bool enabled) const {
    std::error_code ec;
    set_dont_fragment(enabled, ec);
    if (ec) throw detail::socket_exception("setsockopt", ec);
}

std::uint32_t UdpSocket::mtu(std::error_code& ec) const noexcept {
    require_socket(socket_, ec);
    if (ec) return 0;
    return detail::get_mtu(socket_->get(), ec);
}

std::uint32_t UdpSocket::mtu() const {
    std::error_code ec;
    auto res = mtu(ec);
    if (ec) throw detail::socket_exception("getsockopt", ec);
    return res;
}

std::optional<std::error_code> UdpSocket::take_error() const {
    require_socket(socket_);
    return detail::get_socket_error(socket_->get());
}

bool UdpSocket::valid() const noexcept {
    return socket_ && socket_->valid();
}

} // namespace ioxx::net

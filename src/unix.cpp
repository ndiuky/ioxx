#include <ioxx/unix.hpp>

#if !defined(_WIN32)

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

void throw_errno(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

void close_fd(int fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
    }
}

void set_non_blocking(int fd, std::error_code& ec) noexcept {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return;
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return;
    }
    ec.clear();
}

void set_non_blocking(int fd) {
    std::error_code ec;
    set_non_blocking(fd, ec);
    if (ec) throw std::system_error(ec, "fcntl(F_GETFL/F_SETFL)");
}

void set_close_on_exec(int fd, std::error_code& ec) noexcept {
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return;
    }
    if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return;
    }
    ec.clear();
}

void set_close_on_exec(int fd) {
    std::error_code ec;
    set_close_on_exec(fd, ec);
    if (ec) throw std::system_error(ec, "fcntl(F_GETFD/F_SETFD)");
}

void prepare_fd(int fd, std::error_code& ec) noexcept {
    set_non_blocking(fd, ec);
    if (ec) return;
    set_close_on_exec(fd, ec);
}

void prepare_fd(int fd) {
    std::error_code ec;
    prepare_fd(fd, ec);
    if (ec) throw std::system_error(ec, "prepare_fd");
}

sockaddr_un socket_address(std::string_view path, std::error_code& ec) noexcept {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        ec = std::make_error_code(std::errc::filename_too_long);
        return address;
    }
    std::memcpy(address.sun_path, path.data(), path.size());
    address.sun_path[path.size()] = '\0';
    ec.clear();
    return address;
}

sockaddr_un socket_address(std::string_view path) {
    std::error_code ec;
    auto res = socket_address(path, ec);
    if (ec) throw std::system_error(ec, "Unix socket path too long");
    return res;
}

socklen_t socket_address_length(const sockaddr_un& address) {
    return static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + std::strlen(address.sun_path) + 1);
}

std::string path_from_sockaddr(const sockaddr_un& address, socklen_t length) {
    if (length <= offsetof(sockaddr_un, sun_path)) {
        return {};
    }
    const auto path_length = static_cast<std::size_t>(length - offsetof(sockaddr_un, sun_path));
    return std::string{address.sun_path, strnlen(address.sun_path, path_length)};
}

int make_socket(int type, std::error_code& ec) noexcept {
    const int fd = ::socket(AF_UNIX, type, 0);
    if (fd == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return -1;
    }
    prepare_fd(fd, ec);
    if (ec) {
        close_fd(fd);
        return -1;
    }
    return fd;
}

int make_socket(int type) {
    std::error_code ec;
    int res = make_socket(type, ec);
    if (ec) throw std::system_error(ec, "socket(AF_UNIX)");
    return res;
}

void require_fd(int fd, std::error_code& ec) noexcept {
    if (fd < 0) {
        ec = std::make_error_code(std::errc::bad_file_descriptor);
    } else {
        ec.clear();
    }
}

void require_fd(int fd) {
    std::error_code ec;
    require_fd(fd, ec);
    if (ec) throw std::system_error(ec, "invalid Unix socket");
}

} // namespace

namespace ioxx::unix {

SourceFd::SourceFd(int fd) noexcept : fd_(fd) {}

native_handle_type SourceFd::native_handle() const noexcept {
    return fd_;
}

SourceKind SourceFd::source_kind() const noexcept {
    return SourceKind::raw_descriptor;
}

Sender::Sender() noexcept = default;

Sender::Sender(int fd) : fd_(fd) {
    require_fd(fd_);
    prepare_fd(fd_);
}

Sender::Sender(Sender&& other) noexcept : fd_(other.release()) {}

Sender& Sender::operator=(Sender&& other) noexcept {
    if (this != &other) {
        close_fd(fd_);
        fd_ = other.release();
    }
    return *this;
}

Sender::~Sender() {
    close_fd(fd_);
}

native_handle_type Sender::native_handle() const noexcept {
    return fd_;
}

SourceKind Sender::source_kind() const noexcept {
    return SourceKind::raw_descriptor;
}

bool Sender::valid() const noexcept {
    return fd_ >= 0;
}

int Sender::release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

std::size_t Sender::write(const void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return 0;
    const auto result = ::write(fd_, data, size);
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t Sender::write(const void* data, std::size_t size) const {
    std::error_code ec;
    auto res = write(data, size, ec);
    if (ec) throw std::system_error(ec, "write(pipe)");
    return res;
}

std::size_t Sender::write(std::span<const std::byte> buffer, std::error_code& ec) const noexcept {
    return write(buffer.data(), buffer.size(), ec);
}

std::size_t Sender::write(std::span<const std::byte> buffer) const {
    return write(buffer.data(), buffer.size());
}

Receiver::Receiver() noexcept = default;

Receiver::Receiver(int fd) : fd_(fd) {
    require_fd(fd_);
    prepare_fd(fd_);
}

Receiver::Receiver(Receiver&& other) noexcept : fd_(other.release()) {}

Receiver& Receiver::operator=(Receiver&& other) noexcept {
    if (this != &other) {
        close_fd(fd_);
        fd_ = other.release();
    }
    return *this;
}

Receiver::~Receiver() {
    close_fd(fd_);
}

native_handle_type Receiver::native_handle() const noexcept {
    return fd_;
}

SourceKind Receiver::source_kind() const noexcept {
    return SourceKind::raw_descriptor;
}

bool Receiver::valid() const noexcept {
    return fd_ >= 0;
}

int Receiver::release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

std::size_t Receiver::read(void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return 0;
    const auto result = ::read(fd_, data, size);
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t Receiver::read(void* data, std::size_t size) const {
    std::error_code ec;
    auto res = read(data, size, ec);
    if (ec) throw std::system_error(ec, "read(pipe)");
    return res;
}

std::size_t Receiver::read(std::span<std::byte> buffer, std::error_code& ec) const noexcept {
    return read(buffer.data(), buffer.size(), ec);
}

std::size_t Receiver::read(std::span<std::byte> buffer) const {
    return read(buffer.data(), buffer.size());
}

std::pair<Sender, Receiver> pipe(std::error_code& ec) noexcept {
    int fds[2] = {-1, -1};
    if (::pipe(fds) == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return {Sender{}, Receiver{}};
    }
    prepare_fd(fds[0], ec);
    if (!ec) {
        prepare_fd(fds[1], ec);
    }
    if (ec) {
        close_fd(fds[0]);
        close_fd(fds[1]);
        return {Sender{}, Receiver{}};
    }
    return {Sender{fds[1]}, Receiver{fds[0]}};
}

std::pair<Sender, Receiver> pipe() {
    std::error_code ec;
    auto res = pipe(ec);
    if (ec) throw std::system_error(ec, "pipe");
    return res;
}

} // namespace ioxx::unix

namespace ioxx::net {

UnixStream::UnixStream() noexcept = default;

UnixStream::UnixStream(int fd) : fd_(fd) {
    require_fd(fd_);
    prepare_fd(fd_);
}

UnixStream UnixStream::connect(std::string_view path, std::error_code& ec) noexcept {
    const int fd = make_socket(SOCK_STREAM, ec);
    if (ec) return UnixStream{};
    auto address = socket_address(path, ec);
    if (ec) {
        close_fd(fd);
        return UnixStream{};
    }
    const int result = ::connect(fd, reinterpret_cast<const sockaddr*>(&address), socket_address_length(address));
    if (result == -1 && errno != EINPROGRESS) {
        ec = std::error_code{errno, std::generic_category()};
        close_fd(fd);
        return UnixStream{};
    }
    ec.clear();
    return UnixStream{fd};
}

UnixStream UnixStream::connect(std::string_view path) {
    std::error_code ec;
    auto res = connect(path, ec);
    if (ec) throw std::system_error(ec, "connect(AF_UNIX)");
    return res;
}

std::pair<UnixStream, UnixStream> UnixStream::pair(std::error_code& ec) noexcept {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return {UnixStream{}, UnixStream{}};
    }
    prepare_fd(fds[0], ec);
    if (!ec) prepare_fd(fds[1], ec);
    if (ec) {
        close_fd(fds[0]);
        close_fd(fds[1]);
        return {UnixStream{}, UnixStream{}};
    }
    return {UnixStream{fds[0]}, UnixStream{fds[1]}};
}

std::pair<UnixStream, UnixStream> UnixStream::pair() {
    std::error_code ec;
    auto res = pair(ec);
    if (ec) throw std::system_error(ec, "socketpair(SOCK_STREAM)");
    return res;
}

UnixStream::UnixStream(UnixStream&& other) noexcept : fd_(other.release()) {}

UnixStream& UnixStream::operator=(UnixStream&& other) noexcept {
    if (this != &other) {
        close_fd(fd_);
        fd_ = other.release();
    }
    return *this;
}

UnixStream::~UnixStream() {
    close_fd(fd_);
}

native_handle_type UnixStream::native_handle() const noexcept {
    return fd_;
}

SourceKind UnixStream::source_kind() const noexcept {
    return SourceKind::raw_descriptor;
}

bool UnixStream::valid() const noexcept {
    return fd_ >= 0;
}

int UnixStream::release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

std::size_t UnixStream::read(void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return 0;
    const auto result = ::read(fd_, data, size);
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t UnixStream::read(void* data, std::size_t size) const {
    std::error_code ec;
    auto res = read(data, size, ec);
    if (ec) throw std::system_error(ec, "read(AF_UNIX)");
    return res;
}

std::size_t UnixStream::read(std::span<std::byte> buffer, std::error_code& ec) const noexcept {
    return read(buffer.data(), buffer.size(), ec);
}

std::size_t UnixStream::read(std::span<std::byte> buffer) const {
    return read(buffer.data(), buffer.size());
}

std::size_t UnixStream::write(const void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return 0;
    const auto result = ::write(fd_, data, size);
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t UnixStream::write(const void* data, std::size_t size) const {
    std::error_code ec;
    auto res = write(data, size, ec);
    if (ec) throw std::system_error(ec, "write(AF_UNIX)");
    return res;
}

std::size_t UnixStream::write(std::span<const std::byte> buffer, std::error_code& ec) const noexcept {
    return write(buffer.data(), buffer.size(), ec);
}

std::size_t UnixStream::write(std::span<const std::byte> buffer) const {
    return write(buffer.data(), buffer.size());
}

std::size_t UnixStream::write(std::string_view text, std::error_code& ec) const noexcept {
    return write(text.data(), text.size(), ec);
}

std::size_t UnixStream::write(std::string_view text) const {
    return write(text.data(), text.size());
}

void UnixStream::shutdown_read(std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return;
    if (::shutdown(fd_, SHUT_RD) == -1) {
        ec = std::error_code{errno, std::generic_category()};
    } else {
        ec.clear();
    }
}

void UnixStream::shutdown_read() const {
    std::error_code ec;
    shutdown_read(ec);
    if (ec) throw std::system_error(ec, "shutdown(SHUT_RD)");
}

void UnixStream::shutdown_write(std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return;
    if (::shutdown(fd_, SHUT_WR) == -1) {
        ec = std::error_code{errno, std::generic_category()};
    } else {
        ec.clear();
    }
}

void UnixStream::shutdown_write() const {
    std::error_code ec;
    shutdown_write(ec);
    if (ec) throw std::system_error(ec, "shutdown(SHUT_WR)");
}

void UnixStream::shutdown_both(std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return;
    if (::shutdown(fd_, SHUT_RDWR) == -1) {
        ec = std::error_code{errno, std::generic_category()};
    } else {
        ec.clear();
    }
}

void UnixStream::shutdown_both() const {
    std::error_code ec;
    shutdown_both(ec);
    if (ec) throw std::system_error(ec, "shutdown(SHUT_RDWR)");
}

UnixListener::UnixListener() noexcept = default;

UnixListener::UnixListener(int fd) : fd_(fd) {
    require_fd(fd_);
    prepare_fd(fd_);
}

UnixListener UnixListener::bind(std::string_view path, int backlog, std::error_code& ec) noexcept {
    const int fd = make_socket(SOCK_STREAM, ec);
    if (ec) return UnixListener{};
    auto address = socket_address(path, ec);
    if (ec) {
        close_fd(fd);
        return UnixListener{};
    }
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), socket_address_length(address)) == -1) {
        ec = std::error_code{errno, std::generic_category()};
        close_fd(fd);
        return UnixListener{};
    }
    if (::listen(fd, backlog) == -1) {
        ec = std::error_code{errno, std::generic_category()};
        close_fd(fd);
        return UnixListener{};
    }
    ec.clear();
    return UnixListener{fd};
}

UnixListener UnixListener::bind(std::string_view path, std::error_code& ec) noexcept {
    return bind(path, 128, ec);
}

UnixListener UnixListener::bind(std::string_view path, int backlog) {
    std::error_code ec;
    auto res = bind(path, backlog, ec);
    if (ec) throw std::system_error(ec, "bind(AF_UNIX)");
    return res;
}

UnixListener::UnixListener(UnixListener&& other) noexcept : fd_(other.release()) {}

UnixListener& UnixListener::operator=(UnixListener&& other) noexcept {
    if (this != &other) {
        close_fd(fd_);
        fd_ = other.release();
    }
    return *this;
}

UnixListener::~UnixListener() {
    close_fd(fd_);
}

native_handle_type UnixListener::native_handle() const noexcept {
    return fd_;
}

SourceKind UnixListener::source_kind() const noexcept {
    return SourceKind::raw_descriptor;
}

bool UnixListener::valid() const noexcept {
    return fd_ >= 0;
}

int UnixListener::release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

std::pair<UnixStream, std::string> UnixListener::accept(std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return {UnixStream{}, std::string{}};
    sockaddr_un address{};
    socklen_t length = sizeof(address);
    const int accepted = ::accept(fd_, reinterpret_cast<sockaddr*>(&address), &length);
    if (accepted == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return {UnixStream{}, std::string{}};
    }
    ec.clear();
    std::string path;
    try {
        path = path_from_sockaddr(address, length);
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        close_fd(accepted);
        return {UnixStream{}, std::string{}};
    }
    return {UnixStream{accepted}, path};
}

std::pair<UnixStream, std::string> UnixListener::accept() const {
    std::error_code ec;
    auto res = accept(ec);
    if (ec) throw std::system_error(ec, "accept(AF_UNIX)");
    return res;
}

UnixDatagram::UnixDatagram() noexcept = default;

UnixDatagram::UnixDatagram(int fd) : fd_(fd) {
    require_fd(fd_);
    prepare_fd(fd_);
}

UnixDatagram UnixDatagram::bind(std::string_view path, std::error_code& ec) noexcept {
    const int fd = make_socket(SOCK_DGRAM, ec);
    if (ec) return UnixDatagram{};
    auto address = socket_address(path, ec);
    if (ec) {
        close_fd(fd);
        return UnixDatagram{};
    }
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), socket_address_length(address)) == -1) {
        ec = std::error_code{errno, std::generic_category()};
        close_fd(fd);
        return UnixDatagram{};
    }
    ec.clear();
    return UnixDatagram{fd};
}

UnixDatagram UnixDatagram::bind(std::string_view path) {
    std::error_code ec;
    auto res = bind(path, ec);
    if (ec) throw std::system_error(ec, "bind(AF_UNIX datagram)");
    return res;
}

UnixDatagram UnixDatagram::unbound(std::error_code& ec) noexcept {
    return UnixDatagram{make_socket(SOCK_DGRAM, ec)};
}

UnixDatagram UnixDatagram::unbound() {
    std::error_code ec;
    auto res = unbound(ec);
    if (ec) throw std::system_error(ec, "socket(AF_UNIX datagram)");
    return res;
}

std::pair<UnixDatagram, UnixDatagram> UnixDatagram::pair(std::error_code& ec) noexcept {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return {UnixDatagram{}, UnixDatagram{}};
    }
    prepare_fd(fds[0], ec);
    if (!ec) prepare_fd(fds[1], ec);
    if (ec) {
        close_fd(fds[0]);
        close_fd(fds[1]);
        return {UnixDatagram{}, UnixDatagram{}};
    }
    return {UnixDatagram{fds[0]}, UnixDatagram{fds[1]}};
}

std::pair<UnixDatagram, UnixDatagram> UnixDatagram::pair() {
    std::error_code ec;
    auto res = pair(ec);
    if (ec) throw std::system_error(ec, "socketpair(SOCK_DGRAM)");
    return res;
}

UnixDatagram::UnixDatagram(UnixDatagram&& other) noexcept : fd_(other.release()) {}

UnixDatagram& UnixDatagram::operator=(UnixDatagram&& other) noexcept {
    if (this != &other) {
        close_fd(fd_);
        fd_ = other.release();
    }
    return *this;
}

UnixDatagram::~UnixDatagram() {
    close_fd(fd_);
}

native_handle_type UnixDatagram::native_handle() const noexcept {
    return fd_;
}

SourceKind UnixDatagram::source_kind() const noexcept {
    return SourceKind::raw_descriptor;
}

bool UnixDatagram::valid() const noexcept {
    return fd_ >= 0;
}

int UnixDatagram::release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

std::size_t UnixDatagram::send(const void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return 0;
    const auto result = ::send(fd_, data, size, 0);
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t UnixDatagram::send(const void* data, std::size_t size) const {
    std::error_code ec;
    auto res = send(data, size, ec);
    if (ec) throw std::system_error(ec, "send(AF_UNIX datagram)");
    return res;
}

std::size_t UnixDatagram::send(std::span<const std::byte> buffer, std::error_code& ec) const noexcept {
    return send(buffer.data(), buffer.size(), ec);
}

std::size_t UnixDatagram::send(std::span<const std::byte> buffer) const {
    return send(buffer.data(), buffer.size());
}

std::size_t UnixDatagram::send_to(const void* data, std::size_t size, std::string_view path, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return 0;
    auto address = socket_address(path, ec);
    if (ec) return 0;
    const auto result = ::sendto(fd_, data, size, 0, reinterpret_cast<const sockaddr*>(&address),
                                 socket_address_length(address));
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t UnixDatagram::send_to(const void* data, std::size_t size, std::string_view path) const {
    std::error_code ec;
    auto res = send_to(data, size, path, ec);
    if (ec) throw std::system_error(ec, "sendto(AF_UNIX datagram)");
    return res;
}

std::pair<std::size_t, std::string> UnixDatagram::recv_from(void* data, std::size_t size, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return {0, std::string{}};
    sockaddr_un address{};
    socklen_t length = sizeof(address);
    const auto result = ::recvfrom(fd_, data, size, 0, reinterpret_cast<sockaddr*>(&address), &length);
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return {0, std::string{}};
    }
    ec.clear();
    std::string path;
    try {
        path = path_from_sockaddr(address, length);
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {0, std::string{}};
    }
    return {static_cast<std::size_t>(result), path};
}

std::pair<std::size_t, std::string> UnixDatagram::recv_from(void* data, std::size_t size) const {
    std::error_code ec;
    auto res = recv_from(data, size, ec);
    if (ec) throw std::system_error(ec, "recvfrom(AF_UNIX datagram)");
    return res;
}

std::pair<std::size_t, std::string> UnixDatagram::recv_from(std::span<std::byte> buffer, std::error_code& ec) const noexcept {
    return recv_from(buffer.data(), buffer.size(), ec);
}

std::pair<std::size_t, std::string> UnixDatagram::recv_from(std::span<std::byte> buffer) const {
    return recv_from(buffer.data(), buffer.size());
}

std::size_t UnixDatagram::send_vectored_to(std::span<const net::IoSlice> buffers, std::string_view path, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return 0;
    sockaddr_un address{};
    socklen_t length = 0;
    if (!path.empty()) {
        try {
            address = sockaddr_from_path(path, length);
        } catch (const std::invalid_argument&) {
            ec = std::make_error_code(std::errc::invalid_argument);
            return 0;
        }
    }
    
    msghdr msg{};
    if (!path.empty()) {
        msg.msg_name = &address;
        msg.msg_namelen = length;
    }
    msg.msg_iov = reinterpret_cast<iovec*>(const_cast<net::IoSlice*>(buffers.data()));
    msg.msg_iovlen = buffers.size();

    const auto result = ::sendmsg(fd_, &msg, 0);
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return 0;
    }
    ec.clear();
    return static_cast<std::size_t>(result);
}

std::size_t UnixDatagram::send_vectored_to(std::span<const net::IoSlice> buffers, std::string_view path) const {
    std::error_code ec;
    auto res = send_vectored_to(buffers, path, ec);
    if (ec) throw std::system_error(ec, "sendmsg(AF_UNIX datagram)");
    return res;
}

std::pair<std::size_t, std::string> UnixDatagram::recv_vectored_from(std::span<net::IoSliceMut> buffers, std::error_code& ec) const noexcept {
    require_fd(fd_, ec);
    if (ec) return {0, std::string{}};
    sockaddr_un address{};
    socklen_t length = sizeof(address);
    
    msghdr msg{};
    msg.msg_name = &address;
    msg.msg_namelen = length;
    msg.msg_iov = reinterpret_cast<iovec*>(buffers.data());
    msg.msg_iovlen = buffers.size();

    const auto result = ::recvmsg(fd_, &msg, 0);
    if (result == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return {0, std::string{}};
    }
    ec.clear();
    std::string path_str;
    if (msg.msg_namelen > 0) {
        try {
            path_str = path_from_sockaddr(address, msg.msg_namelen);
        } catch(const std::bad_alloc&) {
            ec = std::make_error_code(std::errc::not_enough_memory);
            return {0, std::string{}};
        }
    }
    return {static_cast<std::size_t>(result), path_str};
}

std::pair<std::size_t, std::string> UnixDatagram::recv_vectored_from(std::span<net::IoSliceMut> buffers) const {
    std::error_code ec;
    auto res = recv_vectored_from(buffers, ec);
    if (ec) throw std::system_error(ec, "recvmsg(AF_UNIX datagram)");
    return res;
}

} // namespace ioxx::net

#endif

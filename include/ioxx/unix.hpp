#pragma once

#if !defined(_WIN32)

#include <ioxx/core.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace ioxx::unix {

class IOXX_API SourceFd final : public Source {
public:
    explicit SourceFd(int fd) noexcept;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;

private:
    int fd_{};
};

class IOXX_API Sender final : public Source {
public:
    Sender() noexcept;
    explicit Sender(int fd);

    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    Sender(Sender&& other) noexcept;
    Sender& operator=(Sender&& other) noexcept;
    ~Sender() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int release() noexcept;

    std::size_t write(const void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t write(const void* data, std::size_t size) const;
    std::size_t write(std::span<const std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t write(std::span<const std::byte> buffer) const;

private:
    int fd_{-1};
};

class IOXX_API Receiver final : public Source {
public:
    Receiver() noexcept;
    explicit Receiver(int fd);

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&& other) noexcept;
    Receiver& operator=(Receiver&& other) noexcept;
    ~Receiver() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int release() noexcept;

    std::size_t read(void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t read(void* data, std::size_t size) const;
    std::size_t read(std::span<std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t read(std::span<std::byte> buffer) const;

private:
    int fd_{-1};
};

std::pair<Sender, Receiver> pipe(std::error_code& ec) noexcept;
std::pair<Sender, Receiver> pipe();

} // namespace ioxx::unix

namespace ioxx::net {

class IOXX_API UnixStream final : public Source {
public:
    UnixStream() noexcept;
    explicit UnixStream(int fd);
    static UnixStream connect(std::string_view path, std::error_code& ec) noexcept;
    static UnixStream connect(std::string_view path);
    static std::pair<UnixStream, UnixStream> pair(std::error_code& ec) noexcept;
    static std::pair<UnixStream, UnixStream> pair();

    UnixStream(const UnixStream&) = delete;
    UnixStream& operator=(const UnixStream&) = delete;
    UnixStream(UnixStream&& other) noexcept;
    UnixStream& operator=(UnixStream&& other) noexcept;
    ~UnixStream() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int release() noexcept;

    std::size_t read(void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t read(void* data, std::size_t size) const;
    std::size_t read(std::span<std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t read(std::span<std::byte> buffer) const;
    std::size_t write(const void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t write(const void* data, std::size_t size) const;
    std::size_t write(std::span<const std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t write(std::span<const std::byte> buffer) const;
    std::size_t write(std::string_view text, std::error_code& ec) const noexcept;
    std::size_t write(std::string_view text) const;
    void shutdown_read(std::error_code& ec) const noexcept;
    void shutdown_read() const;
    void shutdown_write(std::error_code& ec) const noexcept;
    void shutdown_write() const;
    void shutdown_both(std::error_code& ec) const noexcept;
    void shutdown_both() const;

private:
    int fd_{-1};
};

class IOXX_API UnixListener final : public Source {
public:
    UnixListener() noexcept;
    explicit UnixListener(int fd);
    static UnixListener bind(std::string_view path, int backlog, std::error_code& ec) noexcept;
    static UnixListener bind(std::string_view path, std::error_code& ec) noexcept;
    static UnixListener bind(std::string_view path, int backlog = 128);

    UnixListener(const UnixListener&) = delete;
    UnixListener& operator=(const UnixListener&) = delete;
    UnixListener(UnixListener&& other) noexcept;
    UnixListener& operator=(UnixListener&& other) noexcept;
    ~UnixListener() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int release() noexcept;

    std::pair<UnixStream, std::string> accept(std::error_code& ec) const noexcept;
    std::pair<UnixStream, std::string> accept() const;

private:
    int fd_{-1};
};

class IOXX_API UnixDatagram final : public Source {
public:
    UnixDatagram() noexcept;
    explicit UnixDatagram(int fd);
    static UnixDatagram bind(std::string_view path, std::error_code& ec) noexcept;
    static UnixDatagram bind(std::string_view path);
    static UnixDatagram unbound(std::error_code& ec) noexcept;
    static UnixDatagram unbound();
    static std::pair<UnixDatagram, UnixDatagram> pair(std::error_code& ec) noexcept;
    static std::pair<UnixDatagram, UnixDatagram> pair();

    UnixDatagram(const UnixDatagram&) = delete;
    UnixDatagram& operator=(const UnixDatagram&) = delete;
    UnixDatagram(UnixDatagram&& other) noexcept;
    UnixDatagram& operator=(UnixDatagram&& other) noexcept;
    ~UnixDatagram() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int release() noexcept;

    std::size_t send(const void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t send(const void* data, std::size_t size) const;
    std::size_t send(std::span<const std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t send(std::span<const std::byte> buffer) const;
    std::size_t send_to(const void* data, std::size_t size, std::string_view path, std::error_code& ec) const noexcept;
    std::size_t send_to(const void* data, std::size_t size, std::string_view path) const;
    std::pair<std::size_t, std::string> recv_from(void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::pair<std::size_t, std::string> recv_from(void* data, std::size_t size) const;
    std::pair<std::size_t, std::string> recv_from(std::span<std::byte> buffer, std::error_code& ec) const noexcept;
    std::pair<std::size_t, std::string> recv_from(std::span<std::byte> buffer) const;

    std::size_t send_vectored_to(std::span<const net::IoSlice> buffers, std::string_view path, std::error_code& ec) const noexcept;
    std::size_t send_vectored_to(std::span<const net::IoSlice> buffers, std::string_view path) const;
    std::pair<std::size_t, std::string> recv_vectored_from(std::span<net::IoSliceMut> buffers, std::error_code& ec) const noexcept;
    std::pair<std::size_t, std::string> recv_vectored_from(std::span<net::IoSliceMut> buffers) const;

private:
    int fd_{-1};
};

} // namespace ioxx::net

#endif

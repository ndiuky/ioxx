#pragma once

#include <ioxx/core.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <array>
#include <optional>
#include <span>
#include <variant>
#include <string>
#include <string_view>
#include <utility>

namespace ioxx::detail {
class Socket;
}

namespace ioxx::net {

#if defined(_WIN32)
struct NativeIoSlice {
    unsigned long len;
    char* buf;
};
#else
struct NativeIoSlice {
    void* iov_base;
    std::size_t iov_len;
};
#endif

class IOXX_API IoSliceMut {
public:
    IoSliceMut() noexcept = default;
    IoSliceMut(void* data, std::size_t size) noexcept {
#if defined(_WIN32)
        slice_.len = static_cast<unsigned long>(size);
        slice_.buf = static_cast<char*>(data);
#else
        slice_.iov_base = data;
        slice_.iov_len = size;
#endif
    }
    explicit IoSliceMut(std::span<std::byte> buffer) noexcept 
        : IoSliceMut(buffer.data(), buffer.size()) {}

    [[nodiscard]] void* data() const noexcept {
#if defined(_WIN32)
        return slice_.buf;
#else
        return slice_.iov_base;
#endif
    }

    [[nodiscard]] std::size_t size() const noexcept {
#if defined(_WIN32)
        return slice_.len;
#else
        return slice_.iov_len;
#endif
    }

private:
    friend class TcpStream;
    friend class UdpSocket;
    NativeIoSlice slice_{};
};

class IOXX_API IoSlice {
public:
    IoSlice() noexcept = default;
    IoSlice(const void* data, std::size_t size) noexcept {
#if defined(_WIN32)
        slice_.len = static_cast<unsigned long>(size);
        slice_.buf = static_cast<char*>(const_cast<void*>(data));
#else
        slice_.iov_base = const_cast<void*>(data);
        slice_.iov_len = size;
#endif
    }
    explicit IoSlice(std::span<const std::byte> buffer) noexcept 
        : IoSlice(buffer.data(), buffer.size()) {}

    [[nodiscard]] const void* data() const noexcept {
#if defined(_WIN32)
        return slice_.buf;
#else
        return slice_.iov_base;
#endif
    }

    [[nodiscard]] std::size_t size() const noexcept {
#if defined(_WIN32)
        return slice_.len;
#else
        return slice_.iov_len;
#endif
    }

private:
    friend class TcpStream;
    friend class UdpSocket;
    NativeIoSlice slice_{};
};

class IOXX_API Ipv4Addr {
public:
    constexpr Ipv4Addr() noexcept = default;
    constexpr Ipv4Addr(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) noexcept
        : octets_{a, b, c, d} {}
    constexpr explicit Ipv4Addr(const std::array<std::uint8_t, 4>& octets) noexcept : octets_(octets) {}

    [[nodiscard]] constexpr const std::array<std::uint8_t, 4>& octets() const noexcept { return octets_; }

    friend constexpr bool operator==(const Ipv4Addr&, const Ipv4Addr&) noexcept = default;

private:
    std::array<std::uint8_t, 4> octets_{};
};

class IOXX_API Ipv6Addr {
public:
    constexpr Ipv6Addr() noexcept = default;
    constexpr explicit Ipv6Addr(const std::array<std::uint8_t, 16>& octets) noexcept : octets_(octets) {}
    constexpr Ipv6Addr(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d,
                       std::uint16_t e, std::uint16_t f, std::uint16_t g, std::uint16_t h) noexcept
        : octets_{static_cast<std::uint8_t>(a >> 8), static_cast<std::uint8_t>(a),
                  static_cast<std::uint8_t>(b >> 8), static_cast<std::uint8_t>(b),
                  static_cast<std::uint8_t>(c >> 8), static_cast<std::uint8_t>(c),
                  static_cast<std::uint8_t>(d >> 8), static_cast<std::uint8_t>(d),
                  static_cast<std::uint8_t>(e >> 8), static_cast<std::uint8_t>(e),
                  static_cast<std::uint8_t>(f >> 8), static_cast<std::uint8_t>(f),
                  static_cast<std::uint8_t>(g >> 8), static_cast<std::uint8_t>(g),
                  static_cast<std::uint8_t>(h >> 8), static_cast<std::uint8_t>(h)} {}

    [[nodiscard]] constexpr const std::array<std::uint8_t, 16>& octets() const noexcept { return octets_; }

    friend constexpr bool operator==(const Ipv6Addr&, const Ipv6Addr&) noexcept = default;

private:
    std::array<std::uint8_t, 16> octets_{};
};

class IOXX_API SocketAddrV4 {
public:
    constexpr SocketAddrV4() noexcept = default;
    constexpr SocketAddrV4(Ipv4Addr ip, std::uint16_t port) noexcept : ip_(ip), port_(port) {}

    [[nodiscard]] constexpr const Ipv4Addr& ip() const noexcept { return ip_; }
    [[nodiscard]] constexpr std::uint16_t port() const noexcept { return port_; }

    friend constexpr bool operator==(const SocketAddrV4&, const SocketAddrV4&) noexcept = default;

private:
    Ipv4Addr ip_{};
    std::uint16_t port_{};
};

class IOXX_API SocketAddrV6 {
public:
    constexpr SocketAddrV6() noexcept = default;
    constexpr SocketAddrV6(Ipv6Addr ip, std::uint16_t port, std::uint32_t flowinfo = 0, std::uint32_t scope_id = 0) noexcept
        : ip_(ip), port_(port), flowinfo_(flowinfo), scope_id_(scope_id) {}

    [[nodiscard]] constexpr const Ipv6Addr& ip() const noexcept { return ip_; }
    [[nodiscard]] constexpr std::uint16_t port() const noexcept { return port_; }
    [[nodiscard]] constexpr std::uint32_t flowinfo() const noexcept { return flowinfo_; }
    [[nodiscard]] constexpr std::uint32_t scope_id() const noexcept { return scope_id_; }

    friend constexpr bool operator==(const SocketAddrV6&, const SocketAddrV6&) noexcept = default;

private:
    Ipv6Addr ip_{};
    std::uint16_t port_{};
    std::uint32_t flowinfo_{};
    std::uint32_t scope_id_{};
};

class IOXX_API SocketAddr {
public:
    constexpr SocketAddr() noexcept = default;
    constexpr SocketAddr(SocketAddrV4 v4) noexcept : addr_(v4) {}
    constexpr SocketAddr(SocketAddrV6 v6) noexcept : addr_(v6) {}

    [[nodiscard]] constexpr bool is_v4() const noexcept { return std::holds_alternative<SocketAddrV4>(addr_); }
    [[nodiscard]] constexpr bool is_v6() const noexcept { return std::holds_alternative<SocketAddrV6>(addr_); }

    [[nodiscard]] constexpr std::uint16_t port() const noexcept {
        if (is_v4()) {
            return std::get<SocketAddrV4>(addr_).port();
        } else {
            return std::get<SocketAddrV6>(addr_).port();
        }
    }

    [[nodiscard]] constexpr const SocketAddrV4* as_v4() const noexcept { return std::get_if<SocketAddrV4>(&addr_); }
    [[nodiscard]] constexpr const SocketAddrV6* as_v6() const noexcept { return std::get_if<SocketAddrV6>(&addr_); }

    friend constexpr bool operator==(const SocketAddr&, const SocketAddr&) noexcept = default;

private:
    std::variant<SocketAddrV4, SocketAddrV6> addr_;
};

class IOXX_API TcpStream;
class IOXX_API TcpListener;

class IOXX_API TcpSocket {
public:
    static TcpSocket new_v4();
    static TcpSocket new_v4(std::error_code& ec) noexcept;
    static TcpSocket new_v6();
    static TcpSocket new_v6(std::error_code& ec) noexcept;

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    ~TcpSocket();

    void set_reuseaddr(bool reuse, std::error_code& ec) const noexcept;
    void set_reuseaddr(bool reuse) const;
    [[nodiscard]] bool reuseaddr(std::error_code& ec) const noexcept;
    [[nodiscard]] bool reuseaddr() const;

    void set_reuseport(bool reuse, std::error_code& ec) const noexcept;
    void set_reuseport(bool reuse) const;
    [[nodiscard]] bool reuseport(std::error_code& ec) const noexcept;
    [[nodiscard]] bool reuseport() const;

    void set_keepalive(bool keepalive, std::error_code& ec) const noexcept;
    void set_keepalive(bool keepalive) const;
    [[nodiscard]] bool keepalive(std::error_code& ec) const noexcept;
    [[nodiscard]] bool keepalive() const;

    void set_nodelay(bool nodelay, std::error_code& ec) const noexcept;
    void set_nodelay(bool nodelay) const;
    [[nodiscard]] bool nodelay(std::error_code& ec) const noexcept;
    [[nodiscard]] bool nodelay() const;

    void set_tcp_fastopen(int backlog, std::error_code& ec) const noexcept;
    void set_tcp_fastopen(int backlog) const;
    void set_tcp_fastopen_connect(bool enabled, std::error_code& ec) const noexcept;
    void set_tcp_fastopen_connect(bool enabled) const;

    TcpListener bind(const SocketAddr& endpoint, int backlog, std::error_code& ec) noexcept;
    TcpListener bind(const SocketAddr& endpoint, int backlog = 128);
    TcpStream connect(const SocketAddr& endpoint, std::error_code& ec) noexcept;
    TcpStream connect(const SocketAddr& endpoint);

private:
    explicit TcpSocket(std::unique_ptr<detail::Socket> socket);
    std::unique_ptr<detail::Socket> socket_;
};

class IOXX_API TcpStream final : public Source {
public:
    TcpStream();
    explicit TcpStream(native_handle_type handle);
    static TcpStream connect(const SocketAddr& endpoint);
    static TcpStream connect(const SocketAddr& endpoint, std::error_code& ec) noexcept;

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    TcpStream(TcpStream&& other) noexcept;
    TcpStream& operator=(TcpStream&& other) noexcept;
    ~TcpStream() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SocketAddr local_endpoint(std::error_code& ec) const noexcept;
    [[nodiscard]] SocketAddr local_endpoint() const;
    [[nodiscard]] SocketAddr peer_endpoint(std::error_code& ec) const noexcept;
    [[nodiscard]] SocketAddr peer_endpoint() const;
    std::size_t read(void* data, std::size_t size) const;
    std::size_t read(void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t read(std::span<std::byte> buffer) const;
    std::size_t read(std::span<std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t read_vectored(std::span<IoSliceMut> bufs) const;
    std::size_t read_vectored(std::span<IoSliceMut> bufs, std::error_code& ec) const noexcept;
    std::size_t write(const void* data, std::size_t size) const;
    std::size_t write(const void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t write(std::span<const std::byte> buffer) const;
    std::size_t write(std::span<const std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t write(std::string_view text) const;
    std::size_t write(std::string_view text, std::error_code& ec) const noexcept;
    std::size_t write_vectored(std::span<IoSlice> bufs) const;
    std::size_t write_vectored(std::span<IoSlice> bufs, std::error_code& ec) const noexcept;

    void shutdown_read() const;
    void shutdown_read(std::error_code& ec) const noexcept;
    void shutdown_write() const;
    void shutdown_write(std::error_code& ec) const noexcept;
    void shutdown_both() const;
    void shutdown_both(std::error_code& ec) const noexcept;

    void set_nodelay(bool enabled) const;
    void set_nodelay(bool enabled, std::error_code& ec) const noexcept;
    [[nodiscard]] bool nodelay() const;
    [[nodiscard]] bool nodelay(std::error_code& ec) const noexcept;
    void set_ttl(std::uint32_t ttl, std::error_code& ec) const noexcept;
    void set_ttl(std::uint32_t ttl) const;
    [[nodiscard]] std::uint32_t ttl(std::error_code& ec) const noexcept;
    [[nodiscard]] std::uint32_t ttl() const;
    [[nodiscard]] std::optional<std::error_code> take_error() const;
    [[nodiscard]] bool valid() const noexcept;

private:
    friend class TcpListener;
    friend class TcpSocket;

    explicit TcpStream(std::unique_ptr<detail::Socket> socket);
    void on_registered(const Registry& registry, Token token, Interest interests) override;
    void on_reregistered(const Registry& registry, Token token, Interest interests) override;
    void on_deregistered(const Registry& registry) override;
    void rearm_after_would_block() const;

    std::unique_ptr<detail::Socket> socket_;
    mutable std::optional<Registry> registry_;
    mutable Token token_{};
    mutable Interest interests_{};
};

class IOXX_API TcpListener final : public Source {
public:
    TcpListener();
    explicit TcpListener(native_handle_type handle);
    static TcpListener bind(const SocketAddr& endpoint, int backlog = 128);
    static TcpListener bind(const SocketAddr& endpoint, int backlog, std::error_code& ec) noexcept;
    static TcpListener bind(const SocketAddr& endpoint, std::error_code& ec) noexcept;

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    TcpListener(TcpListener&& other) noexcept;
    TcpListener& operator=(TcpListener&& other) noexcept;
    ~TcpListener() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;
    [[nodiscard]] SocketAddr local_endpoint(std::error_code& ec) const noexcept;
    [[nodiscard]] SocketAddr local_endpoint() const;
    [[nodiscard]] std::pair<TcpStream, SocketAddr> accept() const;
    [[nodiscard]] std::pair<TcpStream, SocketAddr> accept(std::error_code& ec) const noexcept;

    void set_ttl(std::uint32_t ttl, std::error_code& ec) const noexcept;
    void set_ttl(std::uint32_t ttl) const;
    [[nodiscard]] std::uint32_t ttl(std::error_code& ec) const noexcept;
    [[nodiscard]] std::uint32_t ttl() const;
    [[nodiscard]] std::optional<std::error_code> take_error() const;
    [[nodiscard]] bool valid() const noexcept;

private:
    friend class TcpSocket;

    explicit TcpListener(std::unique_ptr<detail::Socket> socket);
    void on_registered(const Registry& registry, Token token, Interest interests) override;
    void on_reregistered(const Registry& registry, Token token, Interest interests) override;
    void on_deregistered(const Registry& registry) override;
    void rearm_after_would_block() const;

    std::unique_ptr<detail::Socket> socket_;
    mutable std::optional<Registry> registry_;
    mutable Token token_{};
    mutable Interest interests_{};
};

class IOXX_API UdpSocket final : public Source {
public:
    UdpSocket();
    explicit UdpSocket(native_handle_type handle);
    static UdpSocket bind(const SocketAddr& endpoint);
    static UdpSocket bind(const SocketAddr& endpoint, std::error_code& ec) noexcept;

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    ~UdpSocket() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;
    [[nodiscard]] SocketAddr local_endpoint(std::error_code& ec) const noexcept;
    [[nodiscard]] SocketAddr local_endpoint() const;
    [[nodiscard]] SocketAddr peer_endpoint(std::error_code& ec) const noexcept;
    [[nodiscard]] SocketAddr peer_endpoint() const;

    void connect(const SocketAddr& endpoint) const;
    void connect(const SocketAddr& endpoint, std::error_code& ec) const noexcept;
    std::size_t send(const void* data, std::size_t size) const;
    std::size_t send(const void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t send(std::span<const std::byte> buffer) const;
    std::size_t send(std::span<const std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t send(std::string_view text) const;
    std::size_t send(std::string_view text, std::error_code& ec) const noexcept;
    std::size_t recv(void* data, std::size_t size) const;
    std::size_t recv(void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::size_t recv(std::span<std::byte> buffer) const;
    std::size_t recv(std::span<std::byte> buffer, std::error_code& ec) const noexcept;
    std::size_t send_to(const void* data, std::size_t size, const SocketAddr& endpoint) const;
    std::size_t send_to(const void* data, std::size_t size, const SocketAddr& endpoint, std::error_code& ec) const noexcept;
    std::size_t send_to(std::span<const std::byte> buffer, const SocketAddr& endpoint) const;
    std::size_t send_to(std::span<const std::byte> buffer, const SocketAddr& endpoint, std::error_code& ec) const noexcept;
    std::pair<std::size_t, SocketAddr> recv_from(void* data, std::size_t size) const;
    std::pair<std::size_t, SocketAddr> recv_from(void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::pair<std::size_t, SocketAddr> recv_from(std::span<std::byte> buffer) const;
    std::pair<std::size_t, SocketAddr> recv_from(std::span<std::byte> buffer, std::error_code& ec) const noexcept;
    std::pair<std::size_t, SocketAddr> peek_from(void* data, std::size_t size, std::error_code& ec) const noexcept;
    std::pair<std::size_t, SocketAddr> peek_from(void* data, std::size_t size) const;

    std::size_t send_vectored_to(std::span<const IoSlice> buffers, const SocketAddr& endpoint, std::error_code& ec) const noexcept;
    std::size_t send_vectored_to(std::span<const IoSlice> buffers, const SocketAddr& endpoint) const;
    std::pair<std::size_t, SocketAddr> recv_vectored_from(std::span<IoSliceMut> buffers, std::error_code& ec) const noexcept;
    std::pair<std::size_t, SocketAddr> recv_vectored_from(std::span<IoSliceMut> buffers) const;

    void set_ttl(std::uint32_t ttl, std::error_code& ec) const noexcept;
    void set_ttl(std::uint32_t ttl) const;
    [[nodiscard]] std::uint32_t ttl(std::error_code& ec) const noexcept;
    [[nodiscard]] std::uint32_t ttl() const;
    void set_broadcast(bool enabled, std::error_code& ec) const noexcept;
    void set_broadcast(bool enabled) const;
    [[nodiscard]] bool broadcast(std::error_code& ec) const noexcept;
    [[nodiscard]] bool broadcast() const;

    /// Sets the Don't Fragment (DF) flag on the socket.
    ///
    /// This is used for Path MTU Discovery (PMTUD), primarily for protocols like WebRTC or QUIC.
    /// - **Linux**: Sets `IP_MTU_DISCOVER` to `IP_PMTUDISC_DO`.
    /// - **Windows**: Sets `IP_DONTFRAGMENT` to 1.
    /// - **Apple**: Sets `IP_DONTFRAG` to 1.
    void set_dont_fragment(bool enabled, std::error_code& ec) const noexcept;
    void set_dont_fragment(bool enabled) const;

    /// Gets the discovered Path MTU for the socket.
    ///
    /// - **Linux/Windows**: Reads `IP_MTU`. Note that on Windows, the socket MUST be connected
    ///   (`connect()`) to retrieve the MTU. On Linux, it often also requires connection or 
    ///   previous traffic.
    /// - **Apple**: Not supported natively via `getsockopt`. Will return `function_not_supported` error.
    [[nodiscard]] std::uint32_t mtu(std::error_code& ec) const noexcept;
    [[nodiscard]] std::uint32_t mtu() const;

    [[nodiscard]] std::optional<std::error_code> take_error() const;
    [[nodiscard]] bool valid() const noexcept;

private:
    explicit UdpSocket(std::unique_ptr<detail::Socket> socket);
    void on_registered(const Registry& registry, Token token, Interest interests) override;
    void on_reregistered(const Registry& registry, Token token, Interest interests) override;
    void on_deregistered(const Registry& registry) override;
    void rearm_after_would_block() const;

    std::unique_ptr<detail::Socket> socket_;
    mutable std::optional<Registry> registry_;
    mutable Token token_{};
    mutable Interest interests_{};
};

} // namespace ioxx::net

#pragma once

#include <ioxx/export.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

namespace ioxx {

#if defined(_WIN32)
using native_handle_type = std::uintptr_t;
inline constexpr native_handle_type invalid_native_handle = ~native_handle_type{0};
#else
using native_handle_type = int;
inline constexpr native_handle_type invalid_native_handle = -1;
#endif

namespace detail {
class Selector;
class WakerState;
}

class Registry;

class IOXX_API Token {
public:
    constexpr Token() noexcept = default;
    constexpr explicit Token(std::size_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::size_t value() const noexcept { return value_; }
    constexpr explicit operator std::size_t() const noexcept { return value_; }

    friend constexpr bool operator==(Token lhs, Token rhs) noexcept = default;

private:
    std::size_t value_{};
};

class IOXX_API Interest {
private:
    constexpr explicit Interest(std::uint8_t bits) noexcept : bits_(bits) {}

public:
    constexpr Interest() noexcept = default;

    static const Interest READABLE;
    static const Interest WRITABLE;
    static const Interest AIO;
    static const Interest LIO;
    static const Interest PRIORITY;

    [[nodiscard]] constexpr Interest add(Interest other) const noexcept {
        return Interest{static_cast<std::uint8_t>(bits_ | other.bits_)};
    }

    [[nodiscard]] constexpr std::optional<Interest> remove(Interest other) const noexcept {
        const auto bits = static_cast<std::uint8_t>(bits_ & ~other.bits_);
        if (bits == 0) {
            return std::nullopt;
        }
        return Interest{bits};
    }

    [[nodiscard]] constexpr bool is_readable() const noexcept { return (bits_ & READABLE.bits_) != 0; }
    [[nodiscard]] constexpr bool is_writable() const noexcept { return (bits_ & WRITABLE.bits_) != 0; }
    [[nodiscard]] constexpr bool is_aio() const noexcept { return (bits_ & AIO.bits_) != 0; }
    [[nodiscard]] constexpr bool is_lio() const noexcept { return (bits_ & LIO.bits_) != 0; }
    [[nodiscard]] constexpr bool is_priority() const noexcept { return (bits_ & PRIORITY.bits_) != 0; }
    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr std::uint8_t bits() const noexcept { return bits_; }

    friend constexpr bool operator==(Interest lhs, Interest rhs) noexcept = default;

private:
    std::uint8_t bits_{};
};

inline constexpr Interest Interest::READABLE{0b0000'0001};
inline constexpr Interest Interest::WRITABLE{0b0000'0010};
inline constexpr Interest Interest::AIO{0b0000'0100};
inline constexpr Interest Interest::LIO{0b0000'1000};
inline constexpr Interest Interest::PRIORITY{0b0001'0000};

constexpr Interest operator|(Interest lhs, Interest rhs) noexcept {
    return lhs.add(rhs);
}

constexpr Interest& operator|=(Interest& lhs, Interest rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

class IOXX_API Event {
public:
    constexpr Event() noexcept = default;

    [[nodiscard]] constexpr Token token() const noexcept { return token_; }
    [[nodiscard]] constexpr bool is_readable() const noexcept { return readiness_.is_readable(); }
    [[nodiscard]] constexpr bool is_writable() const noexcept { return readiness_.is_writable(); }
    [[nodiscard]] constexpr bool is_aio() const noexcept { return readiness_.is_aio(); }
    [[nodiscard]] constexpr bool is_lio() const noexcept { return readiness_.is_lio(); }
    [[nodiscard]] constexpr bool is_priority() const noexcept { return readiness_.is_priority(); }
    [[nodiscard]] constexpr bool is_error() const noexcept { return error_; }
    [[nodiscard]] constexpr bool is_read_closed() const noexcept { return read_closed_; }
    [[nodiscard]] constexpr bool is_write_closed() const noexcept { return write_closed_; }

private:
    friend class Events;
    friend class detail::Selector;

    Event(Token token, Interest readiness, bool error = false, bool read_closed = false,
          bool write_closed = false) noexcept;

    Token token_{};
    Interest readiness_{};
    bool error_{false};
    bool read_closed_{false};
    bool write_closed_{false};
};

namespace detail {
class Selector;
}

class IOXX_API Events {
public:
    using container_type = std::vector<Event>;
    using const_iterator = container_type::const_iterator;

    explicit Events(std::size_t capacity);

    [[nodiscard]] std::size_t capacity() const noexcept { return events_.capacity(); }
    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }
    [[nodiscard]] bool is_empty() const noexcept { return events_.empty(); }

    void clear() noexcept { events_.clear(); }

    [[nodiscard]] const Event& operator[](std::size_t index) const { return events_.at(index); }
    [[nodiscard]] const_iterator begin() const noexcept { return events_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return events_.end(); }

private:
    friend class detail::Selector;

    void push(Token token, Interest readiness, bool error = false, bool read_closed = false,
              bool write_closed = false);

    container_type events_;
};

enum class SourceKind {
    socket,
    handle,
    raw_descriptor,
    waker,
};

class Registry;

class IOXX_API Source {
public:
    virtual ~Source() = default;

    [[nodiscard]] virtual native_handle_type native_handle() const noexcept = 0;
    [[nodiscard]] virtual SourceKind source_kind() const noexcept { return SourceKind::raw_descriptor; }

protected:
    friend class Registry;

    virtual void on_registered(const Registry& registry, Token token, Interest interests) {
        (void)registry;
        (void)token;
        (void)interests;
    }
    virtual void on_reregistered(const Registry& registry, Token token, Interest interests) {
        (void)registry;
        (void)token;
        (void)interests;
    }
    virtual void on_deregistered(const Registry& registry) {
        (void)registry;
    }
};

class IOXX_API Registry {
public:
    Registry(const Registry& other) noexcept = default;
    Registry& operator=(const Registry& other) noexcept = default;
    Registry(Registry&& other) noexcept = default;
    Registry& operator=(Registry&& other) noexcept = default;
    ~Registry();

    void register_source(Source& source, Token token, Interest interests) const;
    void register_source(Source& source, Token token, Interest interests, std::error_code& ec) const noexcept;
    void reregister(Source& source, Token token, Interest interests) const;
    void reregister(Source& source, Token token, Interest interests, std::error_code& ec) const noexcept;
    void deregister(Source& source) const;
    void deregister(Source& source, std::error_code& ec) const noexcept;
    void register_handle(native_handle_type handle, SourceKind kind, Token token,
                         Interest interests) const;
    void register_handle(native_handle_type handle, SourceKind kind, Token token,
                         Interest interests, std::error_code& ec) const noexcept;
    void reregister_handle(native_handle_type handle, SourceKind kind, Token token,
                           Interest interests) const;
    void reregister_handle(native_handle_type handle, SourceKind kind, Token token,
                           Interest interests, std::error_code& ec) const noexcept;
    void deregister_handle(native_handle_type handle) const;
    void deregister_handle(native_handle_type handle, std::error_code& ec) const noexcept;

    [[nodiscard]] Registry try_clone() const noexcept { return *this; }

private:
    friend class Poll;
    friend class Waker;

    explicit Registry(std::shared_ptr<detail::Selector> selector) noexcept;

    std::shared_ptr<detail::Selector> selector_;
};

class IOXX_API Poll {
public:
    Poll();
    explicit Poll(std::error_code& ec) noexcept;
    Poll(const Poll&) = delete;
    Poll& operator=(const Poll&) = delete;
    Poll(Poll&& other) noexcept;
    Poll& operator=(Poll&& other) noexcept;
    ~Poll();

    [[nodiscard]] Registry& registry() noexcept { return registry_; }
    [[nodiscard]] const Registry& registry() const noexcept { return registry_; }

    void poll(Events& events, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    void poll(Events& events, std::optional<std::chrono::milliseconds> timeout, std::error_code& ec) noexcept;

private:
    std::shared_ptr<detail::Selector> selector_;
    Registry registry_;
};

class IOXX_API Waker {
public:
    Waker(const Registry& registry, Token token);
    Waker(const Registry& registry, Token token, std::error_code& ec) noexcept;
    Waker(const Waker&) = delete;
    Waker& operator=(const Waker&) = delete;
    Waker(Waker&& other) noexcept;
    Waker& operator=(Waker&& other) noexcept;
    ~Waker();

    void wake() const;
    void wake(std::error_code& ec) const noexcept;

private:
    std::unique_ptr<detail::WakerState> state_;
};

IOXX_API bool is_would_block(const std::error_code& error) noexcept;

} // namespace ioxx

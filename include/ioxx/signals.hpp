#pragma once

#include <ioxx/core.hpp>

#include <system_error>
#include <vector>
#include <memory>

namespace ioxx {

/// Signal handling integration for the event loop.
/// 
/// Allows receiving OS signals (like SIGINT or SIGTERM) as standard readable events
/// inside the Poll loop.
/// 
/// - **Linux**: Uses `signalfd` directly.
/// - **Windows / macOS**: Uses a global signal handler and the "Self-Pipe Trick".
class IOXX_API Signals final : public Source {
public:
    Signals();
    explicit Signals(std::error_code& ec) noexcept;
    ~Signals() override;

    Signals(const Signals&) = delete;
    Signals& operator=(const Signals&) = delete;
    Signals(Signals&& other) noexcept;
    Signals& operator=(Signals&& other) noexcept;

    /// Register a signal to be caught.
    void add(int signum, std::error_code& ec) noexcept;
    void add(int signum);

    /// Unregister a signal.
    void remove(int signum, std::error_code& ec) noexcept;
    void remove(int signum);

    /// Consume and return all pending signals that have fired.
    /// Does not block. Returns an empty vector if no signals are pending.
    std::vector<int> pending(std::error_code& ec) noexcept;
    std::vector<int> pending();

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;

private:
    void on_registered(const Registry& registry, Token token, Interest interests) override;
    void on_reregistered(const Registry& registry, Token token, Interest interests) override;
    void on_deregistered(const Registry& registry) override;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ioxx

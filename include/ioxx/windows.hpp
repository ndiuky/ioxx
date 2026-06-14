#pragma once

#if defined(_WIN32)

#include <ioxx/core.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <mutex>
#include <vector>

#include <windows.h>

namespace ioxx::windows {

class IOXX_API NamedPipe final : public Source {
public:
    NamedPipe() noexcept;
    explicit NamedPipe(HANDLE handle) noexcept;
    static NamedPipe create(std::wstring_view name, std::error_code& ec) noexcept;
    static NamedPipe create(std::wstring_view name);
    static NamedPipe open_client(std::wstring_view name, std::error_code& ec) noexcept;
    static NamedPipe open_client(std::wstring_view name);

    NamedPipe(const NamedPipe&) = delete;
    NamedPipe& operator=(const NamedPipe&) = delete;
    NamedPipe(NamedPipe&& other) noexcept;
    NamedPipe& operator=(NamedPipe&& other) noexcept;
    ~NamedPipe() override;

    [[nodiscard]] native_handle_type native_handle() const noexcept override;
    [[nodiscard]] SourceKind source_kind() const noexcept override;
    [[nodiscard]] HANDLE native_handle_raw() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] HANDLE release() noexcept;

    void connect(std::error_code& ec);
    void connect();
    void disconnect(std::error_code& ec);
    void disconnect();
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
    [[nodiscard]] std::error_code take_error() const noexcept;

private:
    bool complete_connect_locked() const;
    bool complete_read_locked() const;
    bool complete_write_locked() const;
    void close() noexcept;

    HANDLE handle_{INVALID_HANDLE_VALUE};
    mutable OVERLAPPED connect_overlapped_{};
    mutable OVERLAPPED read_overlapped_{};
    mutable OVERLAPPED write_overlapped_{};
    mutable std::mutex io_mutex_;
    mutable bool connect_pending_{};
    mutable bool read_pending_{};
    mutable bool write_pending_{};
    mutable std::vector<std::byte> read_buffer_;
    mutable std::size_t read_ready_size_{};
    mutable std::size_t read_pos_{};
    mutable std::vector<std::byte> write_buffer_;
    mutable std::error_code last_error_;
};

[[nodiscard]] IOXX_API std::wstring pipe_name(std::wstring_view local_name);

} // namespace ioxx::windows

#endif

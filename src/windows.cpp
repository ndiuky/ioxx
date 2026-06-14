#include <ioxx/windows.hpp>

#if defined(_WIN32)

#include <algorithm>
#include <cstring>
#include <fileapi.h>
#include <string>

namespace {

std::system_error win_error(const char* operation, DWORD error = ::GetLastError()) {
    return std::system_error(static_cast<int>(error), std::system_category(), operation);
}

bool is_valid(HANDLE handle) noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

std::size_t checked_size(std::size_t size) {
    return std::min<std::size_t>(size, std::numeric_limits<DWORD>::max());
}

std::system_error would_block(const char* operation) {
    return std::system_error(std::make_error_code(std::errc::operation_would_block), operation);
}

} // namespace

namespace ioxx::windows {

NamedPipe::NamedPipe() noexcept = default;

NamedPipe::NamedPipe(HANDLE handle) noexcept : handle_(handle) {}

NamedPipe NamedPipe::create(std::wstring_view name, std::error_code& ec) noexcept {
    const std::wstring owned_name{name};
    HANDLE handle = ::CreateNamedPipeW(
        owned_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0,
        nullptr);
    if (!is_valid(handle)) {
        ec = std::error_code{static_cast<int>(::GetLastError()), std::system_category()};
    } else {
        ec.clear();
    }
    return NamedPipe{handle};
}

NamedPipe NamedPipe::create(std::wstring_view name) {
    std::error_code ec;
    auto res = create(name, ec);
    if (ec) throw std::system_error(ec, "CreateNamedPipeW");
    return res;
}

NamedPipe NamedPipe::open_client(std::wstring_view name, std::error_code& ec) noexcept {
    const std::wstring owned_name{name};
    HANDLE handle = ::CreateFileW(owned_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                  nullptr);
    if (!is_valid(handle)) {
        ec = std::error_code{static_cast<int>(::GetLastError()), std::system_category()};
    } else {
        ec.clear();
    }
    return NamedPipe{handle};
}

NamedPipe NamedPipe::open_client(std::wstring_view name) {
    std::error_code ec;
    auto res = open_client(name, ec);
    if (ec) throw std::system_error(ec, "CreateFileW(named pipe)");
    return res;
}

NamedPipe::NamedPipe(NamedPipe&& other) noexcept : handle_(other.release()) {}

NamedPipe& NamedPipe::operator=(NamedPipe&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.release();
    }
    return *this;
}

NamedPipe::~NamedPipe() {
    close();
}

native_handle_type NamedPipe::native_handle() const noexcept {
    return reinterpret_cast<native_handle_type>(handle_);
}

SourceKind NamedPipe::source_kind() const noexcept {
    return SourceKind::handle;
}

HANDLE NamedPipe::native_handle_raw() const noexcept {
    return handle_;
}

bool NamedPipe::valid() const noexcept {
    return is_valid(handle_);
}

HANDLE NamedPipe::release() noexcept {
    const HANDLE handle = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return handle;
}

void NamedPipe::connect(std::error_code& ec) {
    if (!valid()) {
        ec = std::make_error_code(std::errc::bad_file_descriptor);
        return;
    }

    std::lock_guard lock{io_mutex_};
    if (connect_pending_) {
        if (complete_connect_locked()) {
            ec.clear();
            return;
        }
        ec = std::make_error_code(std::errc::operation_would_block);
        return;
    }

    std::memset(&connect_overlapped_, 0, sizeof(connect_overlapped_));
    if (::ConnectNamedPipe(handle_, &connect_overlapped_) != 0) {
        ec.clear();
        return;
    }

    const DWORD error = ::GetLastError();
    if (error == ERROR_PIPE_CONNECTED || error == ERROR_NO_DATA) {
        ec.clear();
        return;
    }
    if (error == ERROR_IO_PENDING) {
        connect_pending_ = true;
        ec = std::make_error_code(std::errc::operation_would_block);
        return;
    }
    ec = std::error_code{static_cast<int>(error), std::system_category()};
    last_error_ = ec;
}

void NamedPipe::connect() {
    std::error_code ec;
    connect(ec);
    if (ec) throw std::system_error(ec, "ConnectNamedPipe");
}

void NamedPipe::disconnect(std::error_code& ec) {
    if (!::DisconnectNamedPipe(handle_)) {
        ec = std::error_code{static_cast<int>(::GetLastError()), std::system_category()};
    } else {
        ec.clear();
    }
}

void NamedPipe::disconnect() {
    std::error_code ec;
    disconnect(ec);
    if (ec) throw std::system_error(ec, "DisconnectNamedPipe");
}

std::size_t NamedPipe::read(void* data, std::size_t size, std::error_code& ec) const noexcept {
    if (!valid()) {
        ec = std::make_error_code(std::errc::bad_file_descriptor);
        return 0;
    }

    std::lock_guard lock{io_mutex_};

    auto copy_ready = [&]() -> std::size_t {
        const std::size_t available = read_ready_size_ - read_pos_;
        const std::size_t copied = std::min(size, available);
        if (copied != 0) {
            std::memcpy(data, read_buffer_.data() + read_pos_, copied);
            read_pos_ += copied;
        }
        if (read_pos_ == read_ready_size_) {
            read_ready_size_ = 0;
            read_pos_ = 0;
        }
        ec.clear();
        return copied;
    };

    if (read_ready_size_ > read_pos_) {
        return copy_ready();
    }

    if (read_pending_) {
        try {
            if (!complete_read_locked()) {
                ec = std::make_error_code(std::errc::operation_would_block);
                return 0;
            }
        } catch(const std::system_error& e) {
            ec = e.code();
            return 0;
        }
        return copy_ready();
    }

    try {
        read_buffer_.assign(65536, std::byte{});
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return 0;
    }

    DWORD read = 0;
    std::memset(&read_overlapped_, 0, sizeof(read_overlapped_));
    if (::ReadFile(handle_, read_buffer_.data(), static_cast<DWORD>(read_buffer_.size()), &read,
                   &read_overlapped_) != 0) {
        read_ready_size_ = read;
        read_pos_ = 0;
        return copy_ready();
    }

    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_PENDING) {
        read_pending_ = true;
        ec = std::make_error_code(std::errc::operation_would_block);
        return 0;
    }
    ec = std::error_code{static_cast<int>(error), std::system_category()};
    last_error_ = ec;
    return 0;
}

std::size_t NamedPipe::read(void* data, std::size_t size) const {
    std::error_code ec;
    auto res = read(data, size, ec);
    if (ec) throw std::system_error(ec, "ReadFile(named pipe)");
    return res;
}

std::size_t NamedPipe::read(std::span<std::byte> buffer, std::error_code& ec) const noexcept {
    return read(buffer.data(), buffer.size(), ec);
}

std::size_t NamedPipe::read(std::span<std::byte> buffer) const {
    return read(buffer.data(), buffer.size());
}

std::size_t NamedPipe::write(const void* data, std::size_t size, std::error_code& ec) const noexcept {
    if (!valid()) {
        ec = std::make_error_code(std::errc::bad_file_descriptor);
        return 0;
    }

    std::lock_guard lock{io_mutex_};
    if (write_pending_) {
        try {
            if (!complete_write_locked()) {
                ec = std::make_error_code(std::errc::operation_would_block);
                return 0;
            }
        } catch(const std::system_error& e) {
            ec = e.code();
            return 0;
        }
    }

    const auto bytes = static_cast<const std::byte*>(data);
    try {
        write_buffer_.assign(bytes, bytes + size);
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return 0;
    }

    DWORD written = 0;
    std::memset(&write_overlapped_, 0, sizeof(write_overlapped_));
    if (::WriteFile(handle_, write_buffer_.data(), static_cast<DWORD>(checked_size(write_buffer_.size())), &written,
                    &write_overlapped_) != 0) {
        write_buffer_.clear();
        ec.clear();
        return written;
    }

    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_PENDING) {
        write_pending_ = true;
        ec.clear();
        return size;
    }
    write_buffer_.clear();
    ec = std::error_code{static_cast<int>(error), std::system_category()};
    last_error_ = ec;
    return 0;
}

std::size_t NamedPipe::write(const void* data, std::size_t size) const {
    std::error_code ec;
    auto res = write(data, size, ec);
    if (ec) throw std::system_error(ec, "WriteFile(named pipe)");
    return res;
}

std::size_t NamedPipe::write(std::span<const std::byte> buffer, std::error_code& ec) const noexcept {
    return write(buffer.data(), buffer.size(), ec);
}

std::size_t NamedPipe::write(std::span<const std::byte> buffer) const {
    return write(buffer.data(), buffer.size());
}

std::size_t NamedPipe::write(std::string_view text, std::error_code& ec) const noexcept {
    return write(text.data(), text.size(), ec);
}

std::size_t NamedPipe::write(std::string_view text) const {
    return write(text.data(), text.size());
}

std::error_code NamedPipe::take_error() const noexcept {
    std::lock_guard lock{io_mutex_};
    auto error = last_error_;
    last_error_.clear();
    return error;
}

bool NamedPipe::complete_connect_locked() const {
    DWORD transferred = 0;
    if (::GetOverlappedResult(handle_, &connect_overlapped_, &transferred, FALSE) != 0) {
        connect_pending_ = false;
        return true;
    }
    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_INCOMPLETE) {
        return false;
    }
    connect_pending_ = false;
    last_error_ = std::error_code{static_cast<int>(error), std::system_category()};
    throw win_error("GetOverlappedResult(connect)", error);
}

bool NamedPipe::complete_read_locked() const {
    DWORD transferred = 0;
    if (::GetOverlappedResult(handle_, &read_overlapped_, &transferred, FALSE) != 0) {
        read_pending_ = false;
        read_ready_size_ = transferred;
        read_pos_ = 0;
        return true;
    }
    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_INCOMPLETE) {
        return false;
    }
    read_pending_ = false;
    read_ready_size_ = 0;
    read_pos_ = 0;
    last_error_ = std::error_code{static_cast<int>(error), std::system_category()};
    throw win_error("GetOverlappedResult(read)", error);
}

bool NamedPipe::complete_write_locked() const {
    DWORD transferred = 0;
    if (::GetOverlappedResult(handle_, &write_overlapped_, &transferred, FALSE) != 0) {
        (void)transferred;
        write_pending_ = false;
        write_buffer_.clear();
        return true;
    }
    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_INCOMPLETE) {
        return false;
    }
    write_pending_ = false;
    write_buffer_.clear();
    last_error_ = std::error_code{static_cast<int>(error), std::system_category()};
    throw win_error("GetOverlappedResult(write)", error);
}

void NamedPipe::close() noexcept {
    if (is_valid(handle_)) {
        ::CancelIoEx(handle_, nullptr);
        ::CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

std::wstring pipe_name(std::wstring_view local_name) {
    std::wstring result = LR"(\\.\pipe\)";
    result.append(local_name);
    return result;
}

} // namespace ioxx::windows

#endif

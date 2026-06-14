#include <ioxx/core.hpp>

#include "socket.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#include <mswsock.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
#include <sys/event.h>
#include <sys/time.h>
#endif
#endif

namespace ioxx::detail {

namespace {

#if defined(_WIN32)
struct AfdSocketState;
#endif

struct Registration {
    native_handle_type handle{};
    SourceKind kind{SourceKind::socket};
    Token token{};
    Interest interests{};
    bool auto_drain{};
#if defined(_WIN32)
    std::shared_ptr<AfdSocketState> afd_state;
#endif
};

[[nodiscard]] std::chrono::milliseconds clamp_timeout(
    std::optional<std::chrono::milliseconds> timeout) noexcept {
    if (!timeout.has_value()) {
        return std::chrono::milliseconds{-1};
    }
    if (timeout->count() < 0) {
        return std::chrono::milliseconds{0};
    }
    return *timeout;
}

#if !defined(_WIN32)

void close_descriptor(int fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
    }
}

void set_descriptor_non_blocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl(F_GETFL)");
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl(F_SETFL)");
    }
}

void set_descriptor_close_on_exec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl(F_GETFD)");
    }
    if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl(F_SETFD)");
    }
}

void drain_descriptor(native_handle_type handle) noexcept {
    std::array<char, 256> buffer{};
    for (;;) {
        const auto n = ::read(handle, buffer.data(), buffer.size());
        if (n > 0) {
            continue;
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }
        return;
    }
}

class OwnedDescriptor {
public:
    OwnedDescriptor() noexcept = default;
    explicit OwnedDescriptor(int fd) noexcept : fd_(fd) {}
    OwnedDescriptor(const OwnedDescriptor&) = delete;
    OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;
    OwnedDescriptor(OwnedDescriptor&& other) noexcept : fd_(other.release()) {}
    OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    ~OwnedDescriptor() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    [[nodiscard]] int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }
    void reset(int fd = -1) noexcept {
        if (fd_ != fd) {
            close_descriptor(fd_);
            fd_ = fd;
        }
    }

private:
    int fd_{-1};
};

#endif

#if defined(_WIN32)

using NtStatus = LONG;

constexpr NtStatus status_success = 0;
constexpr NtStatus status_pending = 0x00000103L;
constexpr NtStatus status_cancelled = static_cast<NtStatus>(0xC0000120L);
constexpr NtStatus status_not_found = static_cast<NtStatus>(0xC0000225L);
constexpr ULONG file_open = 0x00000001;
constexpr ULONG ioctl_afd_poll = 0x00012024;

constexpr ULONG afd_poll_receive = 0b0'0000'0001;
constexpr ULONG afd_poll_receive_expedited = 0b0'0000'0010;
constexpr ULONG afd_poll_send = 0b0'0000'0100;
constexpr ULONG afd_poll_disconnect = 0b0'0000'1000;
constexpr ULONG afd_poll_abort = 0b0'0001'0000;
constexpr ULONG afd_poll_local_close = 0b0'0010'0000;
constexpr ULONG afd_poll_accept = 0b0'1000'0000;
constexpr ULONG afd_poll_connect_fail = 0b1'0000'0000;
constexpr ULONG afd_known_events = afd_poll_receive | afd_poll_receive_expedited | afd_poll_send |
    afd_poll_disconnect | afd_poll_abort | afd_poll_local_close | afd_poll_accept |
    afd_poll_connect_fail;

constexpr ULONG afd_readable_flags = afd_poll_receive | afd_poll_disconnect | afd_poll_accept |
    afd_poll_abort | afd_poll_connect_fail;
constexpr ULONG afd_writable_flags = afd_poll_send | afd_poll_abort | afd_poll_connect_fail;
constexpr ULONG afd_error_flags = afd_poll_connect_fail;
constexpr ULONG afd_read_closed_flags = afd_poll_disconnect | afd_poll_abort | afd_poll_connect_fail;
constexpr ULONG afd_write_closed_flags = afd_poll_abort | afd_poll_connect_fail;

struct NtIoStatusBlock {
    NtStatus status{};
    ULONG_PTR Information{};
};

struct NtUnicodeString {
    USHORT Length{};
    USHORT MaximumLength{};
    PWSTR Buffer{};
};

struct NtObjectAttributes {
    ULONG Length{};
    HANDLE RootDirectory{};
    NtUnicodeString* ObjectName{};
    ULONG Attributes{};
    void* SecurityDescriptor{};
    void* SecurityQualityOfService{};
};

struct AfdPollHandleInfo {
    HANDLE handle{};
    ULONG events{};
    NtStatus status{};
};

struct AfdPollInfo {
    LONGLONG timeout{};
    ULONG number_of_handles{};
    ULONG exclusive{};
    AfdPollHandleInfo handles[1]{};
};

using NtCreateFileFn = NtStatus(NTAPI*)(PHANDLE, ACCESS_MASK, NtObjectAttributes*,
                                        NtIoStatusBlock*, PLARGE_INTEGER, ULONG, ULONG, ULONG,
                                        ULONG, void*, ULONG);
using NtDeviceIoControlFileFn = NtStatus(NTAPI*)(HANDLE, HANDLE, void*, void*, NtIoStatusBlock*,
                                                ULONG, void*, ULONG, void*, ULONG);
using NtCancelIoFileExFn = NtStatus(NTAPI*)(HANDLE, NtIoStatusBlock*, NtIoStatusBlock*);
using RtlNtStatusToDosErrorFn = ULONG(NTAPI*)(NtStatus);

struct NativeNt {
    NtCreateFileFn NtCreateFile{};
    NtDeviceIoControlFileFn NtDeviceIoControlFile{};
    NtCancelIoFileExFn NtCancelIoFileEx{};
    RtlNtStatusToDosErrorFn RtlNtStatusToDosError{};
};

NativeNt& native_nt() {
    static NativeNt functions = [] {
        HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr) {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                    "GetModuleHandleW(ntdll.dll)");
        }

        NativeNt loaded{};
        loaded.NtCreateFile = reinterpret_cast<NtCreateFileFn>(::GetProcAddress(ntdll, "NtCreateFile"));
        loaded.NtDeviceIoControlFile = reinterpret_cast<NtDeviceIoControlFileFn>(
            ::GetProcAddress(ntdll, "NtDeviceIoControlFile"));
        loaded.NtCancelIoFileEx = reinterpret_cast<NtCancelIoFileExFn>(
            ::GetProcAddress(ntdll, "NtCancelIoFileEx"));
        loaded.RtlNtStatusToDosError = reinterpret_cast<RtlNtStatusToDosErrorFn>(
            ::GetProcAddress(ntdll, "RtlNtStatusToDosError"));
        if (!loaded.NtCreateFile || !loaded.NtDeviceIoControlFile || !loaded.NtCancelIoFileEx ||
            !loaded.RtlNtStatusToDosError) {
            throw std::system_error(std::make_error_code(std::errc::function_not_supported),
                                    "missing ntdll native API");
        }
        return loaded;
    }();
    return functions;
}

std::error_code nt_error(NtStatus status) noexcept {
    return {static_cast<int>(native_nt().RtlNtStatusToDosError(status)), std::system_category()};
}

void throw_nt(const char* operation, NtStatus status) {
    throw std::system_error(nt_error(status), operation);
}

ULONG interests_to_afd_flags(Interest interests) noexcept {
    ULONG flags = 0;
    if (interests.is_readable()) {
        flags |= afd_readable_flags | afd_read_closed_flags | afd_error_flags;
    }
    if (interests.is_writable()) {
        flags |= afd_writable_flags | afd_write_closed_flags | afd_error_flags;
    }
    if (interests.is_priority()) {
        flags |= afd_poll_receive_expedited;
    }
    return flags | afd_poll_abort | afd_poll_connect_fail;
}

Interest afd_flags_to_interest(ULONG flags) noexcept {
    Interest readiness{};
    if ((flags & afd_readable_flags) != 0) {
        readiness |= Interest::READABLE;
    }
    if ((flags & afd_writable_flags) != 0) {
        readiness |= Interest::WRITABLE;
    }
    if ((flags & afd_poll_receive_expedited) != 0) {
        readiness |= Interest::PRIORITY;
    }
    return readiness;
}

SOCKET try_get_base_socket(SOCKET socket, DWORD ioctl) {
    SOCKET base_socket = INVALID_SOCKET;
    DWORD bytes = 0;
    if (::WSAIoctl(socket, ioctl, nullptr, 0, &base_socket, sizeof(base_socket), &bytes, nullptr,
                   nullptr) != SOCKET_ERROR) {
        return base_socket;
    }
    return INVALID_SOCKET;
}

SOCKET get_base_socket(SOCKET socket) {
    SOCKET base_socket = try_get_base_socket(socket, SIO_BASE_HANDLE);
    if (base_socket != INVALID_SOCKET) {
        return base_socket;
    }

    for (DWORD ioctl : {SIO_BSP_HANDLE_SELECT, SIO_BSP_HANDLE_POLL, SIO_BSP_HANDLE}) {
        base_socket = try_get_base_socket(socket, ioctl);
        if (base_socket != INVALID_SOCKET && base_socket != socket) {
            return base_socket;
        }
    }

    throw std::system_error(::WSAGetLastError(), std::system_category(), "WSAIoctl(SIO_BASE_HANDLE)");
}

class AfdHelper {
public:
    explicit AfdHelper(HANDLE iocp) {
        std::wstring name = L"\\Device\\Afd\\Mio";
        NtUnicodeString unicode_name{};
        unicode_name.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
        unicode_name.MaximumLength = unicode_name.Length;
        unicode_name.Buffer = name.data();

        NtObjectAttributes attributes{};
        attributes.Length = sizeof(attributes);
        attributes.ObjectName = &unicode_name;

        NtIoStatusBlock iosb{};
        HANDLE handle = INVALID_HANDLE_VALUE;
        const NtStatus status = native_nt().NtCreateFile(
            &handle, SYNCHRONIZE, &attributes, &iosb, nullptr, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            file_open, 0, nullptr, 0);
        if (status != status_success) {
            throw_nt("NtCreateFile(\\Device\\Afd\\Mio)", status);
        }

        handle_ = handle;
        if (::CreateIoCompletionPort(handle_, iocp, afd_completion_key, 0) == nullptr) {
            const DWORD error = ::GetLastError();
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            throw std::system_error(static_cast<int>(error), std::system_category(),
                                    "CreateIoCompletionPort(AFD)");
        }

        if (::SetFileCompletionNotificationModes(handle_, FILE_SKIP_SET_EVENT_ON_HANDLE) == 0) {
            const DWORD error = ::GetLastError();
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            throw std::system_error(static_cast<int>(error), std::system_category(),
                                    "SetFileCompletionNotificationModes(AFD)");
        }
    }

    AfdHelper(const AfdHelper&) = delete;
    AfdHelper& operator=(const AfdHelper&) = delete;

    ~AfdHelper() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
    }

    bool poll(AfdPollInfo& info, NtIoStatusBlock& iosb, void* context) const {
        iosb.status = status_pending;
        const NtStatus status = native_nt().NtDeviceIoControlFile(
            handle_, nullptr, nullptr, context, &iosb, ioctl_afd_poll, &info, sizeof(info), &info,
            sizeof(info));
        if (status == status_success) {
            return true;
        }
        if (status == status_pending) {
            return false;
        }
        throw_nt("NtDeviceIoControlFile(IOCTL_AFD_POLL)", status);
        return false;
    }

    void cancel(NtIoStatusBlock& iosb) const {
        if (iosb.status != status_pending) {
            return;
        }
        NtIoStatusBlock cancel_iosb{};
        const NtStatus status = native_nt().NtCancelIoFileEx(handle_, &iosb, &cancel_iosb);
        if (status == status_success || status == status_not_found) {
            return;
        }
        throw_nt("NtCancelIoFileEx(AFD)", status);
    }

    static constexpr ULONG_PTR afd_completion_key = 2;

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

enum class AfdPollStatus {
    idle,
    pending,
    cancelled,
};

struct AfdSocketState : std::enable_shared_from_this<AfdSocketState> {
    explicit AfdSocketState(native_handle_type socket_handle, std::shared_ptr<AfdHelper> helper)
        : afd(std::move(helper)), socket(socket_handle), base_socket(static_cast<native_handle_type>(get_base_socket(static_cast<SOCKET>(socket_handle)))) {}

    std::mutex mutex;
    std::shared_ptr<AfdHelper> afd;
    native_handle_type socket{};
    native_handle_type base_socket{};
    Token token{};
    Interest interests{};
    ULONG user_events{};
    ULONG pending_events{};
    AfdPollStatus status{AfdPollStatus::idle};
    bool delete_pending{};
    NtIoStatusBlock iosb{};
    AfdPollInfo poll_info{};
};

struct AfdOperation {
    explicit AfdOperation(std::shared_ptr<AfdSocketState> state) : state(std::move(state)) {}
    std::shared_ptr<AfdSocketState> state;
};

#endif

#if defined(__linux__)

[[nodiscard]] std::uint32_t to_epoll_events(Interest interests) noexcept {
    std::uint32_t events = EPOLLERR | EPOLLHUP | EPOLLET;
    if (interests.is_readable()) {
        events |= EPOLLIN | EPOLLRDHUP;
    }
    if (interests.is_writable()) {
        events |= EPOLLOUT;
    }
    if (interests.is_priority()) {
        events |= EPOLLPRI;
    }
    return events;
}

[[nodiscard]] Interest from_epoll_events(std::uint32_t flags) noexcept {
    Interest readiness{};
    if ((flags & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) != 0) {
        readiness |= Interest::READABLE;
    }
    if ((flags & EPOLLOUT) != 0) {
        readiness |= Interest::WRITABLE;
    }
    if ((flags & EPOLLPRI) != 0) {
        readiness |= Interest::PRIORITY;
    }
    return readiness;
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)

void add_kevent(std::vector<kevent>& changes, uintptr_t ident, short filter, unsigned short flags,
                Token token) {
    kevent event{};
    EV_SET(&event, ident, filter, flags, 0, 0, reinterpret_cast<void*>(token.value));
    changes.push_back(event);
}

void append_kqueue_changes(std::vector<kevent>& changes, native_handle_type handle,
                           Interest interests, Token token, unsigned short flags) {
    if (interests.is_readable()) {
        add_kevent(changes, static_cast<uintptr_t>(handle), EVFILT_READ, flags, token);
    }
    if (interests.is_writable()) {
        add_kevent(changes, static_cast<uintptr_t>(handle), EVFILT_WRITE, flags, token);
    }
#if defined(EVFILT_AIO)
    if (interests.is_aio()) {
        add_kevent(changes, static_cast<uintptr_t>(handle), EVFILT_AIO, flags, token);
    }
#endif
#if defined(EVFILT_LIO)
    if (interests.is_lio()) {
        add_kevent(changes, static_cast<uintptr_t>(handle), EVFILT_LIO, flags, token);
    }
#endif
}

[[nodiscard]] Interest from_kqueue_event(const kevent& event) noexcept {
    Interest readiness{};
    if (event.filter == EVFILT_READ) {
        readiness |= Interest::READABLE;
    } else if (event.filter == EVFILT_WRITE) {
        readiness |= Interest::WRITABLE;
#if defined(EVFILT_AIO)
    } else if (event.filter == EVFILT_AIO) {
        readiness |= Interest::AIO;
#endif
#if defined(EVFILT_LIO)
    } else if (event.filter == EVFILT_LIO) {
        readiness |= Interest::LIO;
#endif
    }
    return readiness;
}

[[nodiscard]] timespec to_timespec(std::chrono::milliseconds timeout) noexcept {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - seconds);

    timespec value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(seconds.count());
    value.tv_nsec = static_cast<decltype(value.tv_nsec)>(nanos.count());
    return value;
}

#endif

} // namespace

class Selector {
public:
    Selector() {
        ensure_socket_runtime();
#if defined(_WIN32)
        iocp_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (iocp_ == nullptr) {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                    "CreateIoCompletionPort");
        }
        afd_ = std::make_shared<AfdHelper>(iocp_);
#elif defined(__linux__)
        backend_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (backend_ == -1) {
            throw std::system_error(errno, std::generic_category(), "epoll_create1");
        }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
        backend_ = ::kqueue();
        if (backend_ == -1) {
            throw std::system_error(errno, std::generic_category(), "kqueue");
        }
        set_descriptor_close_on_exec(backend_);
#endif
    }

    Selector(const Selector&) = delete;
    Selector& operator=(const Selector&) = delete;

    ~Selector() {
#if defined(_WIN32)
        afd_.reset();
        if (iocp_ != nullptr) {
            ::CloseHandle(iocp_);
        }
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__) || defined(__DragonFly__)
        close_descriptor(backend_);
#endif
    }

    void add(native_handle_type handle, SourceKind kind, Token token, Interest interests,
             bool auto_drain = false) {
        validate(handle, interests);

        {
            std::lock_guard lock{mutex_};
            const auto [_, inserted] = registrations_.emplace(
                handle, Registration{handle, kind, token, interests, auto_drain});
            if (!inserted) {
                throw std::system_error(std::make_error_code(std::errc::file_exists),
                                        "source is already registered");
            }
        }

        try {
            add_to_backend(handle, kind, token, interests);
        } catch (...) {
            std::lock_guard lock{mutex_};
            registrations_.erase(handle);
            throw;
        }

        cv_.notify_all();
    }

    void update(native_handle_type handle, SourceKind kind, Token token, Interest interests) {
        validate(handle, interests);

        {
            std::lock_guard lock{mutex_};
            auto item = registrations_.find(handle);
            if (item == registrations_.end()) {
                throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory),
                                        "source is not registered");
            }
            item->second.kind = kind;
            item->second.token = token;
            item->second.interests = interests;
        }

        update_backend(handle, kind, token, interests);
        cv_.notify_all();
    }

    void remove(native_handle_type handle) {
        Registration registration{};
        {
            std::lock_guard lock{mutex_};
            auto item = registrations_.find(handle);
            if (item == registrations_.end()) {
                throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory),
                                        "source is not registered");
            }
            registration = item->second;
            registrations_.erase(item);
            if (waker_handle_ == handle) {
                waker_handle_ = invalid_native_handle;
            }
        }

        remove_from_backend(registration);
        cv_.notify_all();
    }

    void add_waker(native_handle_type handle, Token token) {
#if defined(_WIN32)
        (void)handle;
        std::lock_guard lock{mutex_};
        if (waker_active_) {
            throw std::system_error(std::make_error_code(std::errc::device_or_resource_busy),
                                    "only one Waker can be active per Poll");
        }
        waker_active_ = true;
        (void)token;
#else
        add(handle, SourceKind::raw_descriptor, token, Interest::READABLE, true);
        std::lock_guard lock{mutex_};
        waker_handle_ = handle;
#endif
    }

    void remove_waker(native_handle_type handle) noexcept {
#if defined(_WIN32)
        (void)handle;
        std::lock_guard lock{mutex_};
        waker_active_ = false;
#else
        try {
            remove(handle);
        } catch (...) {
        }
#endif
    }

    void post_waker(Token token) const {
#if defined(_WIN32)
        post_packet(token, Interest::READABLE, false, false, false);
#else
        (void)token;
#endif
    }

    void select(Events& events, std::optional<std::chrono::milliseconds> timeout) {
        events.clear();
        if (events.capacity() == 0) {
            return;
        }

#if defined(_WIN32)
        select_iocp(events, timeout);
#elif defined(__linux__)
        select_epoll(events, timeout);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
        select_kqueue(events, timeout);
#else
        select_poll(events, timeout);
#endif
    }

private:
    static void validate(native_handle_type handle, Interest interests) {
#if defined(_WIN32)
        if (handle == invalid_native_handle && handle != 0) {
            throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor),
                                    "invalid native handle");
        }
#else
        if (handle == invalid_native_handle) {
            throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor),
                                    "invalid native handle");
        }
#endif
        if (interests.empty()) {
            throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                                    "interest set must not be empty");
        }
    }

    [[nodiscard]] std::vector<Registration> snapshot() const {
        std::lock_guard lock{mutex_};
        std::vector<Registration> registrations;
        registrations.reserve(registrations_.size());
        for (const auto& [_, registration] : registrations_) {
            registrations.push_back(registration);
        }
        return registrations;
    }

    [[nodiscard]] std::optional<Registration> find(native_handle_type handle) const {
        std::lock_guard lock{mutex_};
        const auto item = registrations_.find(handle);
        if (item == registrations_.end()) {
            return std::nullopt;
        }
        return item->second;
    }

    void add_to_backend(native_handle_type handle, SourceKind kind, Token token, Interest interests) {
#if defined(_WIN32)
        if (kind == SourceKind::socket) {
            auto state = std::make_shared<AfdSocketState>(handle, afd_);
            {
                std::lock_guard lock{mutex_};
                registrations_.at(handle).afd_state = state;
            }
            set_afd_interest(state, token, interests);
        } else if (kind == SourceKind::handle) {
            const auto native = reinterpret_cast<HANDLE>(handle);
            if (::CreateIoCompletionPort(native, iocp_, static_cast<ULONG_PTR>(handle), 0) == nullptr) {
                throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                        "CreateIoCompletionPort(handle)");
            }
        }
#elif defined(__linux__)
        epoll_event event{};
        event.events = to_epoll_events(interests);
        event.data.fd = handle;
        if (::epoll_ctl(backend_, EPOLL_CTL_ADD, handle, &event) == -1) {
            throw std::system_error(errno, std::generic_category(), "epoll_ctl(ADD)");
        }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
        std::vector<kevent> changes;
        append_kqueue_changes(changes, handle, interests, token, EV_ADD | EV_CLEAR);
        if (!changes.empty() && ::kevent(backend_, changes.data(), static_cast<int>(changes.size()),
                                         nullptr, 0, nullptr) == -1) {
            throw std::system_error(errno, std::generic_category(), "kevent(ADD)");
        }
#else
        (void)handle;
        (void)kind;
        (void)token;
        (void)interests;
#endif
    }

    void update_backend(native_handle_type handle, SourceKind kind, Token token, Interest interests) {
#if defined(_WIN32)
        if (kind == SourceKind::socket) {
            const auto registration = find(handle);
            if (registration.has_value() && registration->afd_state) {
                set_afd_interest(registration->afd_state, token, interests);
            }
        }
#elif defined(__linux__)
        epoll_event event{};
        event.events = to_epoll_events(interests);
        event.data.fd = handle;
        if (::epoll_ctl(backend_, EPOLL_CTL_MOD, handle, &event) == -1) {
            throw std::system_error(errno, std::generic_category(), "epoll_ctl(MOD)");
        }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
        std::vector<kevent> changes;
        append_kqueue_changes(changes, handle, Interest::READABLE | Interest::WRITABLE |
                                           Interest::AIO | Interest::LIO | Interest::PRIORITY,
                              token, EV_DELETE);
        append_kqueue_changes(changes, handle, interests, token, EV_ADD | EV_CLEAR);
        if (!changes.empty()) {
            ::kevent(backend_, changes.data(), static_cast<int>(changes.size()), nullptr, 0, nullptr);
        }
#else
        (void)handle;
        (void)kind;
        (void)token;
        (void)interests;
#endif
    }

    void remove_from_backend(const Registration& registration) noexcept {
#if defined(_WIN32)
        if (registration.afd_state) {
            std::lock_guard state_lock{registration.afd_state->mutex};
            registration.afd_state->delete_pending = true;
            if (registration.afd_state->status == AfdPollStatus::pending) {
                try {
                    registration.afd_state->afd->cancel(registration.afd_state->iosb);
                    registration.afd_state->status = AfdPollStatus::cancelled;
                } catch (...) {
                }
            }
        }
#elif defined(__linux__)
        ::epoll_ctl(backend_, EPOLL_CTL_DEL, registration.handle, nullptr);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
        std::vector<kevent> changes;
        append_kqueue_changes(changes, registration.handle, registration.interests, registration.token,
                              EV_DELETE);
        if (!changes.empty()) {
            ::kevent(backend_, changes.data(), static_cast<int>(changes.size()), nullptr, 0, nullptr);
        }
#else
        (void)registration;
#endif
    }

#if defined(_WIN32)
    struct Packet {
        OVERLAPPED overlapped{};
        Token token{};
        Interest readiness{};
        bool error{};
        bool read_closed{};
        bool write_closed{};
    };

    static constexpr ULONG_PTR packet_key = std::numeric_limits<ULONG_PTR>::max();

    void post_packet(Token token, Interest readiness, bool error, bool read_closed,
                     bool write_closed) const {
        auto packet = std::make_unique<Packet>();
        packet->token = token;
        packet->readiness = readiness;
        packet->error = error;
        packet->read_closed = read_closed;
        packet->write_closed = write_closed;
        auto* raw = packet.release();
        if (::PostQueuedCompletionStatus(iocp_, 0, packet_key, &raw->overlapped) == 0) {
            delete raw;
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                    "PostQueuedCompletionStatus");
        }
    }

    void select_iocp(Events& events, std::optional<std::chrono::milliseconds> timeout) {
        const auto timeout_value = clamp_timeout(timeout);
        const DWORD timeout_ms = timeout_value.count() < 0
            ? INFINITE
            : static_cast<DWORD>(std::min<std::int64_t>(timeout_value.count(), INFINITE - 1));

        while (events.size() < events.capacity()) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            const DWORD wait_ms = events.is_empty() ? timeout_ms : 0;
            const BOOL ok = ::GetQueuedCompletionStatus(iocp_, &bytes, &key, &overlapped, wait_ms);
            if (!ok && overlapped == nullptr) {
                const DWORD error = ::GetLastError();
                if (error == WAIT_TIMEOUT) {
                    return;
                }
                throw std::system_error(static_cast<int>(error), std::system_category(),
                                        "GetQueuedCompletionStatus");
            }
            if (key == packet_key && overlapped != nullptr) {
                auto* packet = reinterpret_cast<Packet*>(overlapped);
                std::unique_ptr<Packet> owned{packet};
                events.push(owned->token, owned->readiness, owned->error, owned->read_closed,
                            owned->write_closed);
            } else if (key == AfdHelper::afd_completion_key && overlapped != nullptr) {
                complete_afd_operation(reinterpret_cast<AfdOperation*>(overlapped), events);
            } else {
                const auto registration = find(static_cast<native_handle_type>(key));
                if (registration.has_value()) {
                    const bool failed = ok == 0;
                    (void)bytes;
                    events.push(registration->token, registration->interests, failed, failed, failed);
                }
            }

        }
    }

    void set_afd_interest(const std::shared_ptr<AfdSocketState>& state, Token token,
                          Interest interests) {
        {
            std::lock_guard state_lock{state->mutex};
            state->token = token;
            state->interests = interests;
            state->user_events = interests_to_afd_flags(interests);
        }
        issue_afd_poll(state);
    }

    void issue_afd_poll(const std::shared_ptr<AfdSocketState>& state) {
        auto operation = std::make_unique<AfdOperation>(state);

        {
            std::lock_guard state_lock{state->mutex};
            if (state->delete_pending || state->user_events == 0) {
                return;
            }

            if (state->status == AfdPollStatus::pending) {
                if ((state->user_events & afd_known_events & ~state->pending_events) == 0) {
                    return;
                }
                state->afd->cancel(state->iosb);
                state->status = AfdPollStatus::cancelled;
                state->pending_events = 0;
                return;
            }

            if (state->status == AfdPollStatus::cancelled) {
                return;
            }

            state->poll_info = AfdPollInfo{};
            state->poll_info.timeout = std::numeric_limits<LONGLONG>::max();
            state->poll_info.number_of_handles = 1;
            state->poll_info.exclusive = 0;
            state->poll_info.handles[0].handle = reinterpret_cast<HANDLE>(state->base_socket);
            state->poll_info.handles[0].events = state->user_events | afd_poll_local_close;
            state->poll_info.handles[0].status = 0;

            (void)state->afd->poll(state->poll_info, state->iosb, operation.get());
            state->status = AfdPollStatus::pending;
            state->pending_events = state->user_events;
        }

        (void)operation.release();
    }

    void complete_afd_operation(AfdOperation* raw_operation, Events& events) {
        std::unique_ptr<AfdOperation> operation{raw_operation};
        const auto state = operation->state;
        ULONG afd_events = 0;
        Token token{};
        bool emit = false;
        bool reissue = false;

        {
            std::lock_guard state_lock{state->mutex};
            state->status = AfdPollStatus::idle;
            state->pending_events = 0;

            if (state->delete_pending) {
                return;
            }

            const NtStatus status = state->iosb.status;
            if (status == status_cancelled) {
                reissue = state->user_events != 0;
            } else if (status < 0) {
                afd_events = afd_poll_connect_fail;
            } else if (state->poll_info.number_of_handles < 1) {
                reissue = state->user_events != 0;
            } else if ((state->poll_info.handles[0].events & afd_poll_local_close) != 0) {
                state->delete_pending = true;
                return;
            } else {
                afd_events = state->poll_info.handles[0].events;
            }

            afd_events &= state->user_events;
            if (afd_events != 0) {
                token = state->token;
                state->user_events &= ~afd_events;
                emit = true;
            }
            reissue = reissue || state->user_events != 0;
        }

        if (emit) {
            // printf("complete_afd_operation EMIT: token=%d, afd_events=%lu\n", (int)static_cast<std::size_t>(token), afd_events);
            events.push(token, afd_flags_to_interest(afd_events), (afd_events & afd_error_flags) != 0,
                        (afd_events & afd_read_closed_flags) != 0,
                        (afd_events & afd_write_closed_flags) != 0);
        }

        if (reissue) {
            issue_afd_poll(state);
        }
    }

#endif

#if defined(__linux__)
    void select_epoll(Events& events, std::optional<std::chrono::milliseconds> timeout) {
        std::vector<epoll_event> raw_events(events.capacity());
        const auto timeout_value = clamp_timeout(timeout);
        const int timeout_ms = static_cast<int>(std::min<std::int64_t>(
            timeout_value.count(), std::numeric_limits<int>::max()));
        const int count = ::epoll_wait(backend_, raw_events.data(), static_cast<int>(raw_events.size()),
                                       timeout_ms);
        if (count == -1) {
            if (errno == EINTR) {
                return;
            }
            throw std::system_error(errno, std::generic_category(), "epoll_wait");
        }

        for (int index = 0; index < count; ++index) {
            const auto handle = raw_events[index].data.fd;
            const auto registration = find(handle);
            if (!registration.has_value()) {
                continue;
            }

            const auto flags = raw_events[index].events;
            const bool error = (flags & EPOLLERR) != 0;
            const bool read_closed = (flags & (EPOLLRDHUP | EPOLLHUP)) != 0;
            const bool write_closed = (flags & EPOLLHUP) != 0 || ((flags & EPOLLERR) != 0 && (flags & EPOLLOUT) != 0);
            events.push(registration->token, from_epoll_events(flags), error, read_closed,
                        write_closed);
            if (registration->auto_drain && (flags & EPOLLIN) != 0) {
                drain_descriptor(registration->handle);
            }
        }
    }
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
    void select_kqueue(Events& events, std::optional<std::chrono::milliseconds> timeout) {
        std::vector<kevent> raw_events(events.capacity());
        timespec timeout_value{};
        timespec* timeout_ptr = nullptr;
        if (timeout.has_value()) {
            timeout_value = to_timespec(*timeout);
            timeout_ptr = &timeout_value;
        }

        const int count = ::kevent(backend_, nullptr, 0, raw_events.data(),
                                   static_cast<int>(raw_events.size()), timeout_ptr);
        if (count == -1) {
            if (errno == EINTR) {
                return;
            }
            throw std::system_error(errno, std::generic_category(), "kevent(wait)");
        }

        for (int index = 0; index < count; ++index) {
            const auto& event = raw_events[index];
            const auto handle = static_cast<native_handle_type>(event.ident);
            const auto registration = find(handle);
            if (!registration.has_value()) {
                continue;
            }
            const bool error = (event.flags & EV_ERROR) != 0;
            const bool closed = (event.flags & EV_EOF) != 0;
            events.push(registration->token, from_kqueue_event(event), error, closed, closed);
            if (registration->auto_drain && event.filter == EVFILT_READ) {
                drain_descriptor(registration->handle);
            }
        }
    }
#endif

#if !defined(_WIN32) && !defined(__linux__) && \
    !(defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
      defined(__DragonFly__))
    void select_poll(Events& events, std::optional<std::chrono::milliseconds> timeout) {
        auto registrations = snapshot();
        std::vector<pollfd> poll_fds;
        poll_fds.reserve(registrations.size());
        for (const auto& registration : registrations) {
            short flags = POLLERR | POLLHUP;
            if (registration.interests.is_readable()) {
                flags |= POLLIN;
            }
            if (registration.interests.is_writable()) {
                flags |= POLLOUT;
            }
            if (registration.interests.is_priority()) {
                flags |= POLLPRI;
            }
            poll_fds.push_back(pollfd{registration.handle, flags, 0});
        }

        const auto timeout_value = clamp_timeout(timeout);
        const int result = ::poll(poll_fds.data(), poll_fds.size(), static_cast<int>(timeout_value.count()));
        if (result == -1) {
            if (errno == EINTR) {
                return;
            }
            throw std::system_error(errno, std::generic_category(), "poll");
        }

        for (std::size_t index = 0; index < poll_fds.size(); ++index) {
            const auto flags = poll_fds[index].revents;
            if (flags == 0) {
                continue;
            }
            Interest readiness{};
            if ((flags & (POLLIN | POLLHUP)) != 0) {
                readiness |= Interest::READABLE;
            }
            if ((flags & POLLOUT) != 0) {
                readiness |= Interest::WRITABLE;
            }
            if ((flags & POLLPRI) != 0) {
                readiness |= Interest::PRIORITY;
            }
            const bool closed = (flags & POLLHUP) != 0;
            events.push(registrations[index].token, readiness, (flags & POLLERR) != 0, closed,
                        closed);
            if (registrations[index].auto_drain && (flags & POLLIN) != 0) {
                drain_descriptor(registrations[index].handle);
            }
        }
    }
#endif

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<native_handle_type, Registration> registrations_;
    native_handle_type waker_handle_{invalid_native_handle};

#if defined(_WIN32)
    HANDLE iocp_{};
    std::shared_ptr<AfdHelper> afd_;
    bool waker_active_{};
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__) || defined(__DragonFly__)
    int backend_{-1};
#endif
};

class WakerState {
public:
    WakerState(std::shared_ptr<Selector> selector, Token token) : selector_(std::move(selector)), token_(token) {
#if defined(_WIN32)
        selector_->add_waker(0, token_);
#elif defined(__linux__)
        OwnedDescriptor event{::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)};
        if (!event.valid()) {
            throw std::system_error(errno, std::generic_category(), "eventfd");
        }
        read_handle_ = event.get();
        write_handle_ = event.get();
        selector_->add_waker(read_handle_, token_);
        owned_read_ = std::move(event);
#else
        int fds[2] = {-1, -1};
        if (::pipe(fds) == -1) {
            throw std::system_error(errno, std::generic_category(), "pipe");
        }
        OwnedDescriptor read{fds[0]};
        OwnedDescriptor write{fds[1]};
        set_descriptor_non_blocking(read.get());
        set_descriptor_non_blocking(write.get());
        set_descriptor_close_on_exec(read.get());
        set_descriptor_close_on_exec(write.get());
        read_handle_ = read.get();
        write_handle_ = write.get();
        selector_->add_waker(read_handle_, token_);
        owned_read_ = std::move(read);
        owned_write_ = std::move(write);
#endif
    }

    ~WakerState() {
        if (selector_) {
            selector_->remove_waker(read_handle_);
        }
    }

    void wake() const {
#if defined(_WIN32)
        selector_->post_waker(token_);
#elif defined(__linux__)
        const std::uint64_t value = 1;
        const auto written = ::write(write_handle_, &value, sizeof(value));
        if (written == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            throw std::system_error(errno, std::generic_category(), "write(eventfd)");
        }
#else
        constexpr std::array<char, 1> byte{{'w'}};
        const auto written = ::write(write_handle_, byte.data(), byte.size());
        if (written == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            throw std::system_error(errno, std::generic_category(), "write(waker pipe)");
        }
#endif
    }

private:
    std::shared_ptr<Selector> selector_;
    Token token_{};
    native_handle_type read_handle_{};
    native_handle_type write_handle_{};
#if !defined(_WIN32)
    OwnedDescriptor owned_read_;
    OwnedDescriptor owned_write_;
#endif
};

} // namespace ioxx::detail

namespace ioxx {

Event::Event(Token token, Interest readiness, bool error, bool read_closed,
             bool write_closed) noexcept
    : token_(token), readiness_(readiness), error_(error), read_closed_(read_closed),
      write_closed_(write_closed) {}

Events::Events(std::size_t capacity) {
    events_.reserve(capacity);
}

void Events::push(Token token, Interest readiness, bool error, bool read_closed, bool write_closed) {
    events_.push_back(Event{token, readiness, error, read_closed, write_closed});
}

Registry::Registry(std::shared_ptr<detail::Selector> selector) noexcept : selector_(std::move(selector)) {}

Registry::~Registry() = default;

void Registry::register_source(Source& source, Token token, Interest interests, std::error_code& ec) const noexcept {
    register_handle(source.native_handle(), source.source_kind(), token, interests, ec);
    if (!ec) {
        source.on_registered(*this, token, interests);
    }
}

void Registry::register_source(Source& source, Token token, Interest interests) const {
    std::error_code ec;
    register_source(source, token, interests, ec);
    if (ec) throw std::system_error(ec, "register_source");
}

void Registry::reregister(Source& source, Token token, Interest interests, std::error_code& ec) const noexcept {
    reregister_handle(source.native_handle(), source.source_kind(), token, interests, ec);
    if (!ec) {
        source.on_reregistered(*this, token, interests);
    }
}

void Registry::reregister(Source& source, Token token, Interest interests) const {
    std::error_code ec;
    reregister(source, token, interests, ec);
    if (ec) throw std::system_error(ec, "reregister");
}

void Registry::deregister(Source& source, std::error_code& ec) const noexcept {
    deregister_handle(source.native_handle(), ec);
    if (!ec) {
        source.on_deregistered(*this);
    }
}

void Registry::deregister(Source& source) const {
    std::error_code ec;
    deregister(source, ec);
    if (ec) throw std::system_error(ec, "deregister");
}

void Registry::register_handle(native_handle_type handle, SourceKind kind, Token token,
                               Interest interests, std::error_code& ec) const noexcept {
    try {
        selector_->add(handle, kind, token, interests);
        ec.clear();
    } catch(const std::system_error& e) {
        ec = e.code();
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
    }
}

void Registry::register_handle(native_handle_type handle, SourceKind kind, Token token,
                               Interest interests) const {
    std::error_code ec;
    register_handle(handle, kind, token, interests, ec);
    if (ec) throw std::system_error(ec, "register_handle");
}

void Registry::reregister_handle(native_handle_type handle, SourceKind kind, Token token,
                                 Interest interests, std::error_code& ec) const noexcept {
    try {
        selector_->update(handle, kind, token, interests);
        ec.clear();
    } catch(const std::system_error& e) {
        ec = e.code();
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
    }
}

void Registry::reregister_handle(native_handle_type handle, SourceKind kind, Token token,
                                 Interest interests) const {
    std::error_code ec;
    reregister_handle(handle, kind, token, interests, ec);
    if (ec) throw std::system_error(ec, "reregister_handle");
}

void Registry::deregister_handle(native_handle_type handle, std::error_code& ec) const noexcept {
    try {
        selector_->remove(handle);
        ec.clear();
    } catch(const std::system_error& e) {
        ec = e.code();
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
    }
}

void Registry::deregister_handle(native_handle_type handle) const {
    std::error_code ec;
    deregister_handle(handle, ec);
    if (ec) throw std::system_error(ec, "deregister_handle");
}

Poll::Poll(std::error_code& ec) noexcept : selector_(), registry_(nullptr) {
    try {
        selector_ = std::make_shared<detail::Selector>();
        registry_ = Registry(selector_);
        ec.clear();
    } catch(const std::system_error& e) {
        ec = e.code();
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
    }
}

Poll::Poll() : selector_(std::make_shared<detail::Selector>()), registry_(selector_) {}

Poll::Poll(Poll&& other) noexcept
    : selector_(std::move(other.selector_)), registry_(std::move(other.registry_)) {}

Poll& Poll::operator=(Poll&& other) noexcept {
    if (this != &other) {
        selector_ = std::move(other.selector_);
        registry_ = std::move(other.registry_);
    }
    return *this;
}

Poll::~Poll() = default;

void Poll::poll(Events& events, std::optional<std::chrono::milliseconds> timeout, std::error_code& ec) noexcept {
    try {
        selector_->select(events, timeout);
        ec.clear();
    } catch(const std::system_error& e) {
        ec = e.code();
    }
}

void Poll::poll(Events& events, std::optional<std::chrono::milliseconds> timeout) {
    std::error_code ec;
    poll(events, timeout, ec);
    if (ec) throw std::system_error(ec, "poll");
}

Waker::Waker(const Registry& registry, Token token, std::error_code& ec) noexcept : state_() {
    try {
        state_ = std::make_unique<detail::WakerState>(registry.selector_, token);
        ec.clear();
    } catch(const std::system_error& e) {
        ec = e.code();
    } catch(const std::bad_alloc&) {
        ec = std::make_error_code(std::errc::not_enough_memory);
    }
}

Waker::Waker(const Registry& registry, Token token)
    : state_(std::make_unique<detail::WakerState>(registry.selector_, token)) {}

Waker::Waker(Waker&& other) noexcept = default;

Waker& Waker::operator=(Waker&& other) noexcept = default;

Waker::~Waker() = default;

void Waker::wake(std::error_code& ec) const noexcept {
    try {
        state_->wake();
        ec.clear();
    } catch(const std::system_error& e) {
        ec = e.code();
    }
}

void Waker::wake() const {
    std::error_code ec;
    wake(ec);
    if (ec) throw std::system_error(ec, "wake");
}

} // namespace ioxx

#include <ioxx/signals.hpp>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <signal.h>
#elif defined(__linux__)
#include <sys/signalfd.h>
#include <signal.h>
#include <unistd.h>
#else // macOS / BSD
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include "socket.hpp"
#include <mutex>
#include <set>
#include <algorithm>

namespace ioxx {

#if defined(_WIN32)

class Signals::Impl {
public:
    SOCKET read_sock = INVALID_SOCKET;
    SOCKET write_sock = INVALID_SOCKET;
    sockaddr_in target_addr{};
    std::set<int> registered_signals;

    static std::mutex global_mutex;
    static Impl* global_instance;

    Impl(std::error_code& ec) {
        try {
            detail::ensure_socket_runtime();
        } catch (const std::system_error& e) {
            ec = e.code();
            return;
        }

        read_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (read_sock == INVALID_SOCKET) {
            ec = std::error_code{static_cast<int>(::WSAGetLastError()), std::system_category()};
            return;
        }

        // Make non-blocking
        u_long mode = 1;
        ::ioctlsocket(read_sock, FIONBIO, &mode);

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind_addr.sin_port = 0;

        if (::bind(read_sock, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) == SOCKET_ERROR) {
            ec = std::error_code{static_cast<int>(::WSAGetLastError()), std::system_category()};
            return;
        }

        int len = sizeof(target_addr);
        if (::getsockname(read_sock, reinterpret_cast<sockaddr*>(&target_addr), &len) == SOCKET_ERROR) {
            ec = std::error_code{static_cast<int>(::WSAGetLastError()), std::system_category()};
            return;
        }

        write_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (write_sock == INVALID_SOCKET) {
            ec = std::error_code{static_cast<int>(::WSAGetLastError()), std::system_category()};
            return;
        }

        std::lock_guard lock(global_mutex);
        global_instance = this;
    }

    ~Impl() {
        std::lock_guard lock(global_mutex);
        if (global_instance == this) {
            global_instance = nullptr;
            ::SetConsoleCtrlHandler(handler_routine, FALSE);
        }
        if (read_sock != INVALID_SOCKET) ::closesocket(read_sock);
        if (write_sock != INVALID_SOCKET) ::closesocket(write_sock);
    }

    static BOOL WINAPI handler_routine(DWORD dwCtrlType) {
        int signum = -1;
        switch (dwCtrlType) {
            case CTRL_C_EVENT: signum = SIGINT; break;
            case CTRL_BREAK_EVENT: signum = SIGBREAK; break;
            case CTRL_CLOSE_EVENT:
            case CTRL_LOGOFF_EVENT:
            case CTRL_SHUTDOWN_EVENT: signum = SIGTERM; break;
            default: return FALSE;
        }

        std::lock_guard lock(global_mutex);
        if (global_instance && global_instance->registered_signals.count(signum)) {
            ::sendto(global_instance->write_sock, reinterpret_cast<const char*>(&signum), sizeof(signum), 0,
                     reinterpret_cast<const sockaddr*>(&global_instance->target_addr), sizeof(global_instance->target_addr));
            return TRUE;
        }
        return FALSE;
    }

    void add(int signum, std::error_code& ec) {
        std::lock_guard lock(global_mutex);
        if (registered_signals.empty()) {
            if (!::SetConsoleCtrlHandler(handler_routine, TRUE)) {
                ec = std::error_code{static_cast<int>(::GetLastError()), std::system_category()};
                return;
            }
        }
        registered_signals.insert(signum);
        ec.clear();
    }

    void remove(int signum, std::error_code& ec) {
        std::lock_guard lock(global_mutex);
        registered_signals.erase(signum);
        if (registered_signals.empty()) {
            ::SetConsoleCtrlHandler(handler_routine, FALSE);
        }
        ec.clear();
    }

    std::vector<int> pending(std::error_code& ec) {
        std::vector<int> sigs;
        while (true) {
            int signum = 0;
            const int res = ::recv(read_sock, reinterpret_cast<char*>(&signum), sizeof(signum), 0);
            if (res == sizeof(signum)) {
                sigs.push_back(signum);
            } else {
                if (res == SOCKET_ERROR) {
                    int err = ::WSAGetLastError();
                    if (err != WSAEWOULDBLOCK) {
                        ec = std::error_code{err, std::system_category()};
                        return sigs;
                    }
                }
                break;
            }
        }
        ec.clear();
        return sigs;
    }
};

std::mutex Signals::Impl::global_mutex;
Signals::Impl* Signals::Impl::global_instance = nullptr;

#elif defined(__linux__)

class Signals::Impl {
public:
    int sfd = -1;
    sigset_t mask{};

    Impl(std::error_code& ec) {
        sigemptyset(&mask);
        sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (sfd == -1) {
            ec = std::error_code{errno, std::generic_category()};
        } else {
            ec.clear();
        }
    }

    ~Impl() {
        if (sfd != -1) {
            close(sfd);
            sigprocmask(SIG_UNBLOCK, &mask, nullptr);
        }
    }

    void add(int signum, std::error_code& ec) {
        sigaddset(&mask, signum);
        if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
            ec = std::error_code{errno, std::generic_category()};
            return;
        }
        int new_sfd = signalfd(sfd, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (new_sfd == -1) {
            ec = std::error_code{errno, std::generic_category()};
            return;
        }
        sfd = new_sfd;
        ec.clear();
    }

    void remove(int signum, std::error_code& ec) {
        sigset_t unblock_mask;
        sigemptyset(&unblock_mask);
        sigaddset(&unblock_mask, signum);
        sigprocmask(SIG_UNBLOCK, &unblock_mask, nullptr);
        
        sigdelset(&mask, signum);
        int new_sfd = signalfd(sfd, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (new_sfd == -1) {
            ec = std::error_code{errno, std::generic_category()};
            return;
        }
        sfd = new_sfd;
        ec.clear();
    }

    std::vector<int> pending(std::error_code& ec) {
        std::vector<int> sigs;
        while (true) {
            signalfd_siginfo info;
            ssize_t res = read(sfd, &info, sizeof(info));
            if (res == sizeof(info)) {
                sigs.push_back(info.ssi_signo);
            } else {
                if (res == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    ec = std::error_code{errno, std::generic_category()};
                    return sigs;
                }
                break;
            }
        }
        ec.clear();
        return sigs;
    }
};

#else // macOS / BSD (Self-Pipe Trick)

class Signals::Impl {
public:
    int pipe_fds[2] = {-1, -1};
    std::set<int> registered_signals;

    static std::mutex global_mutex;
    static Impl* global_instance;

    Impl(std::error_code& ec) {
        if (pipe(pipe_fds) == -1) {
            ec = std::error_code{errno, std::generic_category()};
            return;
        }
        fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);
        fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK);

        std::lock_guard lock(global_mutex);
        global_instance = this;
        ec.clear();
    }

    ~Impl() {
        std::lock_guard lock(global_mutex);
        if (global_instance == this) {
            for (int signum : registered_signals) {
                signal(signum, SIG_DFL);
            }
            global_instance = nullptr;
        }
        if (pipe_fds[0] != -1) close(pipe_fds[0]);
        if (pipe_fds[1] != -1) close(pipe_fds[1]);
    }

    static void handler_routine(int signum) {
        if (global_instance && global_instance->pipe_fds[1] != -1) {
            int saved_errno = errno;
            write(global_instance->pipe_fds[1], &signum, sizeof(signum));
            errno = saved_errno;
        }
    }

    void add(int signum, std::error_code& ec) {
        std::lock_guard lock(global_mutex);
        registered_signals.insert(signum);
        struct sigaction sa{};
        sa.sa_handler = handler_routine;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        if (sigaction(signum, &sa, nullptr) == -1) {
            ec = std::error_code{errno, std::generic_category()};
            return;
        }
        ec.clear();
    }

    void remove(int signum, std::error_code& ec) {
        std::lock_guard lock(global_mutex);
        registered_signals.erase(signum);
        signal(signum, SIG_DFL);
        ec.clear();
    }

    std::vector<int> pending(std::error_code& ec) {
        std::vector<int> sigs;
        while (true) {
            int signum = 0;
            ssize_t res = read(pipe_fds[0], &signum, sizeof(signum));
            if (res == sizeof(signum)) {
                sigs.push_back(signum);
            } else {
                if (res == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    ec = std::error_code{errno, std::generic_category()};
                    return sigs;
                }
                break;
            }
        }
        ec.clear();
        return sigs;
    }
};

std::mutex Signals::Impl::global_mutex;
Signals::Impl* Signals::Impl::global_instance = nullptr;

#endif

Signals::Signals(std::error_code& ec) noexcept : impl_(std::make_unique<Impl>(ec)) {}

Signals::Signals() {
    std::error_code ec;
    impl_ = std::make_unique<Impl>(ec);
    if (ec) throw std::system_error(ec, "Signals::Signals");
}

Signals::~Signals() = default;

Signals::Signals(Signals&& other) noexcept = default;
Signals& Signals::operator=(Signals&& other) noexcept = default;

void Signals::add(int signum, std::error_code& ec) noexcept {
    impl_->add(signum, ec);
}

void Signals::add(int signum) {
    std::error_code ec;
    add(signum, ec);
    if (ec) throw std::system_error(ec, "Signals::add");
}

void Signals::remove(int signum, std::error_code& ec) noexcept {
    impl_->remove(signum, ec);
}

void Signals::remove(int signum) {
    std::error_code ec;
    remove(signum, ec);
    if (ec) throw std::system_error(ec, "Signals::remove");
}

std::vector<int> Signals::pending(std::error_code& ec) noexcept {
    return impl_->pending(ec);
}

std::vector<int> Signals::pending() {
    std::error_code ec;
    auto sigs = pending(ec);
    if (ec) throw std::system_error(ec, "Signals::pending");
    return sigs;
}

native_handle_type Signals::native_handle() const noexcept {
#if defined(_WIN32)
    return impl_->read_sock;
#elif defined(__linux__)
    return impl_->sfd;
#else
    return impl_->pipe_fds[0];
#endif
}

SourceKind Signals::source_kind() const noexcept {
#if defined(_WIN32)
    return SourceKind::socket;
#else
    return SourceKind::raw_descriptor;
#endif
}

void Signals::on_registered(const Registry& registry, Token token, Interest interests) {
    registry.register_handle(native_handle(), source_kind(), token, interests);
}

void Signals::on_reregistered(const Registry& registry, Token token, Interest interests) {
    registry.reregister_handle(native_handle(), source_kind(), token, interests);
}

void Signals::on_deregistered(const Registry& registry) {
    registry.deregister_handle(native_handle());
}

} // namespace ioxx

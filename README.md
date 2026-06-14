# ioxx – I/O for C++20

`ioxx` is a fast, low-level I/O library for C++20 focusing on non-blocking APIs and event notification for building high performance I/O apps with as little overhead as possible over the OS abstractions. It is heavily inspired by Rust's [Mio](https://github.com/tokio-rs/mio).

This is a low level library, if you are looking for something easier to get started with, you might want to build higher-level asynchronous runtimes on top of `ioxx`.

## Features
- **Modern C++20**: Zero-overhead abstractions, `std::span`, modules, and strict RAII semantics.
- **Exception-Free Design**: All methods have `std::error_code` overloads. Perfect for environments where exceptions are disabled (`-fno-exceptions`).
- **Cross-Platform Event Loop**: Uses `epoll` (Linux), `kqueue` (macOS), and `wepoll`/IOCP (Windows).
- **Vectored I/O (Scatter/Gather)**: `send_vectored_to` and `read_vectored` support for zero-copy protocol implementations.
- **Advanced Network Features**:
  - **TCP Fast Open (TFO)**: Accelerates connection setup by saving 1 RTT.
  - **Path MTU Discovery (PMTUD)**: Fine-grained control over IP fragmentation (`set_dont_fragment`).
- **OS Signal Handling**: First-class integration of `SIGINT`, `SIGTERM`, etc. into the `Poll` loop (`ioxx::Signals`).

## Usage

To use `ioxx`, integrate it into your CMake project. Note that CMake 3.28+ is required if you want to use the optional C++20 Modules feature.

```cmake
add_subdirectory(ioxx)
target_link_libraries(my_app PRIVATE ioxx::ioxx)
```

Next we can start using `ioxx`. The following is a quick introduction using `TcpListener` and `TcpStream`.

```cpp
#include <ioxx/ioxx.hpp>
#include <iostream>
#include <system_error>
#include <thread>
#include <chrono>

using namespace ioxx;
using namespace ioxx::net;
using namespace std::chrono_literals;

// Some tokens to allow us to identify which event is for which socket.
const Token SERVER = Token(0);
const Token CLIENT = Token(1);

int main() {
    std::error_code ec;

    // Create a poll instance.
    Poll poll(ec);
    if (ec) { std::cerr << "Poll error: " << ec.message() << '\n'; return 1; }

    // Create storage for events.
    Events events{128};

    // Setup the server socket.
    auto addr = SocketAddrV4{Ipv4Addr{127, 0, 0, 1}, 13265};
    auto listener = TcpListener::bind(addr, ec);
    
    // Start listening for incoming connections.
    poll.registry().register_source(listener, SERVER, Interest::READABLE, ec);

    // Setup the client socket.
    auto client = TcpStream::connect(addr, ec);
    
    // Register the socket.
    poll.registry().register_source(client, CLIENT, Interest::READABLE | Interest::WRITABLE, ec);

    // Start an event loop.
    while (true) {
        // Poll ioxx for events, blocking until we get an event.
        poll.poll(events, std::nullopt, ec);

        // Process each event.
        for (const auto& event : events) {
            // We can use the token we previously provided to `register_source` to
            // determine for which socket the event is.
            if (event.token() == SERVER) {
                // If this is an event for the server, it means a connection
                // is ready to be accepted.
                //
                // Accept the connection and drop it immediately. This will
                // close the socket and notify the client of the EOF.
                auto [connection, peer] = listener.accept(ec);
                if (ec && is_would_block(ec)) {
                    continue; // Nothing to accept right now
                }
                
                // connection is automatically closed when it goes out of scope.
            } else if (event.token() == CLIENT) {
                if (event.is_writable()) {
                    // We can (likely) write to the socket without blocking.
                }

                if (event.is_readable()) {
                    // We can (likely) read from the socket without blocking.
                }
            }
        }
    }

    return 0;
}
```

## Acknowledgements

`ioxx` is heavily based on the Rust library [Mio](https://github.com/tokio-rs/mio). The architecture, API design, and many internal platform-specific implementations are directly translated or adapted from `mio`. We would like to thank the `mio` authors and contributors for their work on cross-platform asynchronous I/O.

## Supported Platforms

Currently `ioxx` supports:
- Linux / Android (`epoll`, `signalfd`)
- Windows (`wepoll`, `Winsock2`, Self-Pipe)
- macOS / iOS / BSD (`kqueue`, Self-Pipe)

## Modules (C++20)

`ioxx` provides experimental C++20 Modules support. To use it, enable the CMake option:

```bash
cmake -B build -DIOXX_BUILD_MODULES=ON
```

Then you can simply use:

```cpp
import ioxx;

int main() {
    ioxx::Poll poll;
    // ...
}
```

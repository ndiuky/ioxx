#include <ioxx/ioxx.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>

int main() {
    ioxx::Poll poll;
    ioxx::Events events{64};
    auto socket = ioxx::net::UdpSocket::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 9001});

    poll.registry().register_source(socket, ioxx::Token{1}, ioxx::Interest::READABLE);
    std::cout << "udp echo on 127.0.0.1:9001\n";

    std::array<std::byte, 2048> buffer{};
    for (;;) {
        poll.poll(events, std::chrono::milliseconds{500});
        for (const auto& event : events) {
            if (event.token() != ioxx::Token{1} || !event.is_readable()) {
                continue;
            }

            try {
                auto [size, peer] = socket.recv_from(buffer);
                socket.send_to(std::span<const std::byte>{buffer.data(), size}, peer);
            } catch (const std::system_error& error) {
                if (!ioxx::is_would_block(error.code())) {
                    throw;
                }
            }
        }
    }
}

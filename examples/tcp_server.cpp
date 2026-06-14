#include <ioxx/ioxx.hpp>

#include <chrono>
#include <iostream>

int main() {
    ioxx::Poll poll;
    ioxx::Events events{128};

    auto server = ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 9000});
    poll.registry().register_source(server, ioxx::Token{1}, ioxx::Interest::READABLE);

    std::cout << "listening on 127.0.0.1:9000\n";

    for (;;) {
        poll.poll(events, std::chrono::milliseconds{500});
        for (const auto& event : events) {
            if (event.token() != ioxx::Token{1} || !event.is_readable()) {
                continue;
            }

            try {
                auto [client, peer] = server.accept();
                std::cout << "accepted connection on port " << peer.port() << '\n';
                client.write("hello from ioxx C++20\n");
            } catch (const std::system_error& error) {
                if (!ioxx::is_would_block(error.code())) {
                    throw;
                }
            }
        }
    }
}

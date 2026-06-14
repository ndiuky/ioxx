#include <gtest/gtest.h>
#include <ioxx/ioxx.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

TEST(TcpTests, SetGetTtl) {
    auto listener = ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    std::error_code ec;
    listener.set_ttl(10, ec);
    EXPECT_FALSE(ec);
    const auto ttl = listener.ttl(ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(ttl, 10);
}

TEST(TcpTests, GetTtlWithoutPreviousSet) {
    auto listener = ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    std::error_code ec;
    const auto ttl = listener.ttl(ec);
    EXPECT_FALSE(ec);
    EXPECT_GT(ttl, 0);
}

TEST(TcpTests, AcceptWithoutPoll) {
    auto listener = ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    const auto endpoint = listener.local_endpoint();

    std::thread client{[endpoint] {
        auto stream = new ioxx::net::TcpStream(ioxx::net::TcpStream::connect(endpoint));
        std::this_thread::sleep_for(1s);
        delete stream;
    }};
    client.detach();

    std::this_thread::sleep_for(100ms);

    std::error_code ec;
    auto [stream, addr] = listener.accept(ec);
    if (ec) {
        printf("ACCEPT FAILED: %d\n", ec.value());
    }
    ASSERT_FALSE(ec);
}

TEST(TcpTests, TcpListener) {
    ioxx::Poll poll;
    ioxx::Events events{8};
    auto listener = ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    const auto endpoint = listener.local_endpoint();

    poll.registry().register_source(listener, ioxx::Token{9}, ioxx::Interest::READABLE);

    std::thread client{[endpoint] {
        auto stream = new ioxx::net::TcpStream(ioxx::net::TcpStream::connect(endpoint));
        std::this_thread::sleep_for(10s);
        delete stream;
    }};
    client.detach();

    poll.poll(events, 2s);
    ASSERT_FALSE(events.is_empty());
    EXPECT_EQ(events[0].token(), ioxx::Token{9});
    EXPECT_TRUE(events[0].is_readable());

    const auto [stream, peer] = listener.accept();
    EXPECT_TRUE(stream.valid());
    EXPECT_NE(peer.port(), 0);
}

TEST(TcpTests, TcpRoundTrip) {
    ioxx::Poll poll;
    ioxx::Events events{8};
    auto listener = ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    const auto endpoint = listener.local_endpoint();

    poll.registry().register_source(listener, ioxx::Token{10}, ioxx::Interest::READABLE);

    auto client = ioxx::net::TcpStream::connect(endpoint);
    poll.registry().register_source(client, ioxx::Token{11}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);

    poll.poll(events, 2s);

    auto [server, peer] = listener.accept();
    poll.registry().register_source(server, ioxx::Token{12}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);

    const auto payload = "tcp-ping";
    bool client_writable = false;
    for (const auto& event : events) {
        if (event.token() == ioxx::Token{11} && event.is_writable()) {
            client_writable = true;
        }
    }
    
    if (!client_writable) {
        poll.poll(events, 2s);
    }
    
    EXPECT_EQ(client.write(payload, std::strlen(payload)), std::strlen(payload));

    poll.poll(events, 2s);
    
    std::array<std::byte, 16> buffer{};
    const auto n = server.read(buffer);
    EXPECT_EQ(n, std::strlen(payload));
    
    const auto reply = "tcp-pong";
    EXPECT_EQ(server.write(reply, std::strlen(reply)), std::strlen(reply));
    
    poll.poll(events, 2s);
    const auto n2 = client.read(buffer);
    EXPECT_EQ(n2, std::strlen(reply));
}

TEST(TcpTests, TcpSocketBuilder) {
    auto builder = ioxx::net::TcpSocket::new_v4();
    builder.set_reuseaddr(true);
    builder.set_keepalive(true);
    builder.set_nodelay(true);

    auto listener = builder.bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    EXPECT_TRUE(listener.valid());
    
    auto endpoint = listener.local_endpoint();
    EXPECT_NE(endpoint.port(), 0);

    // Try TFO if supported, otherwise just ignore the error
    std::error_code ec;
    auto client = ioxx::net::TcpSocket::new_v4();
    client.set_tcp_fastopen_connect(true, ec);
    auto stream = client.connect(endpoint, ec);
}

TEST(TcpTests, TcpVectoredIo) {
    auto listener = ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    auto endpoint = listener.local_endpoint();

    ioxx::Poll poll;
    ioxx::Events events{8};
    poll.registry().register_source(listener, ioxx::Token{1}, ioxx::Interest::READABLE);

    auto client = ioxx::net::TcpStream::connect(endpoint);
    poll.poll(events, 2s);
    auto [server, peer] = listener.accept();

    const char* str1 = "hello, ";
    const char* str2 = "world!";
    
    std::array<ioxx::net::IoSlice, 2> write_bufs = {
        ioxx::net::IoSlice(str1, std::strlen(str1)),
        ioxx::net::IoSlice(str2, std::strlen(str2))
    };

    EXPECT_EQ(client.write_vectored(write_bufs), std::strlen(str1) + std::strlen(str2));

    std::array<std::byte, 16> buf1{};
    std::array<std::byte, 16> buf2{};
    std::array<ioxx::net::IoSliceMut, 2> read_bufs = {
        ioxx::net::IoSliceMut(buf1.data(), 7),
        ioxx::net::IoSliceMut(buf2.data(), 6)
    };

    poll.registry().register_source(server, ioxx::Token{2}, ioxx::Interest::READABLE);
    poll.poll(events, 2s);

    EXPECT_EQ(server.read_vectored(read_bufs), 13);
    
    EXPECT_EQ(std::memcmp(buf1.data(), "hello, ", 7), 0);
    EXPECT_EQ(std::memcmp(buf2.data(), "world!", 6), 0);
}

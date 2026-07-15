#include <gtest/gtest.h>
#include <ioxx/ioxx.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

namespace {

ioxx::net::SocketAddrV4 loopback(std::uint16_t port = 0) {
    return ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, port};
}

bool wait_for_event(ioxx::Poll& poll, ioxx::Events& events, ioxx::Token token, ioxx::Interest interest) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code ec;
        events.clear();
        poll.poll(events, 20ms, ec);
        if (ec) {
            ADD_FAILURE() << "poll failed: " << ec.message();
            return false;
        }

        for (const auto& event : events) {
            if (event.token() != token) {
                continue;
            }
            if (interest.is_readable() && !event.is_readable()) {
                continue;
            }
            if (interest.is_writable() && !event.is_writable()) {
                continue;
            }
            return true;
        }
    }

    return false;
}

void expect_connected(const ioxx::net::TcpStream& stream) {
    const auto error = stream.take_error();
    ASSERT_FALSE(error.has_value()) << error->message();
}

void expect_write(ioxx::net::TcpStream& stream, std::string_view payload) {
    std::error_code ec;
    const auto written = stream.write(payload.data(), payload.size(), ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_EQ(written, payload.size());
}

std::string_view read_into(ioxx::net::TcpStream& stream, std::array<std::byte, 128>& buffer) {
    std::error_code ec;
    const auto read = stream.read(buffer, ec);
    EXPECT_FALSE(ec) << ec.message();
    return {reinterpret_cast<const char*>(buffer.data()), read};
}

} // namespace

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

TEST(TcpTests, TcpClientServerHandlesIndependentStreams) {
    ioxx::Poll poll;
    ioxx::Events events{16};
    auto listener = ioxx::net::TcpListener::bind(loopback());
    const auto endpoint = listener.local_endpoint();

    poll.registry().register_source(listener, ioxx::Token{1}, ioxx::Interest::READABLE);

    auto client1 = ioxx::net::TcpStream::connect(endpoint);
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{1}, ioxx::Interest::READABLE));
    auto [server1, peer1] = listener.accept();
    ASSERT_TRUE(server1.valid());
    EXPECT_EQ(peer1.port(), client1.local_endpoint().port());

    auto client2 = ioxx::net::TcpStream::connect(endpoint);
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{1}, ioxx::Interest::READABLE));
    auto [server2, peer2] = listener.accept();
    ASSERT_TRUE(server2.valid());
    EXPECT_EQ(peer2.port(), client2.local_endpoint().port());
    EXPECT_NE(peer1.port(), peer2.port());

    poll.registry().register_source(client1, ioxx::Token{2}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);
    poll.registry().register_source(server1, ioxx::Token{4}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{2}, ioxx::Interest::WRITABLE));
    expect_connected(client1);

    expect_write(client1, "client-one/request-alpha");

    std::array<std::byte, 128> buffer1{};
    std::array<std::byte, 128> buffer2{};
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{4}, ioxx::Interest::READABLE));
    EXPECT_EQ(read_into(server1, buffer1), "client-one/request-alpha");

    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{4}, ioxx::Interest::WRITABLE));
    expect_write(server1, "server-one/response-alpha");

    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{2}, ioxx::Interest::READABLE));
    EXPECT_EQ(read_into(client1, buffer1), "server-one/response-alpha");

    poll.registry().register_source(client2, ioxx::Token{3}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);
    poll.registry().register_source(server2, ioxx::Token{5}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{3}, ioxx::Interest::WRITABLE));
    expect_connected(client2);

    expect_write(client2, "client-two/request-bravo");

    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{5}, ioxx::Interest::READABLE));
    EXPECT_EQ(read_into(server2, buffer2), "client-two/request-bravo");

    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{5}, ioxx::Interest::WRITABLE));
    expect_write(server2, "server-two/response-bravo");

    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{3}, ioxx::Interest::READABLE));
    EXPECT_EQ(read_into(client2, buffer2), "server-two/response-bravo");
}

TEST(TcpTests, TcpClientServerVectoredReplyAndHalfClose) {
    ioxx::Poll poll;
    ioxx::Events events{16};
    auto listener = ioxx::net::TcpListener::bind(loopback());
    const auto endpoint = listener.local_endpoint();

    poll.registry().register_source(listener, ioxx::Token{10}, ioxx::Interest::READABLE);
    auto client = ioxx::net::TcpStream::connect(endpoint);
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{10}, ioxx::Interest::READABLE));
    auto [server, peer] = listener.accept();
    ASSERT_TRUE(server.valid());
    EXPECT_EQ(peer.port(), client.local_endpoint().port());

    poll.registry().register_source(client, ioxx::Token{11}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);
    poll.registry().register_source(server, ioxx::Token{12}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);

    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{11}, ioxx::Interest::WRITABLE));
    expect_connected(client);

    const char* request1 = "POST ";
    const char* request2 = "/tcp/vector ";
    const char* request3 = "payload";
    std::array<ioxx::net::IoSlice, 3> request = {
        ioxx::net::IoSlice(request1, std::strlen(request1)),
        ioxx::net::IoSlice(request2, std::strlen(request2)),
        ioxx::net::IoSlice(request3, std::strlen(request3)),
    };
    std::error_code ec;
    EXPECT_EQ(client.write_vectored(request, ec), std::strlen(request1) + std::strlen(request2) + std::strlen(request3));
    ASSERT_FALSE(ec) << ec.message();

    std::array<std::byte, 128> buffer{};
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{12}, ioxx::Interest::READABLE));
    EXPECT_EQ(read_into(server, buffer), "POST /tcp/vector payload");

    const char* response1 = "HTTP/1.1 200 ";
    const char* response2 = "OK";
    std::array<ioxx::net::IoSlice, 2> response = {
        ioxx::net::IoSlice(response1, std::strlen(response1)),
        ioxx::net::IoSlice(response2, std::strlen(response2)),
    };
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{12}, ioxx::Interest::WRITABLE));
    EXPECT_EQ(server.write_vectored(response, ec), std::strlen(response1) + std::strlen(response2));
    ASSERT_FALSE(ec) << ec.message();

    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{11}, ioxx::Interest::READABLE));
    EXPECT_EQ(read_into(client, buffer), "HTTP/1.1 200 OK");

    client.shutdown_write(ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(wait_for_event(poll, events, ioxx::Token{12}, ioxx::Interest::READABLE));
    EXPECT_EQ(server.read(buffer, ec), 0);
    EXPECT_FALSE(ec) << ec.message();
}

#include <gtest/gtest.h>
#include <ioxx/ioxx.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

namespace {

ioxx::net::SocketAddrV4 loopback(std::uint16_t port = 0) {
    return ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, port};
}

ioxx::net::SocketAddrV6 loopback_v6(std::uint16_t port = 0) {
    return ioxx::net::SocketAddrV6{ioxx::net::Ipv6Addr{0, 0, 0, 0, 0, 0, 0, 1}, port};
}

void expect_connected(const ioxx::net::TcpStream& stream) {
    const auto error = stream.take_error();
    ASSERT_FALSE(error.has_value()) << error->message();
}

bool is_retryable_socket_error(const std::error_code& ec) {
    if (ioxx::is_would_block(ec) || ec == std::errc::not_connected ||
        ec == std::errc::operation_in_progress || ec == std::errc::connection_already_in_progress) {
        return true;
    }
#if defined(_WIN32)
    return ec.value() == 10057 || ec.value() == 10036 || ec.value() == 10037;
#else
    return false;
#endif
}

std::pair<ioxx::net::TcpStream, ioxx::net::SocketAddr> accept_with_retry(const ioxx::net::TcpListener& listener) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code ec;
        auto accepted = listener.accept(ec);
        if (!ec) {
            return accepted;
        }
        if (!is_retryable_socket_error(ec)) {
            ADD_FAILURE() << "accept failed: " << ec.message();
            return accepted;
        }
        std::this_thread::sleep_for(20ms);
    }

    ADD_FAILURE() << "accept timed out";
    return {ioxx::net::TcpStream{}, ioxx::net::SocketAddr{}};
}

void expect_write(ioxx::net::TcpStream& stream, std::string_view payload) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code ec;
        const auto written = stream.write(payload.data(), payload.size(), ec);
        if (!ec) {
            ASSERT_EQ(written, payload.size());
            return;
        }
        if (!is_retryable_socket_error(ec)) {
            FAIL() << "write failed: " << ec.message();
        }
        std::this_thread::sleep_for(20ms);
    }

    FAIL() << "write timed out";
}

std::string_view read_into(ioxx::net::TcpStream& stream, std::array<std::byte, 128>& buffer) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code ec;
        const auto read = stream.read(buffer, ec);
        if (!ec) {
            return {reinterpret_cast<const char*>(buffer.data()), read};
        }
        if (!is_retryable_socket_error(ec)) {
            ADD_FAILURE() << "read failed: " << ec.message();
            return {};
        }
        std::this_thread::sleep_for(20ms);
    }

    ADD_FAILURE() << "read timed out";
    return {};
}

void expect_write_vectored(ioxx::net::TcpStream& stream, std::span<ioxx::net::IoSlice> buffers, std::size_t size) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code ec;
        const auto written = stream.write_vectored(buffers, ec);
        if (!ec) {
            ASSERT_EQ(written, size);
            return;
        }
        if (!is_retryable_socket_error(ec)) {
            FAIL() << "write_vectored failed: " << ec.message();
        }
        std::this_thread::sleep_for(20ms);
    }

    FAIL() << "write_vectored timed out";
}

std::size_t read_size_with_retry(ioxx::net::TcpStream& stream, std::array<std::byte, 128>& buffer) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code ec;
        const auto read = stream.read(buffer, ec);
        if (!ec) {
            return read;
        }
        if (!is_retryable_socket_error(ec)) {
            ADD_FAILURE() << "read failed: " << ec.message();
            return 0;
        }
        std::this_thread::sleep_for(20ms);
    }

    ADD_FAILURE() << "read timed out";
    return 0;
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

TEST(TcpTests, Ipv6LoopbackRoundTrip) {
    std::error_code ec;
    auto listener = ioxx::net::TcpListener::bind(loopback_v6(), ec);
    if (ec) {
        GTEST_SKIP() << "IPv6 loopback is not available: " << ec.message();
    }

    listener.set_ttl(12, ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_EQ(listener.ttl(ec), 12);
    ASSERT_FALSE(ec) << ec.message();

    const auto endpoint = listener.local_endpoint();
    auto client = ioxx::net::TcpStream::connect(endpoint);
    auto [server, peer] = accept_with_retry(listener);
    ASSERT_TRUE(server.valid());
    EXPECT_TRUE(peer.is_v6());
    expect_connected(client);

    expect_write(client, "ipv6/request");
    std::array<std::byte, 128> buffer{};
    EXPECT_EQ(read_into(server, buffer), "ipv6/request");
}

TEST(TcpTests, DualStackListenerAcceptsIpv4Clients) {
    std::error_code ec;
    auto listener = ioxx::net::TcpListener::bind_dual_stack(0, ec);
    if (ec) {
        GTEST_SKIP() << "dual-stack IPv6 sockets are not available: " << ec.message();
    }

    const auto endpoint = loopback(listener.local_endpoint().port());
    auto client = ioxx::net::TcpStream::connect(endpoint);
    auto [server, peer] = accept_with_retry(listener);
    ASSERT_TRUE(server.valid());
    EXPECT_TRUE(peer.is_v4());
    EXPECT_EQ(peer.port(), client.local_endpoint().port());
    expect_connected(client);

    expect_write(client, "dual-stack/request");
    std::array<std::byte, 128> buffer{};
    EXPECT_EQ(read_into(server, buffer), "dual-stack/request");
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
    auto listener = ioxx::net::TcpListener::bind(loopback());
    const auto endpoint = listener.local_endpoint();

    auto client1 = ioxx::net::TcpStream::connect(endpoint);
    auto [server1, peer1] = accept_with_retry(listener);
    ASSERT_TRUE(server1.valid());
    EXPECT_EQ(peer1.port(), client1.local_endpoint().port());

    auto client2 = ioxx::net::TcpStream::connect(endpoint);
    auto [server2, peer2] = accept_with_retry(listener);
    ASSERT_TRUE(server2.valid());
    EXPECT_EQ(peer2.port(), client2.local_endpoint().port());
    EXPECT_NE(peer1.port(), peer2.port());

    expect_connected(client1);

    expect_write(client1, "client-one/request-alpha");

    std::array<std::byte, 128> buffer1{};
    std::array<std::byte, 128> buffer2{};
    EXPECT_EQ(read_into(server1, buffer1), "client-one/request-alpha");

    expect_write(server1, "server-one/response-alpha");

    EXPECT_EQ(read_into(client1, buffer1), "server-one/response-alpha");

    expect_connected(client2);

    expect_write(client2, "client-two/request-bravo");

    EXPECT_EQ(read_into(server2, buffer2), "client-two/request-bravo");

    expect_write(server2, "server-two/response-bravo");

    EXPECT_EQ(read_into(client2, buffer2), "server-two/response-bravo");
}

TEST(TcpTests, TcpClientServerVectoredReplyAndHalfClose) {
    auto listener = ioxx::net::TcpListener::bind(loopback());
    const auto endpoint = listener.local_endpoint();

    auto client = ioxx::net::TcpStream::connect(endpoint);
    auto [server, peer] = accept_with_retry(listener);
    ASSERT_TRUE(server.valid());
    EXPECT_EQ(peer.port(), client.local_endpoint().port());
    expect_connected(client);

    const char* request1 = "POST ";
    const char* request2 = "/tcp/vector ";
    const char* request3 = "payload";
    std::array<ioxx::net::IoSlice, 3> request = {
        ioxx::net::IoSlice(request1, std::strlen(request1)),
        ioxx::net::IoSlice(request2, std::strlen(request2)),
        ioxx::net::IoSlice(request3, std::strlen(request3)),
    };
    expect_write_vectored(client, request, std::strlen(request1) + std::strlen(request2) + std::strlen(request3));

    std::array<std::byte, 128> buffer{};
    EXPECT_EQ(read_into(server, buffer), "POST /tcp/vector payload");

    const char* response1 = "HTTP/1.1 200 ";
    const char* response2 = "OK";
    std::array<ioxx::net::IoSlice, 2> response = {
        ioxx::net::IoSlice(response1, std::strlen(response1)),
        ioxx::net::IoSlice(response2, std::strlen(response2)),
    };
    expect_write_vectored(server, response, std::strlen(response1) + std::strlen(response2));

    EXPECT_EQ(read_into(client, buffer), "HTTP/1.1 200 OK");

    std::error_code ec;
    client.shutdown_write(ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_EQ(read_size_with_retry(server, buffer), 0);
}

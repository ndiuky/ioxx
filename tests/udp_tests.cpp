#include <gtest/gtest.h>
#include <ioxx/ioxx.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

TEST(UdpTests, SetGetTtl) {
    auto socket = ioxx::net::UdpSocket::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    std::error_code ec;
    socket.set_ttl(10, ec);
    EXPECT_FALSE(ec);
    const auto ttl = socket.ttl(ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(ttl, 10);
}

TEST(UdpTests, GetTtlWithoutPreviousSet) {
    auto socket = ioxx::net::UdpSocket::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    std::error_code ec;
    const auto ttl = socket.ttl(ec);
    EXPECT_FALSE(ec);
    EXPECT_GT(ttl, 0);
}

TEST(UdpTests, UdpRoundTrip) {
    ioxx::Poll poll;
    ioxx::Events events{8};

    auto sender = ioxx::net::UdpSocket::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    auto receiver = ioxx::net::UdpSocket::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});

    poll.registry().register_source(receiver, ioxx::Token{7}, ioxx::Interest::READABLE);

    const auto payload = "ping";
    sender.send_to(payload, std::strlen(payload), receiver.local_endpoint());

    poll.poll(events, 2s);
    ASSERT_FALSE(events.is_empty());
    EXPECT_EQ(events[0].token(), ioxx::Token{7});
    EXPECT_TRUE(events[0].is_readable());

    std::array<std::byte, 16> buffer{};
    const auto [size, peer] = receiver.recv_from(buffer);
    EXPECT_EQ(size, std::strlen(payload));
    EXPECT_EQ(peer.port(), sender.local_endpoint().port());

    try {
        receiver.recv_from(buffer);
        FAIL() << "empty UDP socket should have returned would-block";
    } catch (const std::system_error& error) {
        EXPECT_TRUE(ioxx::is_would_block(error.code()));
    }

    const auto payload2 = "pong";
    sender.send_to(payload2, std::strlen(payload2), receiver.local_endpoint());
    poll.poll(events, 2s);
    ASSERT_FALSE(events.is_empty());
    EXPECT_EQ(events[0].token(), ioxx::Token{7});
    
    const auto [size2, peer2] = receiver.recv_from(buffer);
    EXPECT_EQ(size2, std::strlen(payload2));
    EXPECT_EQ(peer2.port(), sender.local_endpoint().port());
}

TEST(UdpTests, UdpMtuAndDf) {
    auto sender = ioxx::net::UdpSocket::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    
    std::error_code ec;
    sender.set_dont_fragment(true, ec);
#if defined(__APPLE__)
    EXPECT_EQ(ec, std::errc::function_not_supported);
#else
    EXPECT_FALSE(ec);
#endif

    sender.connect(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 12345});
    
    auto mtu = sender.mtu(ec);
#if defined(__APPLE__)
    EXPECT_EQ(ec, std::errc::function_not_supported);
#else
    EXPECT_FALSE(ec);
    EXPECT_GT(mtu, 0); // Should have some MTU since we are connected to loopback
#endif
}

TEST(UdpTests, UdpVectoredIo) {
    ioxx::Poll poll;
    ioxx::Events events{8};

    auto sender = ioxx::net::UdpSocket::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    auto receiver = ioxx::net::UdpSocket::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});

    poll.registry().register_source(receiver, ioxx::Token{7}, ioxx::Interest::READABLE);

    const char* str1 = "vec-";
    const char* str2 = "ping";
    std::array<ioxx::net::IoSlice, 2> write_bufs = {
        ioxx::net::IoSlice(str1, std::strlen(str1)),
        ioxx::net::IoSlice(str2, std::strlen(str2))
    };

    sender.send_vectored_to(write_bufs, receiver.local_endpoint());

    poll.poll(events, 2s);
    ASSERT_FALSE(events.is_empty());
    EXPECT_EQ(events[0].token(), ioxx::Token{7});
    EXPECT_TRUE(events[0].is_readable());

    std::array<std::byte, 4> buf1{};
    std::array<std::byte, 4> buf2{};
    std::array<ioxx::net::IoSliceMut, 2> read_bufs = {
        ioxx::net::IoSliceMut(buf1.data(), buf1.size()),
        ioxx::net::IoSliceMut(buf2.data(), buf2.size())
    };

    const auto [size, peer] = receiver.recv_vectored_from(read_bufs);
    EXPECT_EQ(size, 8);
    EXPECT_EQ(peer.port(), sender.local_endpoint().port());
    EXPECT_EQ(std::memcmp(buf1.data(), "vec-", 4), 0);
    EXPECT_EQ(std::memcmp(buf2.data(), "ping", 4), 0);
}

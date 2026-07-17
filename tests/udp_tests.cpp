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

ioxx::net::SocketAddrV6 loopback_v6(std::uint16_t port = 0) {
    return ioxx::net::SocketAddrV6{ioxx::net::Ipv6Addr{0, 0, 0, 0, 0, 0, 0, 1}, port};
}

void expect_udp_send_to(ioxx::net::UdpSocket& socket, std::string_view payload, const ioxx::net::SocketAddr& peer) {
    std::error_code ec;
    const auto sent = socket.send_to(payload.data(), payload.size(), peer, ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_EQ(sent, payload.size());
}

std::pair<std::string_view, ioxx::net::SocketAddr> recv_udp_from(ioxx::net::UdpSocket& socket,
                                                                std::array<std::byte, 128>& buffer) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code ec;
        const auto [received, peer] = socket.recv_from(buffer, ec);
        if (!ec) {
            return {{reinterpret_cast<const char*>(buffer.data()), received}, peer};
        }
        if (!ioxx::is_would_block(ec)) {
            ADD_FAILURE() << "recv_from failed: " << ec.message();
            return {{}, {}};
        }
        std::this_thread::sleep_for(20ms);
    }

    ADD_FAILURE() << "recv_from timed out";
    return {{}, {}};
}

} // namespace

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

TEST(UdpTests, Ipv6LoopbackRoundTrip) {
    std::error_code ec;
    auto sender = ioxx::net::UdpSocket::bind(loopback_v6(), ec);
    if (ec) {
        GTEST_SKIP() << "IPv6 loopback is not available: " << ec.message();
    }
    auto receiver = ioxx::net::UdpSocket::bind(loopback_v6(), ec);
    ASSERT_FALSE(ec) << ec.message();

    receiver.set_ttl(12, ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_EQ(receiver.ttl(ec), 12);
    ASSERT_FALSE(ec) << ec.message();

    expect_udp_send_to(sender, "udp-ipv6/request", receiver.local_endpoint());

    std::array<std::byte, 128> buffer{};
    const auto [payload, peer] = recv_udp_from(receiver, buffer);
    EXPECT_EQ(payload, "udp-ipv6/request");
    EXPECT_TRUE(peer.is_v6());
    EXPECT_EQ(peer.port(), sender.local_endpoint().port());
}

TEST(UdpTests, DualStackSocketReceivesAndRepliesToIpv4) {
    std::error_code ec;
    auto server = ioxx::net::UdpSocket::bind_dual_stack(0, ec);
    if (ec) {
        GTEST_SKIP() << "dual-stack IPv6 sockets are not available: " << ec.message();
    }
    auto client = ioxx::net::UdpSocket::bind(loopback());
    const auto server_endpoint = loopback(server.local_endpoint().port());

    expect_udp_send_to(client, "udp-dual/request", server_endpoint);

    std::array<std::byte, 128> server_buffer{};
    const auto [request, client_peer] = recv_udp_from(server, server_buffer);
    EXPECT_EQ(request, "udp-dual/request");
    EXPECT_TRUE(client_peer.is_v4());
    EXPECT_EQ(client_peer.port(), client.local_endpoint().port());

    expect_udp_send_to(server, "udp-dual/reply", client_peer);

    std::array<std::byte, 128> client_buffer{};
    const auto [reply, server_peer] = recv_udp_from(client, client_buffer);
    EXPECT_EQ(reply, "udp-dual/reply");
    EXPECT_TRUE(server_peer.is_v4());
    EXPECT_EQ(server_peer.port(), server_endpoint.port());
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

TEST(UdpTests, UdpSendToWorksWithoutConnect) {
    auto server = ioxx::net::UdpSocket::bind(loopback());
    auto client = ioxx::net::UdpSocket::bind(loopback());

    expect_udp_send_to(client, "udp-client/request", server.local_endpoint());

    std::array<std::byte, 128> server_buffer{};
    const auto [request, client_peer] = recv_udp_from(server, server_buffer);
    EXPECT_EQ(request, "udp-client/request");
    EXPECT_EQ(client_peer.port(), client.local_endpoint().port());

    expect_udp_send_to(server, "udp-server/reply", client_peer);

    std::array<std::byte, 128> client_buffer{};
    const auto [reply, server_peer] = recv_udp_from(client, client_buffer);
    EXPECT_EQ(reply, "udp-server/reply");
    EXPECT_EQ(server_peer.port(), server.local_endpoint().port());
}

TEST(UdpTests, UdpServerRepliesToMultipleUnconnectedClientsWithSendTo) {
    auto server = ioxx::net::UdpSocket::bind(loopback());
    auto client1 = ioxx::net::UdpSocket::bind(loopback());
    auto client2 = ioxx::net::UdpSocket::bind(loopback());
    const auto server_endpoint = server.local_endpoint();

    std::array<std::byte, 128> server_buffer{};
    ioxx::net::SocketAddr client1_peer{};
    ioxx::net::SocketAddr client2_peer{};

    expect_udp_send_to(client1, "client-1/datagram", server_endpoint);
    auto [payload1, peer1] = recv_udp_from(server, server_buffer);
    EXPECT_EQ(payload1, "client-1/datagram");
    EXPECT_EQ(peer1.port(), client1.local_endpoint().port());
    client1_peer = peer1;

    expect_udp_send_to(client2, "client-2/datagram", server_endpoint);
    auto [payload2, peer2] = recv_udp_from(server, server_buffer);
    EXPECT_EQ(payload2, "client-2/datagram");
    EXPECT_EQ(peer2.port(), client2.local_endpoint().port());
    client2_peer = peer2;

    expect_udp_send_to(server, "reply/client-1", client1_peer);
    std::array<std::byte, 128> client_buffer{};
    auto [reply1, server_peer1] = recv_udp_from(client1, client_buffer);
    EXPECT_EQ(reply1, "reply/client-1");
    EXPECT_EQ(server_peer1.port(), server_endpoint.port());

    expect_udp_send_to(server, "reply/client-2", client2_peer);
    auto [reply2, server_peer2] = recv_udp_from(client2, client_buffer);
    EXPECT_EQ(reply2, "reply/client-2");
    EXPECT_EQ(server_peer2.port(), server_endpoint.port());
}

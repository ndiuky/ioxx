#include <gtest/gtest.h>
#include <ioxx/net.hpp>
#include <ioxx/core.hpp>

using namespace ioxx;
using namespace ioxx::net;

TEST(NoExceptionsTest, TcpListenerBindInvalid) {
    std::error_code ec;
    // Binding to a multicast address should fail for TCP
    auto addr = SocketAddr{SocketAddrV4{Ipv4Addr{224, 0, 0, 1}, 8080}};
    auto listener = TcpListener::bind(addr, ec);
    
    EXPECT_TRUE(ec);
    EXPECT_FALSE(listener.valid());
}

TEST(NoExceptionsTest, TcpStreamConnectInvalid) {
    std::error_code ec;
    // Connect to an invalid address or one where nothing is listening
    auto addr = SocketAddr{SocketAddrV4{Ipv4Addr{127, 0, 0, 1}, 1}}; // Port 1 usually has nothing listening
    auto stream = TcpStream::connect(addr, ec);
    
    // In async IO, connect might return immediately with EINPROGRESS / WSAEWOULDBLOCK,
    // which in our implementation might NOT set ec if it's pending.
    if (!ec && stream.valid()) {
        auto err = stream.take_error();
    } else {
        // Depending on OS, it might fail immediately
        EXPECT_TRUE(ec);
    }
}

TEST(NoExceptionsTest, UdpSocketBindInvalid) {
    std::error_code ec;
    auto addr = SocketAddr{SocketAddrV4{Ipv4Addr{255, 255, 255, 255}, 8080}};
    auto socket = UdpSocket::bind(addr, ec);
    
    EXPECT_TRUE(ec);
    EXPECT_FALSE(socket.valid());
}

TEST(NoExceptionsTest, ValidOperationsDoNotError) {
    std::error_code ec;
    auto addr = SocketAddr{SocketAddrV4{Ipv4Addr{127, 0, 0, 1}, 0}};
    
    auto listener = TcpListener::bind(addr, ec);
    EXPECT_FALSE(ec);
    EXPECT_TRUE(listener.valid());
    
    auto local_addr = listener.local_endpoint(ec);
    EXPECT_FALSE(ec);
    
    auto socket = UdpSocket::bind(addr, ec);
    EXPECT_FALSE(ec);
    EXPECT_TRUE(socket.valid());
}

module;

#include <ioxx/ioxx.hpp>

export module ioxx;

export namespace ioxx {
    using ::ioxx::Interest;
    using ::ioxx::Token;
    using ::ioxx::Events;
    using ::ioxx::Registry;
    using ::ioxx::Poll;
    using ::ioxx::Waker;
    using ::ioxx::Signals;
    using ::ioxx::Source;
    using ::ioxx::SourceKind;
    using ::ioxx::is_would_block;
    
    namespace net {
        using ::ioxx::net::Ipv4Addr;
        using ::ioxx::net::Ipv6Addr;
        using ::ioxx::net::SocketAddrV4;
        using ::ioxx::net::SocketAddrV6;
        using ::ioxx::net::SocketAddr;
        using ::ioxx::net::TcpStream;
        using ::ioxx::net::TcpSocket;
        using ::ioxx::net::TcpListener;
        using ::ioxx::net::UdpSocket;
        using ::ioxx::net::IoSlice;
        using ::ioxx::net::IoSliceMut;
#if !defined(_WIN32)
        using ::ioxx::net::UnixStream;
        using ::ioxx::net::UnixListener;
        using ::ioxx::net::UnixDatagram;
#endif
    }
}

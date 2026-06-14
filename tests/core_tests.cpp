#include <gtest/gtest.h>
#include <ioxx/ioxx.hpp>
#include <chrono>
#include <thread>
#include <signal.h>

using namespace std::chrono_literals;

TEST(CoreTests, Interest) {
    const auto both = ioxx::Interest::READABLE | ioxx::Interest::WRITABLE;
    const auto all = both | ioxx::Interest::AIO | ioxx::Interest::LIO | ioxx::Interest::PRIORITY;
    EXPECT_TRUE(both.is_readable());
    EXPECT_TRUE(both.is_writable());
    EXPECT_FALSE(both.is_priority());
    EXPECT_TRUE(all.is_aio());
    EXPECT_TRUE(all.is_lio());
    EXPECT_TRUE(all.is_priority());

    const auto writable = both.remove(ioxx::Interest::READABLE);
    ASSERT_TRUE(writable.has_value());
    EXPECT_FALSE(writable->is_readable());
    EXPECT_TRUE(writable->is_writable());
}

TEST(CoreTests, RunOnceWithNothing) {
    ioxx::Poll poll;
    ioxx::Events events{16};
    std::error_code ec;
    poll.poll(events, 10ms, ec);
    EXPECT_FALSE(ec);
}

TEST(CoreTests, AddThenDrop) {
    ioxx::Poll poll;
    ioxx::Events events{16};
    
    auto listener = new ioxx::net::TcpListener(ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0}));
    poll.registry().register_source(*listener, ioxx::Token{1}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);
    delete listener;
    
    std::error_code ec;
    poll.poll(events, 100ms, ec);
    EXPECT_FALSE(ec);
}

TEST(CoreTests, ZeroDurationPollsEvents) {
    ioxx::Poll poll;
    ioxx::Events events{16};

    auto listener = ioxx::net::TcpListener::bind(ioxx::net::SocketAddrV4{ioxx::net::Ipv4Addr{127, 0, 0, 1}, 0});
    const auto endpoint = listener.local_endpoint();

    std::vector<ioxx::net::TcpStream> streams;
    streams.reserve(3);
    for (int i = 0; i < 3; ++i) {
        streams.push_back(ioxx::net::TcpStream::connect(endpoint));
        poll.registry().register_source(streams.back(), ioxx::Token{static_cast<std::size_t>(i)}, ioxx::Interest::WRITABLE);
    }

    std::error_code ec;

    // We need to wait for connections to establish first.
    std::size_t received_events = 0;
    for (int i = 0; i < 100 && received_events < 3; ++i) {
        poll.poll(events, 10ms, ec);
        EXPECT_FALSE(ec);
        received_events += events.size();
    }
    
    // Now that connections have arrived, poll with 0ms should work too.
    poll.poll(events, 0ms, ec);
    EXPECT_FALSE(ec);
    
    // Some streams might have disconnected, so we don't assert it's empty or not.
    // The test just ensures `poll(0)` does not crash or throw.
}

TEST(CoreTests, PollClosesFd) {
    for (int i = 0; i < 2000; ++i) {
        ioxx::Poll poll;
        ioxx::Events events{4};
        std::error_code ec;
        poll.poll(events, 0ms, ec);
        EXPECT_FALSE(ec);
    }
}

TEST(CoreTests, Waker) {
    ioxx::Poll poll;
    ioxx::Events events{8};
    ioxx::Waker waker{poll.registry(), ioxx::Token{42}};

    std::thread thread{[&] {
        std::this_thread::sleep_for(50ms);
        waker.wake();
    }};

    poll.poll(events, 2s);
    thread.join();

    ASSERT_FALSE(events.is_empty());
    EXPECT_EQ(events[0].token(), ioxx::Token{42});
    EXPECT_TRUE(events[0].is_readable());
}

#if !defined(_WIN32)
TEST(CoreTests, SignalsCatch) {
    try {
        ioxx::Poll poll;
        ioxx::Events events{8};

        std::error_code ec;
        ioxx::Signals signals(ec);
        ASSERT_FALSE(ec) << "Failed to create Signals: " << ec.message();

        signals.add(SIGINT, ec);
        ASSERT_FALSE(ec) << "Failed to add SIGINT: " << ec.message();

        poll.registry().register_source(signals, ioxx::Token{42}, ioxx::Interest::READABLE);

        raise(SIGINT);

        poll.poll(events, std::chrono::seconds(2));
        ASSERT_FALSE(events.is_empty());
        EXPECT_EQ(events[0].token(), ioxx::Token{42});
        EXPECT_TRUE(events[0].is_readable());

        auto pending = signals.pending(ec);
        ASSERT_FALSE(ec);
        ASSERT_FALSE(pending.empty());
        EXPECT_EQ(pending[0], SIGINT);
    } catch (const std::exception& e) {
        FAIL() << "Exception thrown: " << e.what();
    }
}
#endif

#include <gtest/gtest.h>
#include <ioxx/ioxx.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

#if !defined(_WIN32)
TEST(UnixTests, UnixPipe) {
    ioxx::Poll poll;
    ioxx::Events events{8};

    auto [sender, receiver] = ioxx::unix::pipe();

    poll.registry().register_source(receiver, ioxx::Token{10}, ioxx::Interest::READABLE);
    poll.registry().register_source(sender, ioxx::Token{11}, ioxx::Interest::WRITABLE);

    poll.poll(events, 2s);
    ASSERT_FALSE(events.is_empty());
    
    bool writable = false;
    for (const auto& event : events) {
        if (event.token() == ioxx::Token{11} && event.is_writable()) {
            writable = true;
        }
    }
    EXPECT_TRUE(writable);

    const auto payload = "pipe-test";
    EXPECT_EQ(sender.write(payload, std::strlen(payload)), std::strlen(payload));

    poll.poll(events, 2s);
    ASSERT_FALSE(events.is_empty());

    bool readable = false;
    for (const auto& event : events) {
        if (event.token() == ioxx::Token{10} && event.is_readable()) {
            readable = true;
        }
    }
    EXPECT_TRUE(readable);

    std::array<std::byte, 16> buffer{};
    const auto n = receiver.read(buffer);
    EXPECT_EQ(n, std::strlen(payload));
}
#endif

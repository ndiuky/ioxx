#include <gtest/gtest.h>
#include <ioxx/ioxx.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

#if defined(_WIN32)
TEST(WindowsTests, WindowsNamedPipeIocp) {
    ioxx::Poll poll;
    ioxx::Events events{8};
    const auto name = ioxx::windows::pipe_name(
        L"ioxx-cxx20-test-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
        std::to_wstring(::GetTickCount64()));

    auto server = ioxx::windows::NamedPipe::create(name);
    poll.registry().register_source(server, ioxx::Token{77}, ioxx::Interest::READABLE | ioxx::Interest::WRITABLE);

    try {
        server.connect();
    } catch (const std::system_error& error) {
        EXPECT_EQ(error.code(), std::make_error_code(std::errc::operation_would_block));
    }

    auto client = ioxx::windows::NamedPipe::open_client(name);

    poll.poll(events, 2s);

    ASSERT_FALSE(events.is_empty());
    EXPECT_EQ(events[0].token(), ioxx::Token{77});
    EXPECT_TRUE(events[0].is_writable());
    server.connect();

    std::array<std::byte, 16> buffer{};
    try {
        server.read(buffer);
        FAIL() << "pending named-pipe read should have returned would-block";
    } catch (const std::system_error& error) {
        EXPECT_EQ(error.code(), std::make_error_code(std::errc::operation_would_block));
    }

    const auto payload = "pipe";
    EXPECT_EQ(client.write(payload), std::strlen(payload));
    poll.poll(events, 2s);
    ASSERT_FALSE(events.is_empty());
    const auto n = server.read(buffer);
    EXPECT_EQ(n, std::strlen(payload));
}
#endif

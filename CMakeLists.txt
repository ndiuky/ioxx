cmake_minimum_required(VERSION 3.22)

project(ioxx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ioxx "")
target_sources(ioxx
    PRIVATE
        src/core.cpp
        src/net.cpp
        src/socket.cpp
        src/signals.cpp
        src/unix.cpp
        src/windows.cpp
)

option(IOXX_BUILD_MODULES "Build C++20 module interface" OFF)
if(IOXX_BUILD_MODULES)
    if(CMAKE_VERSION VERSION_LESS "3.28.0")
        message(FATAL_ERROR "Building C++20 modules requires CMake 3.28.0 or newer.")
    endif()
    target_sources(ioxx PUBLIC FILE_SET CXX_MODULES FILES src/ioxx.cppm)
endif()

add_library(ioxx::ioxx ALIAS ioxx)

target_compile_features(ioxx PUBLIC cxx_std_20)
target_include_directories(ioxx
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_definitions(ioxx PUBLIC "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:IOXX_SHARED>")
target_compile_definitions(ioxx PRIVATE "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:IOXX_EXPORTS>")

if(WIN32)
    target_compile_definitions(ioxx PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(ioxx PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(ioxx PUBLIC Threads::Threads)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(ioxx_INSTALL_TARGETS_ARGS
    EXPORT ioxxTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(IOXX_BUILD_MODULES)
    list(APPEND ioxx_INSTALL_TARGETS_ARGS CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ioxx)
endif()

install(TARGETS ioxx ${ioxx_INSTALL_TARGETS_ARGS})

install(DIRECTORY include/ioxx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

install(EXPORT ioxxTargets
    FILE ioxxTargets.cmake
    NAMESPACE ioxx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ioxx
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ioxxConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ioxxConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ioxxConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ioxx
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ioxxConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ioxxConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ioxx
)

if (BUILD_EXAMPLES)
    add_executable(ioxx_tcp_server examples/tcp_server.cpp)
    target_link_libraries(ioxx_tcp_server PRIVATE ioxx::ioxx)

    add_executable(ioxx_udp_echo examples/udp_echo.cpp)
    target_link_libraries(ioxx_udp_echo PRIVATE ioxx::ioxx)
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/gtest")
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(tests/gtest)

    add_executable(ioxx_tests
            tests/core_tests.cpp
            tests/no_exceptions_test.cpp
            tests/tcp_tests.cpp
            tests/udp_tests.cpp
            tests/unix_tests.cpp
            tests/windows_tests.cpp)
    target_link_libraries(ioxx_tests PRIVATE ioxx::ioxx gtest_main)

    enable_testing()
    add_test(NAME core_tests COMMAND ioxx_tests)
endif ()

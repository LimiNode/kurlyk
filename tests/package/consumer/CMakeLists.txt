cmake_minimum_required(VERSION 3.21)
project(kurlyk_package_consumer LANGUAGES CXX)

find_package(kurlyk CONFIG REQUIRED)
find_path(KURLYK_PACKAGE_INCLUDE_DIR kurlyk.hpp REQUIRED)

if(TARGET asio::asio)
    get_target_property(KURLYK_DIRECT_ASIO_INCLUDE_DIRS
        asio::asio INTERFACE_INCLUDE_DIRECTORIES
    )
else()
    find_path(KURLYK_DIRECT_ASIO_INCLUDE_DIR
        NAMES asio.hpp
        HINTS "${KURLYK_ASIO_INCLUDE_DIR}"
        REQUIRED
    )
    set(KURLYK_DIRECT_ASIO_INCLUDE_DIRS
        "${KURLYK_DIRECT_ASIO_INCLUDE_DIR}"
    )
endif()

if(TARGET OpenSSL::SSL)
    get_target_property(KURLYK_DIRECT_OPENSSL_INCLUDE_DIRS
        OpenSSL::SSL INTERFACE_INCLUDE_DIRECTORIES
    )
else()
    find_path(KURLYK_DIRECT_OPENSSL_INCLUDE_DIR
        NAMES openssl/ssl.h
        REQUIRED
    )
    set(KURLYK_DIRECT_OPENSSL_INCLUDE_DIRS
        "${KURLYK_DIRECT_OPENSSL_INCLUDE_DIR}"
    )
endif()

add_executable(kurlyk_package_consumer main.cpp)
target_compile_features(kurlyk_package_consumer PRIVATE cxx_std_11)
target_link_libraries(kurlyk_package_consumer PRIVATE kurlyk::kurlyk)

# Keep this target independent from the exported Kurlyk target. It models a
# direct/header consumer that supplies only the installed and transport include
# roots it needs.
add_executable(kurlyk_package_direct_websocket_consumer direct_websocket.cpp)
target_compile_features(kurlyk_package_direct_websocket_consumer PRIVATE cxx_std_11)
target_include_directories(kurlyk_package_direct_websocket_consumer PRIVATE
    "${KURLYK_PACKAGE_INCLUDE_DIR}"
    ${KURLYK_DIRECT_ASIO_INCLUDE_DIRS}
    ${KURLYK_DIRECT_OPENSSL_INCLUDE_DIRS}
)

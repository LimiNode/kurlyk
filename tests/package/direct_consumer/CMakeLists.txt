cmake_minimum_required(VERSION 3.21)
project(kurlyk_installed_direct_websocket_consumer LANGUAGES CXX)

set(KURLYK_INCLUDE_DIR "" CACHE PATH
    "Installed Kurlyk include directory"
)
if(NOT EXISTS "${KURLYK_INCLUDE_DIR}/kurlyk.hpp")
    message(FATAL_ERROR
        "KURLYK_INCLUDE_DIR must point to an installed Kurlyk include directory"
    )
endif()

find_path(KURLYK_ASIO_INCLUDE_DIR
    NAMES asio.hpp
    REQUIRED
)
find_path(KURLYK_OPENSSL_INCLUDE_DIR
    NAMES openssl/ssl.h
    REQUIRED
)

add_library(kurlyk_installed_direct_websocket_consumer OBJECT main.cpp)
target_compile_features(kurlyk_installed_direct_websocket_consumer PRIVATE cxx_std_11)
target_include_directories(kurlyk_installed_direct_websocket_consumer PRIVATE
    "${KURLYK_INCLUDE_DIR}"
    "${KURLYK_ASIO_INCLUDE_DIR}"
    "${KURLYK_OPENSSL_INCLUDE_DIR}"
)
set_target_properties(kurlyk_installed_direct_websocket_consumer PROPERTIES
    CXX_EXTENSIONS NO
)

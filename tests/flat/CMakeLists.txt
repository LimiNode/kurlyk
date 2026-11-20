cmake_minimum_required(VERSION 3.21)
project(kurlyk_flat_dependencies LANGUAGES CXX)

set(KURLYK_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "Kurlyk source checkout")
set(KURLYK_TEST_ASIO_INCLUDE_DIR "" CACHE PATH "Parent-provided Asio include directory")
set(KURLYK_TEST_SWS_SOURCE_DIR "" CACHE PATH "Parent-provided Simple-WebSocket-Server source")
set(KURLYK_TEST_JSON_INCLUDE_DIR "" CACHE PATH "Parent-provided JSON include directory")
option(KURLYK_TEST_LOCAL_HTTP_PROVIDER "Resolve HTTP server through its local-source option" OFF)
option(KURLYK_TEST_INSTALLED "Consume installed Kurlyk instead of its source tree" OFF)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(parent_asio INTERFACE)
target_include_directories(parent_asio INTERFACE "${KURLYK_TEST_ASIO_INCLUDE_DIR}")
target_compile_definitions(parent_asio INTERFACE ASIO_STANDALONE)
add_library(asio::asio ALIAS parent_asio)
add_library(simple_ws_server INTERFACE)
target_include_directories(simple_ws_server INTERFACE "${KURLYK_TEST_SWS_SOURCE_DIR}")
target_link_libraries(simple_ws_server INTERFACE asio::asio OpenSSL::SSL OpenSSL::Crypto)

add_library(parent_json INTERFACE)
target_include_directories(parent_json INTERFACE "${KURLYK_TEST_JSON_INCLUDE_DIR}")
add_library(nlohmann_json::nlohmann_json ALIAS parent_json)

# Any dependency acquisition is a test failure, even with an available network.
include(FetchContent)
function(FetchContent_Populate)
    message(FATAL_ERROR "Flat dependency regression attempted FetchContent_Populate")
endfunction()
set(KURLYK_ALLOW_DOWNLOADS OFF CACHE BOOL "" FORCE)

if(KURLYK_TEST_INSTALLED)
    find_package(kurlyk CONFIG REQUIRED)
    add_executable(flat_installed_consumer ../package/consumer/main.cpp)
    target_compile_definitions(flat_installed_consumer PRIVATE KURLYK_AUTO_INIT=0)
    target_link_libraries(flat_installed_consumer PRIVATE kurlyk::kurlyk)
else()
    if(NOT KURLYK_TEST_LOCAL_HTTP_PROVIDER)
        add_library(simple_web_server INTERFACE)
        target_include_directories(simple_web_server INTERFACE
            "${KURLYK_SIMPLE_WEB_SERVER_SOURCE_DIR}")
        target_link_libraries(simple_web_server INTERFACE asio::asio OpenSSL::SSL OpenSSL::Crypto)
        # An existing target must win even over an invalid explicit source path.
        set(KURLYK_SIMPLE_WEB_SERVER_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/absent")
    endif()
    enable_testing()
    add_subdirectory("${KURLYK_SOURCE_DIR}/tests/integration" integration)
endif()

cmake_minimum_required(VERSION 3.21)

# Preserve legacy FetchContent_Populate behavior to avoid CMake 3.30+ deprecation warnings
# for fallback dependencies that populate manually (imported targets, interface libraries).
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(kurlyk VERSION 1.0.2 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
# options

option(KURLYK_USE_STANDALONE_ASIO "Use Asio standalone instead of Boost.Asio" ON)
option(KURLYK_BUILD_EXAMPLES "Build repository examples" OFF)
option(KURLYK_AUTH_SUPPORT "Enable HTTP authentication helpers (Bearer, API-key)" ON)
option(KURLYK_OAUTH_SUPPORT "Enable OAuth2 PKCE client (requires OpenSSL)" ON)

option(KURLYK_USE_FALLBACK_OPENSSL "Use fallback for OpenSSL" OFF)
option(KURLYK_USE_FALLBACK_CURL "Use fallback for libcurl" OFF)
option(KURLYK_USE_FALLBACK_ASIO "Use fallback for asio" OFF)
option(KURLYK_USE_FALLBACK_SIMPLE_WS_SERVER "Use fallback for simple websocket server" OFF)
option(KURLYK_OPENSSL_SHARED "For falllback option: OpenSSL shared" ON)
option(KURLYK_CURL_SHARED "For falllback option: libcurl shared" ON)

# Our header-only library
add_library(kurlyk INTERFACE)
add_library(kurlyk::kurlyk ALIAS kurlyk)
target_compile_features(kurlyk INTERFACE cxx_std_11)
target_include_directories(kurlyk INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

# Deps
#openssl
include(cmake/deps/openssl.cmake)
use_or_fetch_openssl(kurlyk)

#curl
include(cmake/deps/curl.cmake)
use_or_fetch_curl(kurlyk)

#asio
include(cmake/deps/asio.cmake)
use_or_fetch_asio(kurlyk)

# simple web-socket server
include(cmake/deps/simple_ws_server.cmake)
use_or_fetch_simple_ws_server(kurlyk)

# Authentication helpers (lightweight — no extra deps)
if(KURLYK_AUTH_SUPPORT)
    target_compile_definitions(kurlyk INTERFACE KURLYK_AUTH_SUPPORT=1)
else()
    target_compile_definitions(kurlyk INTERFACE KURLYK_AUTH_SUPPORT=0)
endif()

if(KURLYK_OAUTH_SUPPORT AND NOT KURLYK_AUTH_SUPPORT)
    message(FATAL_ERROR "KURLYK_OAUTH_SUPPORT requires KURLYK_AUTH_SUPPORT")
endif()

# OAuth2 PKCE (uses OpenSSL for SHA-256 and CSPRNG)
if(KURLYK_OAUTH_SUPPORT)
    target_compile_definitions(kurlyk INTERFACE KURLYK_OAUTH_SUPPORT=1)
else()
    target_compile_definitions(kurlyk INTERFACE KURLYK_OAUTH_SUPPORT=0)
endif()

# OS-specific dependencies
if (WIN32)
    include(cmake/deps/win_deps.cmake)
elseif(UNIX)
    include(cmake/deps/unix_deps.cmake)
else()
	message(FATAL_ERROR "Unsupported platform.")
endif()
use_os_deps(kurlyk)

# Keep the installed target independent from build-tree fallback targets. The
# proxies are imported interface targets at build time, so the export file can
# refer to them without trying to install third-party build targets.
function(kurlyk_add_package_dependency_proxy proxy_target dependency_target)
    if(NOT TARGET ${dependency_target})
        message(FATAL_ERROR "Kurlyk package dependency target is missing: ${dependency_target}")
    endif()

    add_library(${proxy_target} INTERFACE IMPORTED GLOBAL)
    set_property(TARGET ${proxy_target} PROPERTY
        INTERFACE_LINK_LIBRARIES "${dependency_target}"
    )
endfunction()

kurlyk_add_package_dependency_proxy(kurlyk_package_curl CURL::libcurl)
kurlyk_add_package_dependency_proxy(kurlyk_package_ssl OpenSSL::SSL)
kurlyk_add_package_dependency_proxy(kurlyk_package_crypto OpenSSL::Crypto)
if(TARGET Threads::Threads)
    kurlyk_add_package_dependency_proxy(kurlyk_package_threads Threads::Threads)
else()
    add_library(kurlyk_package_threads INTERFACE IMPORTED GLOBAL)
endif()

if(KURLYK_USE_STANDALONE_ASIO)
    kurlyk_add_package_dependency_proxy(kurlyk_package_asio asio::asio)
else()
    kurlyk_add_package_dependency_proxy(kurlyk_package_asio Boost::boost)
endif()

kurlyk_add_package_dependency_proxy(kurlyk_package_simple_ws simple_ws_server)

if(KURLYK_OAUTH_SUPPORT)
    kurlyk_add_package_dependency_proxy(kurlyk_package_hmac hmac_cpp::hmac_cpp)
endif()

add_library(kurlyk_package INTERFACE)
set_target_properties(kurlyk_package PROPERTIES EXPORT_NAME kurlyk)
target_compile_features(kurlyk_package INTERFACE cxx_std_11)
target_include_directories(kurlyk_package INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_compile_definitions(kurlyk_package INTERFACE
    KURLYK_AUTH_SUPPORT=$<BOOL:${KURLYK_AUTH_SUPPORT}>
    KURLYK_OAUTH_SUPPORT=$<BOOL:${KURLYK_OAUTH_SUPPORT}>
)
if(KURLYK_USE_STANDALONE_ASIO)
    target_compile_definitions(kurlyk_package INTERFACE ASIO_STANDALONE)
endif()
target_link_libraries(kurlyk_package INTERFACE
    kurlyk_package_curl
    kurlyk_package_ssl
    kurlyk_package_crypto
    kurlyk_package_threads
    kurlyk_package_asio
    kurlyk_package_simple_ws
)
if(KURLYK_OAUTH_SUPPORT)
    target_link_libraries(kurlyk_package INTERFACE kurlyk_package_hmac)
endif()
if(CMAKE_DL_LIBS)
    target_link_libraries(kurlyk_package INTERFACE ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    target_link_libraries(kurlyk_package INTERFACE ws2_32 crypt32 wsock32)
endif()

if(KURLYK_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

install(TARGETS kurlyk_package EXPORT kurlykTargets)
file(GLOB_RECURSE KURLYK_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)
foreach(public_header IN LISTS KURLYK_PUBLIC_HEADERS)
    file(RELATIVE_PATH public_header_relative
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${public_header}"
    )
    get_filename_component(public_header_directory
        "${public_header_relative}"
        DIRECTORY
    )
    install(FILES "${public_header}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${public_header_directory}"
    )
endforeach()

set(KURLYK_PACKAGE_BUNDLED_SIMPLE_WS OFF)
set(KURLYK_SIMPLE_WS_HEADERS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/Simple-WebSocket-Server"
)
if(EXISTS "${KURLYK_SIMPLE_WS_HEADERS_DIR}/client_ws.hpp")
    set(KURLYK_PACKAGE_BUNDLED_SIMPLE_WS ON)
    install(FILES
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/asio_compatibility.hpp"
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/client_ws.hpp"
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/server_ws.hpp"
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/server_wss.hpp"
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/client_wss.hpp"
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/crypto.hpp"
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/utility.hpp"
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/status_code.hpp"
        "${KURLYK_SIMPLE_WS_HEADERS_DIR}/mutex.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kurlyk/third_party/simple-websocket-server"
    )
endif()

set(KURLYK_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/kurlyk")

install(EXPORT kurlykTargets
    FILE kurlykTargets.cmake
    NAMESPACE kurlyk::
    DESTINATION "${KURLYK_INSTALL_CMAKEDIR}"
)

configure_package_config_file(
    cmake/kurlykConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/kurlykConfig.cmake"
    INSTALL_DESTINATION "${KURLYK_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/kurlykConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/kurlykConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/kurlykConfigVersion.cmake"
    DESTINATION "${KURLYK_INSTALL_CMAKEDIR}"
)

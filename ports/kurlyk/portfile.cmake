vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO LimiNode/kurlyk
    REF dcfab645f0dadd15c6ec200e25c1e2273fae429e
    SHA512 73eca9d95a0bef80d226487b122bd3f1f7ffa27dc7ed086fda0c2bbd17582bbdca738e42325b83a6f4e281a6edd5fe371b49d0d7bf49fdf3ecc9b731442ebb63
)

# Simple-WebSocket-Server is used as a header-only dependency and is not a
# vcpkg port. Fetch it during the port source stage so the CMake install rules
# can bundle the headers required by the exported kurlyk target.
vcpkg_from_git(
    OUT_SOURCE_PATH SIMPLE_WS_SOURCE_PATH
    URL https://gitlab.com/eidheim/Simple-WebSocket-Server.git
    REF 7bb2867b9d50ff559c60b99178fd46531daa2c7e
)
file(COPY "${SIMPLE_WS_SOURCE_PATH}/"
     DESTINATION "${SOURCE_PATH}/external/Simple-WebSocket-Server")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DKURLYK_BUILD_EXAMPLES=OFF
        -DKURLYK_USE_FALLBACK_ASIO=OFF
        -DKURLYK_USE_FALLBACK_CURL=OFF
        -DKURLYK_USE_FALLBACK_OPENSSL=OFF
        -DKURLYK_USE_FALLBACK_SIMPLE_WS_SERVER=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/kurlyk)

# kurlyk is header-only; a debug tree would only contain duplicate headers and
# package metadata. Keep the vcpkg package free of that unused tree.
file(REMOVE "${CURRENT_PACKAGES_DIR}/include/AGENTS.md")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")

vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/LICENSE"
        "${SIMPLE_WS_SOURCE_PATH}/LICENSE"
)

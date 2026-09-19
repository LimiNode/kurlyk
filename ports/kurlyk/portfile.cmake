vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO LimiNode/kurlyk
    REF 3c671cfd256084e59c1e0703ebd80b364d7d5922
    SHA512 882ce42560562702c87ccaf79704439965390f910d64d10380beb89c1fc82aff28a5ac38c79fcdb514417647f5f0d7517ea7cb5f706226de53cd6a9655e9b559
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

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO LimiNode/kurlyk
    REF 1e1123ad5c456e2b72c43ea2d6192cc10c08b46d
    SHA512 eeed2c6b65995632ea46ee38f275ce49548ee484c6c9c4db8d2cfd468cf3504a373f0fd7a01439abe330af968354e4ab67449aa15774effcc96d45846974ec7f
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

# Keep the port usage guidance next to the installed package metadata.
file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)

# kurlyk is header-only; a debug tree would only contain duplicate headers and
# package metadata. Keep the vcpkg package free of that unused tree.
file(REMOVE "${CURRENT_PACKAGES_DIR}/include/AGENTS.md")
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug"
    "${CURRENT_PACKAGES_DIR}/lib"
)

vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/LICENSE"
        "${SIMPLE_WS_SOURCE_PATH}/LICENSE"
)

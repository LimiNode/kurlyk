# Optional listener does not link the HTTP/WebSocket clients or NetworkWorker.
add_library(kurlyk_http_server INTERFACE)
add_library(kurlyk::http_server ALIAS kurlyk_http_server)
set_target_properties(kurlyk_http_server PROPERTIES EXPORT_NAME http_server)
target_compile_features(kurlyk_http_server INTERFACE cxx_std_11)
target_include_directories(kurlyk_http_server INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

include(cmake/deps/fallbacks/load_simple_web_server.cmake)
load_simple_web_server(kurlyk_http_server)
target_link_libraries(kurlyk_http_server INTERFACE
    kurlyk_package_asio kurlyk_package_threads)
if(KURLYK_USE_STANDALONE_ASIO)
    target_compile_definitions(kurlyk_http_server INTERFACE ASIO_STANDALONE)
endif()
if(WIN32)
    target_link_libraries(kurlyk_http_server INTERFACE ws2_32 mswsock)
endif()

# Imported dependency proxy keeps build-tree providers out of installed exports.
kurlyk_add_package_dependency_proxy(kurlyk_package_http_server simple_web_server)
get_target_property(server_links kurlyk_http_server INTERFACE_LINK_LIBRARIES)
list(REMOVE_ITEM server_links simple_web_server)
list(APPEND server_links kurlyk_package_http_server)
set_property(TARGET kurlyk_http_server PROPERTY INTERFACE_LINK_LIBRARIES "${server_links}")
install(TARGETS kurlyk_http_server EXPORT kurlykTargets)

set(server_source "${KURLYK_SIMPLE_WEB_SERVER_SOURCE_DIR}")
if(NOT server_source)
    set(server_source "${CMAKE_CURRENT_SOURCE_DIR}/external/Simple-Web-Server")
endif()
if(EXISTS "${server_source}/server_http.hpp")
    install(FILES
        "${server_source}/server_http.hpp"
        "${server_source}/asio_compatibility.hpp"
        "${server_source}/utility.hpp"
        "${server_source}/status_code.hpp"
        "${server_source}/mutex.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/simple-web-server")
    install(FILES "${server_source}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/kurlyk"
        RENAME Simple-Web-Server-LICENSE)
endif()
configure_file(cmake/simple_web_server.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/kurlyk/detail/simple_web_server.hpp" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/kurlyk/detail/simple_web_server.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kurlyk/detail")

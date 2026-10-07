cmake_minimum_required(VERSION 3.21)
project(kurlyk_full_cpp11_smoke LANGUAGES CXX)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.." "${CMAKE_BINARY_DIR}/kurlyk")

add_library(full_cpp11_smoke OBJECT full_cpp11_smoke.cpp)
target_link_libraries(full_cpp11_smoke PRIVATE kurlyk)
set_target_properties(full_cpp11_smoke PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
if(KURLYK_USE_STANDALONE_ASIO)
    target_compile_definitions(full_cpp11_smoke PRIVATE
        KURLYK_EXPECT_STANDALONE=1
    )
else()
    target_compile_definitions(full_cpp11_smoke PRIVATE
        KURLYK_EXPECT_BOOST=1
    )
endif()

if(KURLYK_USE_BUNDLED_SIMPLE_WS_SERVER AND
        EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../external/Simple-WebSocket-Server/client_ws.hpp")
    add_library(direct_websocket_backend_smoke OBJECT websocket_backend_smoke.cpp)
    target_include_directories(direct_websocket_backend_smoke PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../../include"
        "${CMAKE_CURRENT_LIST_DIR}/../../external/Simple-WebSocket-Server"
        "${CMAKE_CURRENT_LIST_DIR}/../../external/asio/include"
    )
    target_link_libraries(direct_websocket_backend_smoke PRIVATE kurlyk_package_ssl)
    target_compile_features(direct_websocket_backend_smoke PRIVATE cxx_std_11)
    set_target_properties(direct_websocket_backend_smoke PROPERTIES
        CXX_EXTENSIONS NO
    )
    if(KURLYK_USE_STANDALONE_ASIO)
        target_compile_definitions(direct_websocket_backend_smoke PRIVATE
            KURLYK_EXPECT_STANDALONE=1
        )
    else()
        target_link_libraries(direct_websocket_backend_smoke PRIVATE kurlyk_package_asio)
        target_compile_definitions(direct_websocket_backend_smoke PRIVATE
            KURLYK_EXPECT_BOOST=1
            KURLYK_USE_BOOST_ASIO=1
        )
    endif()
else()
    message(STATUS "Skipping source-tree direct WebSocket smoke: SWS submodule is not checked out")
endif()

add_library(http_only_backend_config_smoke OBJECT http_only_backend_config_smoke.cpp)
target_include_directories(http_only_backend_config_smoke PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
)
target_link_libraries(http_only_backend_config_smoke PRIVATE kurlyk_package_curl)
target_compile_features(http_only_backend_config_smoke PRIVATE cxx_std_11)
set_target_properties(http_only_backend_config_smoke PROPERTIES
    CXX_EXTENSIONS NO
)

try_compile(KURLYK_CONFLICT_COMPILES
    "${CMAKE_CURRENT_BINARY_DIR}/asio_backend_conflict"
    "${CMAKE_CURRENT_LIST_DIR}/asio_backend_conflict.cpp"
    CMAKE_FLAGS
        "-DCMAKE_CXX_STANDARD=11"
        "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
    OUTPUT_VARIABLE KURLYK_CONFLICT_OUTPUT
)
if(KURLYK_CONFLICT_COMPILES)
    message(FATAL_ERROR
        "KURLYK_USE_BOOST_ASIO/ASIO_STANDALONE conflict was not rejected"
    )
endif()
if(NOT KURLYK_CONFLICT_OUTPUT MATCHES
        "KURLYK_USE_BOOST_ASIO conflicts with ASIO_STANDALONE")
    message(FATAL_ERROR
        "Conflict test failed without the expected diagnostic. Output:\n"
        "${KURLYK_CONFLICT_OUTPUT}"
    )
endif()

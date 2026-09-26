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

add_library(direct_websocket_backend_smoke OBJECT websocket_backend_smoke.cpp)
target_include_directories(direct_websocket_backend_smoke PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${CMAKE_CURRENT_LIST_DIR}/../../external/Simple-WebSocket-Server"
    "${CMAKE_CURRENT_LIST_DIR}/../../external/asio/include"
)
target_link_libraries(direct_websocket_backend_smoke PRIVATE OpenSSL::SSL)
target_compile_features(direct_websocket_backend_smoke PRIVATE cxx_std_11)
set_target_properties(direct_websocket_backend_smoke PROPERTIES
    CXX_EXTENSIONS NO
)
if(KURLYK_USE_STANDALONE_ASIO)
    target_compile_definitions(direct_websocket_backend_smoke PRIVATE
        KURLYK_EXPECT_STANDALONE=1
    )
else()
    target_link_libraries(direct_websocket_backend_smoke PRIVATE Boost::boost)
    target_compile_definitions(direct_websocket_backend_smoke PRIVATE
        KURLYK_EXPECT_BOOST=1
        KURLYK_USE_BOOST_ASIO=1
    )
endif()

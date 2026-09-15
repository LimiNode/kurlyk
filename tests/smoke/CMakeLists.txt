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

cmake_minimum_required(VERSION 3.21)
project(kurlyk_installed_sws_consumer LANGUAGES CXX)

set(KURLYK_SOURCE_DIR "" CACHE PATH "Kurlyk source directory")
if(NOT EXISTS "${KURLYK_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "KURLYK_SOURCE_DIR must point to the Kurlyk source tree")
endif()

set(KURLYK_USE_BUNDLED_SIMPLE_WS_SERVER OFF CACHE BOOL "" FORCE)
set(KURLYK_USE_FALLBACK_SIMPLE_WS_SERVER OFF CACHE BOOL "" FORCE)
add_subdirectory("${KURLYK_SOURCE_DIR}" kurlyk-source EXCLUDE_FROM_ALL)

add_executable(kurlyk_installed_sws_consumer main.cpp)
target_compile_features(kurlyk_installed_sws_consumer PRIVATE cxx_std_11)
target_link_libraries(kurlyk_installed_sws_consumer PRIVATE kurlyk)

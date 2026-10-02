cmake_minimum_required(VERSION 3.21)
project(kurlyk_package_consumer LANGUAGES CXX)

find_package(kurlyk CONFIG REQUIRED)

add_executable(kurlyk_package_consumer main.cpp)
target_compile_features(kurlyk_package_consumer PRIVATE cxx_std_11)
target_link_libraries(kurlyk_package_consumer PRIVATE kurlyk::kurlyk)

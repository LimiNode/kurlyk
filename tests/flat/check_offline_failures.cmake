if(NOT KURLYK_SOURCE_DIR OR NOT KURLYK_TEST_BUILD_DIR)
    message(FATAL_ERROR "KURLYK_SOURCE_DIR and KURLYK_TEST_BUILD_DIR are required")
endif()
set(configure_args "")
if(CMAKE_CXX_COMPILER)
    list(APPEND configure_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif()
if(CMAKE_GENERATOR)
    list(APPEND configure_args -G "${CMAKE_GENERATOR}")
endif()
foreach(loader IN ITEMS load_asio load_simple_ws_server load_simple_web_server
        load_curl_mingw load_curl_msvc load_openssl_mingw load_openssl_msvc)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -S "${KURLYK_SOURCE_DIR}/tests/flat/offline_failure"
        -B "${KURLYK_TEST_BUILD_DIR}/${loader}"
        "-DKURLYK_TEST_LOADER=${loader}"
        ${configure_args}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "KURLYK_ALLOW_DOWNLOADS=OFF")
        message(FATAL_ERROR "${loader} did not reject missing offline dependency: ${output}${error}")
    endif()
    if(EXISTS "${KURLYK_TEST_BUILD_DIR}/${loader}/_deps")
        message(FATAL_ERROR "${loader} attempted dependency population")
    endif()
endforeach()
message(STATUS "All seven missing-dependency loaders rejected offline configuration")

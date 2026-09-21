if(DEFINED KURLYK_TEST_OPENSSL_ROOT)
    add_library(OpenSSL::SSL SHARED IMPORTED GLOBAL)
    add_library(OpenSSL::Crypto SHARED IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${KURLYK_TEST_OPENSSL_ROOT}/include"
    )
    set_target_properties(OpenSSL::Crypto PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${KURLYK_TEST_OPENSSL_ROOT}/include"
    )
endif()

if(DEFINED KURLYK_TEST_CURL_ROOT)
    add_library(CURL::libcurl SHARED IMPORTED GLOBAL)
    set_target_properties(CURL::libcurl PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${KURLYK_TEST_CURL_ROOT}/include"
    )
endif()

if(NOT TARGET Threads::Threads)
    add_library(Threads::Threads INTERFACE IMPORTED GLOBAL)
endif()

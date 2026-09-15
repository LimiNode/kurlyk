#integration test
cmake_minimum_required(VERSION 3.21)
project(integration_test LANGUAGES CXX)

enable_testing()

#test options
option(KURLYK_TEST_PRELOAD_DEPS "Preload deps before adding kurlyk lib" OFF)

add_executable(integr_test
	get_singletons_a.cpp
	get_singletons_b.cpp
	get_singletons_main.cpp
)

add_executable(ws_local_test
	websocket_local_main.cpp
)

add_executable(backpressure_test
	backpressure_main.cpp
)

add_executable(rate_limit_lifetime_test
	rate_limit_lifetime_main.cpp
)

add_executable(request_group_cancel_test
	request_group_cancel_main.cpp
)

add_executable(http_client_rate_limit_api_test
	http_client_rate_limit_api_main.cpp
)

add_executable(http_client_wait_requests_test
	http_client_wait_requests_main.cpp
)

add_executable(http_client_auth_provider_test
	http_client_auth_provider_main.cpp
)

add_executable(sequential_rate_limit_test
	sequential_rate_limit_main.cpp
)

add_executable(rate_limit_partition_test
	rate_limit_partition_main.cpp
)

add_executable(http_local_test
	http_local_main.cpp
)

add_executable(http_retry_streaming_test
	http_retry_streaming_main.cpp
)

add_executable(http_client_destructor_test
	http_client_destructor_main.cpp
)

add_executable(proxy_checker_test
	proxy_checker_main.cpp
)

set(proxy_checker_test_ssl_cert
	"${CMAKE_CURRENT_SOURCE_DIR}/data/proxy_checker_server.pem")
set(proxy_checker_test_ssl_key
	"${CMAKE_CURRENT_SOURCE_DIR}/data/proxy_checker_server.key")
set(proxy_checker_test_ssl_ca
	"${CMAKE_CURRENT_SOURCE_DIR}/data/proxy_checker_ca.pem")
foreach(proxy_checker_test_ssl_file
		${proxy_checker_test_ssl_cert}
		${proxy_checker_test_ssl_key}
		${proxy_checker_test_ssl_ca})
	if(NOT EXISTS "${proxy_checker_test_ssl_file}")
		message(FATAL_ERROR "Proxy checker HTTPS test file is missing: ${proxy_checker_test_ssl_file}")
	endif()
endforeach()
file(TO_CMAKE_PATH "${proxy_checker_test_ssl_cert}" proxy_checker_test_ssl_cert)
file(TO_CMAKE_PATH "${proxy_checker_test_ssl_key}" proxy_checker_test_ssl_key)
file(TO_CMAKE_PATH "${proxy_checker_test_ssl_ca}" proxy_checker_test_ssl_ca)
target_compile_definitions(proxy_checker_test PRIVATE
	KURLYK_TEST_SSL_CERT_FILE="${proxy_checker_test_ssl_cert}"
	KURLYK_TEST_SSL_KEY_FILE="${proxy_checker_test_ssl_key}"
	KURLYK_TEST_SSL_CA_FILE="${proxy_checker_test_ssl_ca}"
)

list(APPEND CMAKE_MODULE_PATH 
	"${CMAKE_CURRENT_LIST_DIR}/../../cmake/deps"
	"${CMAKE_CURRENT_LIST_DIR}/../../cmake"
)

if(KURLYK_TEST_PRELOAD_DEPS)
message(STATUS "Start preload simple-ws-server, asio, OpenSSl")
#simple-ws-server
	include(fallbacks/load_simple_ws_server)
	load_simple_ws_server(integr_test)
#asio
	include(fallbacks/load_asio)
	load_asio(integr_test)
#openssl
	if (WIN32)
		if(MSVC)
			include(fallbacks/load_openssl_msvc)
		elseif(MINGW)
			include(fallbacks/load_openssl_mingw)
		else()
			message(FATAL_ERROR "Unknown compiler or there is no fallback for this compiler.")
		endif()
		load_openssl(integr_test)
	elseif(UNIX)
		find_package(OpenSSL REQUIRED)
		target_link_libraries(integr_test PRIVATE OpenSSL::SSL OpenSSL::Crypto)
	else()
		message(FATAL_ERROR "Unsupported platform for dependency preload.")
	endif()
	
endif()

add_subdirectory(../.. build)

target_link_libraries(integr_test PRIVATE kurlyk)
target_link_libraries(ws_local_test PRIVATE kurlyk)
target_link_libraries(backpressure_test PRIVATE kurlyk)
target_link_libraries(rate_limit_lifetime_test PRIVATE kurlyk)
target_link_libraries(request_group_cancel_test PRIVATE kurlyk)
target_link_libraries(http_client_rate_limit_api_test PRIVATE kurlyk)
target_link_libraries(http_client_wait_requests_test PRIVATE kurlyk)
target_include_directories(http_client_wait_requests_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../external/Simple-Web-Server")
target_link_libraries(http_client_auth_provider_test PRIVATE kurlyk)
target_include_directories(http_client_auth_provider_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../external/Simple-Web-Server")
target_link_libraries(sequential_rate_limit_test PRIVATE kurlyk)
target_link_libraries(rate_limit_partition_test PRIVATE kurlyk)
target_link_libraries(http_local_test PRIVATE kurlyk)
target_link_libraries(http_retry_streaming_test PRIVATE kurlyk)
target_link_libraries(http_client_destructor_test PRIVATE kurlyk)
target_link_libraries(proxy_checker_test PRIVATE kurlyk)

include(fallbacks/load_simple_web_server)
load_simple_web_server(http_local_test)
load_simple_web_server(http_retry_streaming_test)
load_simple_web_server(http_client_destructor_test)
load_simple_web_server(proxy_checker_test)

target_compile_features(integr_test PRIVATE cxx_std_17)
target_compile_features(ws_local_test PRIVATE cxx_std_17)
target_compile_features(backpressure_test PRIVATE cxx_std_17)
target_compile_features(rate_limit_lifetime_test PRIVATE cxx_std_17)
target_compile_features(request_group_cancel_test PRIVATE cxx_std_17)
target_compile_features(http_client_rate_limit_api_test PRIVATE cxx_std_17)
target_compile_features(http_client_wait_requests_test PRIVATE cxx_std_17)
target_compile_features(http_client_auth_provider_test PRIVATE cxx_std_17)
target_compile_features(sequential_rate_limit_test PRIVATE cxx_std_17)
target_compile_features(rate_limit_partition_test PRIVATE cxx_std_17)
target_compile_features(http_local_test PRIVATE cxx_std_17)
target_compile_features(http_retry_streaming_test PRIVATE cxx_std_17)
target_compile_features(http_client_destructor_test PRIVATE cxx_std_17)
target_compile_features(proxy_checker_test PRIVATE cxx_std_17)

if(MINGW)
	target_compile_options(ws_local_test PRIVATE -Wa,-mbig-obj)
	target_compile_options(http_local_test PRIVATE -Wa,-mbig-obj)
	target_compile_options(http_retry_streaming_test PRIVATE -Wa,-mbig-obj)
	target_compile_options(http_client_destructor_test PRIVATE -Wa,-mbig-obj)
	target_compile_options(proxy_checker_test PRIVATE -Wa,-mbig-obj)
endif()

include(copy_runtime_dlls)
copy_runtime_dlls(integr_test)
copy_runtime_dlls(ws_local_test)
copy_runtime_dlls(backpressure_test)
copy_runtime_dlls(rate_limit_lifetime_test)
copy_runtime_dlls(request_group_cancel_test)
copy_runtime_dlls(http_client_rate_limit_api_test)
copy_runtime_dlls(http_client_wait_requests_test)
copy_runtime_dlls(http_client_auth_provider_test)
copy_runtime_dlls(sequential_rate_limit_test)
copy_runtime_dlls(rate_limit_partition_test)
copy_runtime_dlls(http_local_test)
copy_runtime_dlls(http_retry_streaming_test)
copy_runtime_dlls(http_client_destructor_test)
copy_runtime_dlls(proxy_checker_test)

add_test(NAME integr_test COMMAND integr_test)
add_test(NAME ws_local_test COMMAND ws_local_test)
add_test(NAME backpressure_test COMMAND backpressure_test)
add_test(NAME rate_limit_lifetime_test COMMAND rate_limit_lifetime_test)
add_test(NAME request_group_cancel_test COMMAND request_group_cancel_test)
add_test(NAME http_client_rate_limit_api_test COMMAND http_client_rate_limit_api_test)
add_test(NAME http_client_wait_requests_test COMMAND http_client_wait_requests_test)
add_test(NAME http_client_auth_provider_test COMMAND http_client_auth_provider_test)
add_test(NAME sequential_rate_limit_test COMMAND sequential_rate_limit_test)
add_test(NAME rate_limit_partition_test COMMAND rate_limit_partition_test)
add_test(NAME http_local_test COMMAND http_local_test)
add_test(NAME http_retry_streaming_test COMMAND http_retry_streaming_test)
add_test(NAME http_client_destructor_test COMMAND http_client_destructor_test)
add_test(NAME proxy_checker_test COMMAND proxy_checker_test)

function(kurlyk_mark_simple_ws_client_layout target)
	get_target_property(_aliased_target ${target} ALIASED_TARGET)
	if(_aliased_target)
		set(_layout_target "${_aliased_target}")
	else()
		set(_layout_target "${target}")
	endif()

	get_target_property(_include_dirs ${_layout_target} INTERFACE_INCLUDE_DIRECTORIES)
	if(NOT _include_dirs OR _include_dirs STREQUAL "_include_dirs-NOTFOUND")
		return()
	endif()

	foreach(_include_dir IN LISTS _include_dirs)
		if(EXISTS "${_include_dir}/simple-websocket-server/client_ws.hpp")
			set_property(TARGET ${_layout_target} APPEND PROPERTY
				INTERFACE_COMPILE_DEFINITIONS KURLYK_SIMPLE_WS_CLIENT_USE_NAMESPACED
			)
			return()
		endif()
	endforeach()
endfunction()

function(kurlyk_validate_simple_ws_backend target)
	get_target_property(_compile_definitions ${target} INTERFACE_COMPILE_DEFINITIONS)
	get_target_property(_link_libraries ${target} INTERFACE_LINK_LIBRARIES)
	if(_compile_definitions STREQUAL "_compile_definitions-NOTFOUND")
		set(_compile_definitions "")
	endif()
	if(_link_libraries STREQUAL "_link_libraries-NOTFOUND")
		set(_link_libraries "")
	endif()

	set(_sws_uses_standalone OFF)
	set(_sws_uses_boost OFF)
	foreach(_definition IN LISTS _compile_definitions)
		if(_definition MATCHES "ASIO_STANDALONE")
			set(_sws_uses_standalone ON)
		endif()
	endforeach()
	foreach(_library IN LISTS _link_libraries)
		if(_library MATCHES "(^|::)Boost::")
			set(_sws_uses_boost ON)
		endif()
	endforeach()

	if(KURLYK_USE_STANDALONE_ASIO AND _sws_uses_boost)
		message(FATAL_ERROR
			"Simple-WebSocket-Server backend mismatch: Kurlyk selects standalone "
			"Asio, but the selected SWS target links Boost.Asio. Rebuild SWS "
			"with USE_STANDALONE_ASIO=ON or configure Kurlyk with "
			"KURLYK_USE_STANDALONE_ASIO=OFF."
		)
	endif()
	if(NOT KURLYK_USE_STANDALONE_ASIO AND _sws_uses_standalone)
		message(FATAL_ERROR
			"Simple-WebSocket-Server backend mismatch: Kurlyk selects Boost.Asio, "
			"but the selected SWS target defines ASIO_STANDALONE. Rebuild SWS "
			"with USE_STANDALONE_ASIO=OFF or configure Kurlyk with "
			"KURLYK_USE_STANDALONE_ASIO=ON."
		)
	endif()
endfunction()

function(use_or_fetch_simple_ws_server out_target)
	
	if(TARGET simple_ws_server)
		message(AUTHOR_WARNING "Simple-Websocket-Server: using existing target simple_ws_server")
		if(NOT DEFINED USE_STANDALONE_ASIO)
			set(USE_STANDALONE_ASIO ${KURLYK_USE_STANDALONE_ASIO}
			  CACHE BOOL "Synchronization for Simple-WebSocket-Server" FORCE)
		endif()
		kurlyk_validate_simple_ws_backend(simple_ws_server)
		kurlyk_mark_simple_ws_client_layout(simple_ws_server)
		target_link_libraries(${out_target} INTERFACE simple_ws_server)
		return()
	endif()

	# Prefer the checked-out downstream baseline when building from the source
	# tree. Installed consumers have no bundled checkout and use the package
	# discovery path below instead.
	set(_bundled_simple_ws_server_include_dir
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../external/Simple-WebSocket-Server"
	)
	if(KURLYK_USE_BUNDLED_SIMPLE_WS_SERVER AND
		EXISTS "${_bundled_simple_ws_server_include_dir}/client_ws.hpp")
		message(STATUS "Simple-Websocket-Server: using bundled downstream baseline")
		add_library(simple_ws_server INTERFACE)
		target_include_directories(simple_ws_server INTERFACE
			"${_bundled_simple_ws_server_include_dir}"
		)
		target_link_libraries(${out_target} INTERFACE simple_ws_server)
		return()
	endif()

	# A maintained SWS installation exports a CMake package and target. Prefer
	# that target over raw include-path discovery so its backend and dependency
	# contract remain intact.
	find_package(SimpleWebSocketServer CONFIG QUIET)
	if(TARGET SimpleWebSocketServer::simple-websocket-server)
		message(STATUS "Simple-Websocket-Server: using installed CMake package")
		kurlyk_validate_simple_ws_backend(
			SimpleWebSocketServer::simple-websocket-server
		)
		add_library(simple_ws_server ALIAS
			SimpleWebSocketServer::simple-websocket-server
		)
		kurlyk_mark_simple_ws_client_layout(
			SimpleWebSocketServer::simple-websocket-server
		)
		target_link_libraries(${out_target} INTERFACE simple_ws_server)
		return()
	endif()

	# Header-only SWS ports may install the upstream headers without exporting a
	# CMake target. Discover both the namespaced installed layout and the flat
	# source-tree layout before falling back to FetchContent.
	find_path(KURLYK_SIMPLE_WS_NAMESPACED_INCLUDE_DIR
		NAMES simple-websocket-server/client_ws.hpp
	)
	if(KURLYK_SIMPLE_WS_NAMESPACED_INCLUDE_DIR)
		set(KURLYK_SIMPLE_WS_INCLUDE_DIR
			"${KURLYK_SIMPLE_WS_NAMESPACED_INCLUDE_DIR}"
		)
		set(KURLYK_SIMPLE_WS_CLIENT_USE_NAMESPACED ON)
	else()
		find_path(KURLYK_SIMPLE_WS_INCLUDE_DIR
			NAMES client_ws.hpp
		)
	endif()
	if(KURLYK_SIMPLE_WS_INCLUDE_DIR)
		message(STATUS "Simple-Websocket-Server: using discovered headers at ${KURLYK_SIMPLE_WS_INCLUDE_DIR}")
		add_library(simple_ws_server INTERFACE)
		target_include_directories(simple_ws_server INTERFACE
			"${KURLYK_SIMPLE_WS_INCLUDE_DIR}"
		)
		if(KURLYK_SIMPLE_WS_CLIENT_USE_NAMESPACED)
			target_compile_definitions(simple_ws_server INTERFACE
				KURLYK_SIMPLE_WS_CLIENT_USE_NAMESPACED
			)
		endif()
		target_link_libraries(${out_target} INTERFACE simple_ws_server)
		return()
	endif()
	
	if(KURLYK_USE_FALLBACK_SIMPLE_WS_SERVER)
		include(cmake/deps/fallbacks/load_simple_ws_server.cmake)
		load_simple_ws_server(${out_target})
		return()
	endif()
	
	message(FATAL_ERROR "Target simple_ws_server not found and fallback option is disabled")
	
endfunction()

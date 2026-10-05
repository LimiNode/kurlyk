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

function(use_or_fetch_simple_ws_server out_target)
	
	if(TARGET simple_ws_server)
		message(AUTHOR_WARNING "Simple-Websocket-Server: using existing target simple_ws_server")
		if(NOT DEFINED USE_STANDALONE_ASIO)
			set(USE_STANDALONE_ASIO ${KURLYK_USE_STANDALONE_ASIO}
			  CACHE BOOL "Synchronization for Simple-WebSocket-Server" FORCE)
		endif()
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
	if(EXISTS "${_bundled_simple_ws_server_include_dir}/client_ws.hpp")
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

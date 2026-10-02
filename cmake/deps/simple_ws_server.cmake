function(use_or_fetch_simple_ws_server out_target)
	
	if(TARGET simple_ws_server)
		message(AUTHOR_WARNING "Simple-Websocket-Server: using existing target simple_ws_server")
		if(NOT DEFINED USE_STANDALONE_ASIO)
			set(USE_STANDALONE_ASIO ${KURLYK_USE_STANDALONE_ASIO}
			  CACHE BOOL "Synchronization for Simple-WebSocket-Server" FORCE)
		endif()
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

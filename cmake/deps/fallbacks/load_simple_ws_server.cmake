function(load_simple_ws_server target)
	set(_simple_ws_server_include_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../external/Simple-WebSocket-Server")
	if(EXISTS "${_simple_ws_server_include_dir}/client_ws.hpp")
		message(STATUS "Simple-Websocket-Server: using local submodule")
		if(NOT TARGET simple_ws_server)
			add_library(simple_ws_server INTERFACE)
			target_include_directories(simple_ws_server INTERFACE "${_simple_ws_server_include_dir}")
		endif()
		target_link_libraries(${target} INTERFACE simple_ws_server)
		return()
	endif()

	if(NOT DEFINED USE_STANDALONE_ASIO)
		set(USE_STANDALONE_ASIO ${KURLYK_USE_STANDALONE_ASIO}
		  CACHE BOOL "Synchronization for Simple-WebSocket-Server" FORCE)
	endif()
	include(FetchContent)
	FetchContent_Declare(simple_ws_server
		GIT_REPOSITORY https://gitlab.com/eidheim/Simple-WebSocket-Server.git
		GIT_TAG 7bb2867b9d50ff559c60b99178fd46531daa2c7e
	)
	FetchContent_GetProperties(simple_ws_server)
	if (NOT simple_ws_server_POPULATED)
		FetchContent_Populate(simple_ws_server)
	endif()
	message(STATUS "Simple-Websocket-Server: using fallback from remote repository")
	if (NOT TARGET simple_ws_server)
		add_library(simple_ws_server INTERFACE)
		target_include_directories(simple_ws_server INTERFACE "${simple_ws_server_SOURCE_DIR}")
	endif()
	target_link_libraries(${target} INTERFACE simple_ws_server)
	
	
endfunction()

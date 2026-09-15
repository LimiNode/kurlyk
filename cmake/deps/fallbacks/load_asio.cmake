function(load_asio target)
    # Prefer the checked-out submodule for deterministic/offline builds.
    set(_asio_include_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../external/asio/include")
    if(EXISTS "${_asio_include_dir}/asio.hpp")
        message(STATUS "Asio: using local submodule")
        if(NOT TARGET asio)
            add_library(asio INTERFACE)
            target_include_directories(asio INTERFACE "${_asio_include_dir}")
            target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
            add_library(asio::asio ALIAS asio)
        endif()
        target_link_libraries(${target} INTERFACE asio)
        return()
    endif()

    # Fallback: FetchContent

	message(STATUS "Asio: using fallback standalone Asio from remote repository")
	include(FetchContent)
	FetchContent_Declare(asio_dep
		GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
		GIT_TAG ed6aa8a13d51dfc6c00ae453fc9fb7df5d6ea963
	)
	FetchContent_GetProperties(asio_dep)
	if (NOT asio_dep_POPULATED)
		FetchContent_Populate(asio_dep)
	endif()
	
	if (NOT TARGET asio)
		add_library(asio INTERFACE)
		target_include_directories(asio INTERFACE "${asio_dep_SOURCE_DIR}/asio/include")
		target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
		add_library(asio::asio ALIAS asio)
	endif()
	target_link_libraries(${target} INTERFACE asio)

	
endfunction()

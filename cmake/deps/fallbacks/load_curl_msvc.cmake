function(load_curl target)
#MSVC
	include(FetchContent)
	FetchContent_Declare(curl_dep
		GIT_REPOSITORY https://github.com/LimiNode/curl-8.22.0_1-win64-mingw.git
		GIT_TAG 5a639c3fbe1c322a9048f4d54f66ba48f48a2d7d
	)
	FetchContent_GetProperties(curl_dep)
	if (NOT curl_dep_POPULATED)
		FetchContent_Populate(curl_dep)
	endif()
	
	
	if(KURLYK_CURL_SHARED)
		#Fallback: FetchContent
		# load for shared msvc version
		# temporarily the same repo as for mingw
		message(STATUS "CURL: using fallback curl (MSVC SHARED) from remote repository")
		if(NOT TARGET CURL::libcurl)
			add_library(CURL::libcurl SHARED IMPORTED GLOBAL)
			set_target_properties(CURL::libcurl PROPERTIES
			  IMPORTED_IMPLIB "${curl_dep_SOURCE_DIR}/lib/libcurl.dll.a"
			  IMPORTED_LOCATION "${curl_dep_SOURCE_DIR}/bin/libcurl-x64.dll"
			  INTERFACE_INCLUDE_DIRECTORIES "${curl_dep_SOURCE_DIR}/include"
			)
		endif()
	else()
		# load for static msvc version
		message(FATAL_ERROR "CURL fallback temporarily supports only shared library for MSVC.")
	endif()
	
	target_link_libraries(${target} INTERFACE CURL::libcurl)
	
endfunction()

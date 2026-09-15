# for now fallback binaries are only for Windows
function(use_or_fetch_curl out_target)
	
	
	# if targets were added earlier
	if(TARGET CURL::libcurl)
		message(AUTHOR_WARNING "Target CURL::libcurl was found")
		target_link_libraries(${out_target} INTERFACE CURL::libcurl)
		return()
	endif()
	
	
	# if consumer has package
	if(WIN32)
		find_package(CURL 8.11.0 QUIET)
	else()
		find_package(CURL QUIET)
	endif()
	if (CURL_FOUND AND TARGET CURL::libcurl)
		message(STATUS "CURL: using existing CURL::libcurl")
		target_link_libraries(${out_target} INTERFACE CURL::libcurl)
		return()
	endif()
	
	
	# if consumer use fallback-option
	if(KURLYK_USE_FALLBACK_CURL)
		if (WIN32)
			if(MSVC)
				include(cmake/deps/fallbacks/load_curl_msvc.cmake)
			elseif(MINGW)
				include(cmake/deps/fallbacks/load_curl_mingw.cmake)
			else()
				message(FATAL_ERROR "Unknown compiler or there is no fallback for this compiler.")
			endif()
		elseif(APPLE)
			message(FATAL_ERROR "CURL fallback is only available on Windows; use the system or Homebrew curl on macOS.")
		else()
			message(FATAL_ERROR "CURL fallback option for Linux is not supported yet.")
		endif()
		load_curl(${out_target})
		return()
	endif()
	
	message(FATAL_ERROR "CURL not found and fallback disabled!!!")
	
endfunction()

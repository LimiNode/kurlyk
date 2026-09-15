function(use_or_fetch_openssl out_target)
	
	# if targets were added earlier
	if (TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
		message(AUTHOR_WARNING "Targets OpenSSL::SSL and OpenSSL::Crypto were found")
		target_link_libraries(${out_target} INTERFACE 
			OpenSSL::SSL 
			OpenSSL::Crypto
		)
		target_compile_definitions(${out_target} INTERFACE HAVE_OPENSSL)
		return()
	endif()
	
	
	# if consumer has package
	if(WIN32)
		set(OPENSSL_USE_STATIC_LIBS OFF CACHE BOOL "" FORCE)
		find_package(OpenSSL 3.4.0 QUIET)
	else()
		find_package(OpenSSL QUIET)
	endif()
	if (OpenSSL_FOUND AND TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
		message(STATUS "OpenSSL: using existing package OpenSSL::SSL / OpenSSL::Crypto")
		target_link_libraries(${out_target} INTERFACE 
			OpenSSL::SSL 
			OpenSSL::Crypto
		)
		target_compile_definitions(${out_target} INTERFACE HAVE_OPENSSL)
		return()
	endif()
	
	
	# if consumer use fallback-option
	if (KURLYK_USE_FALLBACK_OPENSSL)
		if (WIN32)
			if(MSVC)
				include(cmake/deps/fallbacks/load_openssl_msvc.cmake)
			elseif(MINGW)
				include(cmake/deps/fallbacks/load_openssl_mingw.cmake)
			else()
				message(FATAL_ERROR "Unknown compiler or there is no fallback for this compiler.")
			endif()
		elseif(APPLE)
			message(FATAL_ERROR "OpenSSL fallback is only available on Windows; use the system or Homebrew OpenSSL on macOS.")
		else()
			message(FATAL_ERROR "OpenSSL fallback option for Linux is not supported yet")
		endif()
		load_openssl(${out_target})
		return()
	endif()
	
	message(FATAL_ERROR "OpenSSL not found and fallback disabled!!!")
	
endfunction()

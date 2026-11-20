function(load_openssl target)
	if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
		target_link_libraries(${target} INTERFACE OpenSSL::SSL OpenSSL::Crypto)
		return()
	endif()
	if(DEFINED KURLYK_ALLOW_DOWNLOADS AND NOT KURLYK_ALLOW_DOWNLOADS)
		message(FATAL_ERROR "OpenSSL fallback requires downloads but KURLYK_ALLOW_DOWNLOADS=OFF. Provide OpenSSL::SSL and OpenSSL::Crypto.")
	endif()
#MINGW
	if(KURLYK_OPENSSL_SHARED)
		# load for shared mingw version
		#Fallback: FetchContent
		message(STATUS "OpenSSL: using fallback (MinGW SHARED) from remote repository")
		include(FetchContent)
		FetchContent_Declare(openssl_dep
			GIT_REPOSITORY https://github.com/LimiNode/openssl-win64-v3.5.8.git
			GIT_TAG d7528b3ade4da0ab445c8bdb34b65907595d27f5
		)
		FetchContent_GetProperties(openssl_dep)
		if (NOT openssl_dep_POPULATED)
			FetchContent_Populate(openssl_dep)
		endif()
		
		if(NOT TARGET OpenSSL::SSL)
			add_library(OpenSSL::SSL SHARED IMPORTED GLOBAL)
			set_target_properties(OpenSSL::SSL PROPERTIES
			  IMPORTED_IMPLIB "${openssl_dep_SOURCE_DIR}/lib/VC/x64/MD/libssl.lib"
			  IMPORTED_LOCATION "${openssl_dep_SOURCE_DIR}/bin/libssl-3-x64.dll"
			  INTERFACE_INCLUDE_DIRECTORIES "${openssl_dep_SOURCE_DIR}/include"
			)
		endif()
		
		if(NOT TARGET OpenSSL::Crypto)
			add_library(OpenSSL::Crypto SHARED IMPORTED GLOBAL)
			set_target_properties(OpenSSL::Crypto PROPERTIES
			  IMPORTED_IMPLIB "${openssl_dep_SOURCE_DIR}/lib/VC/x64/MD/libcrypto.lib"
			  IMPORTED_LOCATION "${openssl_dep_SOURCE_DIR}/bin/libcrypto-3-x64.dll"
			  INTERFACE_INCLUDE_DIRECTORIES "${openssl_dep_SOURCE_DIR}/include"
			)
		endif()
		
		target_link_libraries(${target} INTERFACE 
			OpenSSL::SSL 
			OpenSSL::Crypto
		)
		target_compile_definitions(${target} INTERFACE HAVE_OPENSSL)
	else()
		message(FATAL_ERROR "OpenSSL fallback support only shared library for MinGW.")
	endif()
	
endfunction()

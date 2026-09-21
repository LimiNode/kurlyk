function(load_openssl target)
#MSVC
	
	include(FetchContent)
	FetchContent_Declare(openssl_dep
		GIT_REPOSITORY https://github.com/LimiNode/openssl-win64-v3.5.8.git
		GIT_TAG 19d9b5267e9ab108122feb3cac258ecb6d21cfd3
	)
	FetchContent_GetProperties(openssl_dep)
	if (NOT openssl_dep_POPULATED)
		FetchContent_Populate(openssl_dep)
	endif()
	
	if(KURLYK_OPENSSL_SHARED)
		# load for shared msvc version
		# temporarily the same repo as for mingw
		message(STATUS "OpenSSL: using fallback (MSVC SHARED) from remote repository")
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
		
	else()
		# load for static msvc version
		# temporarily the same repo as for mingw
		message(STATUS "OpenSSL: using fallback (MSVC STATIC) from remote repository")
		if(NOT TARGET OpenSSL::SSL)
			add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
			set_target_properties(OpenSSL::SSL PROPERTIES
			  IMPORTED_LOCATION "${openssl_dep_SOURCE_DIR}/lib/VC/x64/MD/libssl_static.lib"
			  INTERFACE_INCLUDE_DIRECTORIES "${openssl_dep_SOURCE_DIR}/include"
			)
		endif()
		
		if(NOT TARGET OpenSSL::Crypto)
			add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
			set_target_properties(OpenSSL::Crypto PROPERTIES
			  IMPORTED_LOCATION "${openssl_dep_SOURCE_DIR}/lib/VC/x64/MD/libcrypto_static.lib"
			  INTERFACE_INCLUDE_DIRECTORIES "${openssl_dep_SOURCE_DIR}/include"
			)
		endif()
		
	endif()
	
	target_link_libraries(${target} INTERFACE 
			OpenSSL::SSL 
			OpenSSL::Crypto
		)
	target_compile_definitions(${target} INTERFACE HAVE_OPENSSL)
	
endfunction()

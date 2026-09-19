function(use_or_fetch_asio out_target)

	
	if (NOT KURLYK_USE_STANDALONE_ASIO)
# Boost library
# if target Boost already exist
		if (TARGET Boost::boost)
			message(AUTHOR_WARNING "Targets Boost::boost was found")
			target_link_libraries(${out_target} INTERFACE Boost::boost)
			return()
		endif()
		
# if consumer has package
		find_package(Boost 1.88 QUIET)
		if (Boost_FOUND AND TARGET Boost::boost)
			message(STATUS "Asio: using Boost::asio (KURLYK_USE_STANDALONE_ASIO=OFF)")
			target_link_libraries(${out_target} INTERFACE Boost::boost)
			return()
		endif()
		message(FATAL_ERROR "Macros KURLYK_USE_STANDALONE_ASIO is OFF, but Boost.Asio not found!!!")
		
		
	else()
# Asio standalone
# if target asio::asio already exist
		if (TARGET asio::asio)
			message(AUTHOR_WARNING "Target asio::asio was found")
			
			get_target_property(_aliased asio::asio ALIASED_TARGET)
			if(_aliased)
				set(_asio_tgt ${_aliased})
			else()
				set(_asio_tgt asio::asio)
			endif()
			get_target_property(asio_inc ${_asio_tgt} INTERFACE_INCLUDE_DIRECTORIES)
			if(NOT asio_inc)
				message(WARNING "asio::asio has no INTERFACE_INCLUDE_DIRECTORIES")
			endif()
			target_link_libraries(${out_target} INTERFACE asio::asio)
			target_compile_definitions(${out_target} INTERFACE ASIO_STANDALONE)
			return()
		endif()
	
# if consumer has package
		find_package(asio QUIET)
		if (asio_FOUND AND TARGET asio::asio)
			message(STATUS "Asio: using standalone asio::asio (KURLYK_USE_STANDALONE_ASIO=ON)")
			target_link_libraries(${out_target} INTERFACE asio::asio)
			target_compile_definitions(${out_target} INTERFACE ASIO_STANDALONE)
			return()
		endif()

# if consumer set fallback option on
		if(KURLYK_USE_FALLBACK_ASIO)
			include(cmake/deps/fallbacks/load_asio.cmake)
			load_asio(${out_target})
			return()
		endif()
		
		message(FATAL_ERROR "Standalone Asio not found and fallback disabled!!!")
		
	endif()
	
endfunction()

if(NOT DEFINED KURLYK_STAGE_DIR)
    message(FATAL_ERROR "KURLYK_STAGE_DIR must point to the install staging directory")
endif()

foreach(required_path IN ITEMS
    "${KURLYK_STAGE_DIR}/include/kurlyk.hpp"
    "${KURLYK_STAGE_DIR}/include/kurlyk/types/ProxyConfig.hpp"
    "${KURLYK_STAGE_DIR}/share/licenses/kurlyk/LICENSE"
    "${KURLYK_STAGE_DIR}/share/licenses/kurlyk/Simple-WebSocket-Server-LICENSE"
    "${KURLYK_STAGE_DIR}/lib/cmake/kurlyk/kurlykConfig.cmake"
    "${KURLYK_STAGE_DIR}/lib/cmake/kurlyk/kurlykConfigVersion.cmake"
    "${KURLYK_STAGE_DIR}/lib/cmake/kurlyk/kurlykTargets.cmake"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Required installed path is missing: ${required_path}")
    endif()
endforeach()

foreach(forbidden_path IN ITEMS
    "${KURLYK_STAGE_DIR}/include/.codebase-memory"
    "${KURLYK_STAGE_DIR}/include/AGENTS.md"
    "${KURLYK_STAGE_DIR}/tests"
    "${KURLYK_STAGE_DIR}/examples"
    "${KURLYK_STAGE_DIR}/docs"
)
    if(EXISTS "${forbidden_path}")
        message(FATAL_ERROR "Internal project content was installed: ${forbidden_path}")
    endif()
endforeach()

#pragma once
#ifndef KURLYK_HEADER_KURLYK_STARTUP_AUTO_INITIALIZER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_STARTUP_AUTO_INITIALIZER_HPP_INCLUDED

/// \file AutoInitializer.hpp
/// \brief Provides automatic initialization and shutdown for the Kurlyk network system.

namespace kurlyk {
namespace startup {

    /// \class AutoInitializer
    /// \brief Automatically registers and manages network task managers.
    ///
    /// This class ensures that managers like HttpRequestManager and WebSocketManager are
    /// registered to the NetworkWorker during construction, and properly shut down in
    /// reverse order during destruction.
    class AutoInitializer {
    public:
        /// \brief Constructs and registers all available managers.
        AutoInitializer() {
            auto &instance = core::NetworkWorker::get_instance();
#           if KURLYK_HTTP_SUPPORT
            m_http = &HttpRequestManager::get_instance();
            instance.register_manager(m_http);
#           endif
#           if KURLYK_WEBSOCKET_SUPPORT
            m_ws = &WebSocketManager::get_instance();
            instance.register_manager(m_ws);
#           endif
            instance.start(KURLYK_AUTO_INIT_USE_ASYNC);
        }

        /// \brief Stops the NetworkWorker before program termination.
        ~AutoInitializer() {
            core::NetworkWorker::get_instance().stop();
        }

    private:
#       if KURLYK_HTTP_SUPPORT
        HttpRequestManager* m_http = nullptr;
#       endif
#       if KURLYK_WEBSOCKET_SUPPORT
        WebSocketManager*   m_ws = nullptr;
#       endif
    };
	
    /// \brief Returns the process-wide automatic initializer.
    inline AutoInitializer& get_auto_initializer() {
        static AutoInitializer instance;
        return instance;
    }

    /// Convenience auto-init helper. Initialization order relative to other statics
    /// is undefined; ensure_initialized() in client constructors is the reliable fallback.
    static AutoInitializer& _kurlyk_auto_initializer = get_auto_initializer();

} // namespace startup
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_STARTUP_AUTO_INITIALIZER_HPP_INCLUDED

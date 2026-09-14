#pragma once
#ifndef KURLYK_HEADER_KURLYK_WEBSOCKET_WEB_SOCKET_MANAGER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_WEBSOCKET_WEB_SOCKET_MANAGER_HPP_INCLUDED

/// \file WebSocketManager.hpp
/// \brief Defines the WebSocketManager singleton responsible for backend-specific WebSocket client instances.

#include "client/BaseWebSocketClient.hpp"

#ifdef KURLYK_USE_EMSCRIPTEN
#include "client/Emscripten/EmscriptenWebSocketClientAdapter.hpp"
#endif

#ifdef KURLYK_USE_SIMPLEWEB
#include "client/SimpleWeb.hpp"
#endif

namespace kurlyk {

#ifdef KURLYK_USE_EMSCRIPTEN
    /// \brief Backend client type selected for Emscripten builds.
    using selected_backend_client_t = EmscriptenWebSocketClientAdapter;
#elif defined(KURLYK_USE_SIMPLEWEB)
    /// \brief Backend client type selected for native builds using Simple-WebSocket-Server.
    using selected_backend_client_t = SimpleWebSocketClientAdapter;
#endif

    /// \brief Shared pointer to the compile-time selected backend client type.
    using selected_backend_client_ptr = std::shared_ptr<selected_backend_client_t>;
    /// \brief Weak pointer to the compile-time selected backend client type.
    using selected_backend_client_weak_ptr = std::weak_ptr<selected_backend_client_t>;

    /// \class WebSocketManager
    /// \brief Manages backend-specific WebSocket client instances and coordinates their lifecycle.
    class WebSocketManager final : public core::INetworkTaskManager {
    public:

        /// \brief Get the singleton instance of WebSocketManager.
        /// \return Reference to the singleton instance of WebSocketManager.
        static WebSocketManager& get_instance() {
            static WebSocketManager* instance = new WebSocketManager();
            return *instance;
        }

        /// \brief Processes all active backend-specific WebSocket clients managed by this instance.
        ///
        /// Iterates over each WebSocket client and calls its `process` method to handle pending operations.
        /// Automatically removes clients that are no longer active from the internal client list.
        void process() override {
            std::lock_guard<std::mutex> lock(m_client_list_mutex);
            for (auto &&client_weak_ptr : m_client_list) {
                if (auto client_ptr = client_weak_ptr.lock()) {
                    client_ptr->process();
                }
            }
            // Remove expired clients from the list
            m_client_list.remove_if([](const selected_backend_client_weak_ptr& client_weak_ptr) {
                return client_weak_ptr.expired();
            });
        }

        /// \brief Shuts down all active backend-specific WebSocket clients managed by this instance.
        ///
        /// Iterates over each WebSocket client and calls its `shutdown` method to stop and clear its state.
        void shutdown() override {
            std::lock_guard<std::mutex> lock(m_client_list_mutex);
            for (auto &&client_weak_ptr : m_client_list) {
                if (auto client_ptr = client_weak_ptr.lock()) {
                    client_ptr->shutdown();
                }
            }
        }

        /// \brief Checks if any managed backend-specific WebSocket client is currently running.
        /// \return True if at least one WebSocket client is in a running state, otherwise false.
        bool is_loaded() const override {
            std::lock_guard<std::mutex> lock(m_client_list_mutex);
            for (auto &&client_weak_ptr : m_client_list) {
                if (auto client_ptr = client_weak_ptr.lock()) {
                    if (client_ptr->is_running()) return true;
                }
            }
            return false;
        }

        /// \brief Creates and returns a new backend-specific WebSocket client selected by compilation flags.
        /// \return A shared pointer to the created backend-specific WebSocket client instance.
        selected_backend_client_ptr create_client() {
            selected_backend_client_ptr client;

#           ifdef KURLYK_USE_EMSCRIPTEN
            // Client for the Emscripten platform
#           if __cplusplus >= 201402L
            client = std::make_shared<selected_backend_client_t>();
#           else
            client = selected_backend_client_ptr(new selected_backend_client_t());
#           endif
#           endif

#           ifdef KURLYK_USE_SIMPLEWEB
            // Client for other platforms
#           if __cplusplus >= 201402L
            client = std::make_shared<selected_backend_client_t>();
#           else
            client = selected_backend_client_ptr(new selected_backend_client_t());
#           endif
#           endif

            std::lock_guard<std::mutex> lock(m_client_list_mutex);
            m_client_list.push_back(client);
            return client;
        }

    private:
        mutable std::mutex                          m_client_list_mutex; ///< Mutex for synchronizing access to the client list.
        std::list<selected_backend_client_weak_ptr> m_client_list;       ///< List of managed backend-specific WebSocket clients.

        /// \brief Private constructor to enforce singleton pattern.
        WebSocketManager() = default;

        /// \brief Private destructor to enforce singleton pattern.
        virtual ~WebSocketManager() = default;

        /// \brief Deleted copy constructor to enforce the singleton pattern.
        WebSocketManager(const WebSocketManager&) = delete;

        /// \brief Deleted copy assignment operator to enforce the singleton pattern.
        WebSocketManager& operator=(const WebSocketManager&) = delete;

    }; // WebSocketManager

}

#endif // KURLYK_HEADER_KURLYK_WEBSOCKET_WEB_SOCKET_MANAGER_HPP_INCLUDED

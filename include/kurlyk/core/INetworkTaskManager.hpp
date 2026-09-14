#pragma once
#ifndef KURLYK_HEADER_KURLYK_CORE_I_NETWORK_TASK_MANAGER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_CORE_I_NETWORK_TASK_MANAGER_HPP_INCLUDED

/// \file INetworkTaskManager.hpp
/// \brief Defines an interface for modules that can register with the NetworkWorker for lifecycle handling.

namespace kurlyk {
namespace core {

    /// \class INetworkTaskManager
    /// \brief Interface for modules managed by NetworkWorker (e.g., HTTP, WebSocket).
    class INetworkTaskManager {
    public:
        /// \brief Called periodically to process tasks.
        virtual void process() = 0;

        /// \brief Called during shutdown to clean up.
        virtual void shutdown() = 0;

        /// \brief Indicates whether the module has pending or active work.
        virtual bool is_loaded() const = 0;

        virtual ~INetworkTaskManager() = default;
    };

} // namespace core
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_CORE_I_NETWORK_TASK_MANAGER_HPP_INCLUDED

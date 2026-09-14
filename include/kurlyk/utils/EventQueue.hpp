#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_EVENT_QUEUE_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_EVENT_QUEUE_HPP_INCLUDED

/// \file EventQueue.hpp
/// \brief Defines the EventQueue class for managing a thread-safe event queue.

#include <queue>
#include <mutex>
#include <condition_variable>

namespace kurlyk {
namespace utils {

    /// \class EventQueue
    /// \brief A thread-safe event queue that supports blocking and non-blocking event retrieval.
    template<class T>
    class EventQueue {
    public:
        /// \brief Adds an event to the queue using move semantics.
        /// \param event The event to add to the queue.
        void push_event(T&& event) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_events.emplace(std::move(event));
            m_cond_var.notify_one();
        }

        /// \brief Adds a copy of an event to the queue and notifies any waiting threads.
        /// \param event The event to add to the queue.
        void push_event(const T& event) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_events.push(event);
            m_cond_var.notify_one();
        }

        /// \brief Removes and returns an event from the queue (blocks if the queue is empty).
        /// \return The retrieved event.
        T pop_event() {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond_var.wait(lock, [this] { return !m_events.empty(); });
            T event = std::move(m_events.front());
            m_events.pop();
            return event;
        }

        /// \brief Checks if there are events in the queue.
        /// \return `true` if the queue is not empty, otherwise `false`.
        bool has_events() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return !m_events.empty();
        }

    private:
        std::queue<T> m_events;             ///< Queue to store events.
        mutable std::mutex m_mutex;         ///< Mutex to protect queue access.
        std::condition_variable m_cond_var; ///< Condition variable for blocking until events are available.
    };

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_EVENT_QUEUE_HPP_INCLUDED

#pragma once
#ifndef KURLYK_HEADER_KURLYK_CORE_NETWORK_WORKER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_CORE_NETWORK_WORKER_HPP_INCLUDED

/// \file NetworkWorker.hpp
/// \brief Manages and processes network tasks such as HTTP requests and WebSocket events in a separate thread.

#define KURLYK_HANDLE_ERROR(e, msg) \
    ::kurlyk::core::NetworkWorker::get_instance().handle_error((e), (msg), __FILE__, __LINE__, __FUNCTION__)

#include <thread>

namespace kurlyk {
namespace core {

    /// \class NetworkWorker
    /// \brief Singleton worker that manages asynchronous network operations, including HTTP requests and WebSocket events.
    ///
    /// The `NetworkWorker` is responsible for managing and processing network-related tasks in a separate thread.
    /// It supports adding tasks to a queue, notifying and starting the worker thread, and handling graceful shutdown.
    class NetworkWorker {
    public:
        using ErrorHandler = std::function<void(const std::exception&, const char*, const char*, int, const char*)>;

        /// \brief Get the singleton instance of NetworkWorker.
        /// \return Reference to the singleton instance.
        static NetworkWorker& get_instance() {
            static NetworkWorker* instance = new NetworkWorker();
            return *instance;
        }

        /// \brief Registers a callback for handling network errors.
        /// \param handler Function invoked when an error is dispatched.
        void add_error_handler(ErrorHandler handler) {
            std::lock_guard<std::mutex> lock(m_error_handlers_mutex);
            m_error_handlers.push_back(std::move(handler));
        }

        /// \brief Dispatches an exception to all registered error handlers.
        /// \param e   The thrown exception.
        /// \param msg Additional context message.
        /// \param file Source file where the error occurred.
        /// \param line Line number where the error occurred.
        /// \param func Function name where the error occurred.
        void handle_error(
                const std::exception& e,
                const char* msg,
                const char* file,
                int line,
                const char* func) {

            std::unique_lock<std::mutex> lock(m_error_handlers_mutex);
            if (m_error_handlers.empty()) return;
            std::vector<ErrorHandler> handlers = m_error_handlers;
            lock.unlock();

            for (const auto& handler : handlers) {
                try {
                    handler(e, msg, file, line, func);
                } catch (...) {
                    // Never let handler crash the system
                }
            }
        }

        /// \brief Handles an exception captured as exception_ptr.
        /// \param eptr The captured exception.
        /// \param msg Custom message for context.
        /// \param file File where the error occurred.
        /// \param line Line number where the error occurred.
        /// \param func Function name where the error occurred.
        void handle_error(
                std::exception_ptr eptr,
                const char* msg,
                const char* file,
                int line,
                const char* func) {

            if (!eptr) return;

            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                handle_error(e, msg, file, line, func);
            } catch (...) {
                const std::runtime_error unknown("Unknown non-std::exception");
                handle_error(unknown, msg, file, line, func);
            }
        }

        /// \brief Returns true if the calling thread is the worker thread.
        bool is_worker_thread() const {
            return std::this_thread::get_id() == m_worker_thread_id;
        }

        /// \brief Adds a task to the queue and notifies the worker thread.
        /// \param task A function or lambda with no arguments to be executed by the worker.
        void add_task(std::function<void()> task) {
            std::unique_lock<std::mutex> lock(m_tasks_list_mutex);
            m_tasks_list.push_back(std::move(task));
            lock.unlock();
            notify();
        }

        /// \brief Registers a network task manager to be managed by the NetworkWorker.
        /// \param manager Pointer to a manager implementing INetworkTaskManager. Must remain valid during its lifetime.
        void register_manager(INetworkTaskManager* manager) {
            std::lock_guard<std::mutex> lock(m_managers_mutex);
            if (std::find(m_managers.begin(), m_managers.end(), manager) == m_managers.end()) {
                m_managers.push_back(manager);
            }
        }

        /// \brief Processes all queued tasks and active HTTP and WebSocket requests.
        ///
        /// Processes pending tasks in the task list and manages network requests in both HTTP and WebSocket managers.
        void process() {
            std::unique_lock<std::mutex> lock(m_managers_mutex);
            for (auto* m : m_managers) m->process();
            lock.unlock();
            process_tasks();
        }

        /// \brief Notifies the worker to begin processing requests or tasks.
        ///
        /// Signals the condition variable to wake up the worker thread if it is waiting, allowing tasks to be processed.
        void notify() {
            std::lock_guard<std::mutex> locker(m_notify_mutex);
            m_notify = true;
            m_notify_condition.notify_one();
        }

        /// \brief Starts the worker thread for asynchronous task processing.
        ///
        /// If `use_async` is true, the worker runs in a separate thread, continually processing tasks and network events
        /// until `stop()` is called.
        /// \param use_async Indicates whether the worker should run asynchronously.
        void start(const bool use_async) {
            std::unique_lock<std::mutex> locker(m_is_worker_started_mutex);
            if (m_is_worker_started) return;
            m_is_worker_started = true;
            if (!use_async) return;
            locker.unlock();

            m_future = std::async(
                    std::launch::async,
                    [this] {
                m_worker_thread_id = std::this_thread::get_id();
                for (;;) {
                    std::unique_lock<std::mutex> locker(m_notify_mutex);
                    m_notify_condition.wait(locker, [this]() { return m_notify; });
                    m_notify = false;
                    locker.unlock();

                    if (m_shutdown) {
                        shutdown();
                        return;
                    }

                    while (is_loaded()) {
                        process();
                        if (m_shutdown) {
                            shutdown();
                            return;
                        }

                        std::unique_lock<std::mutex> locker(m_notify_mutex);
                        m_notify_condition.wait_for(locker, std::chrono::milliseconds(1), [this] {
                            return m_notify || m_shutdown;
                        });
                        m_notify = false;
                        locker.unlock();

                        if (m_shutdown) {
                            shutdown();
                            return;
                        }
                    }

                    if (m_shutdown) {
                        shutdown();
                        return;
                    }
                }
            }).share();
        }

        /// \brief Stops the worker thread, ensuring all tasks are completed.
        ///
        /// Signals the worker thread to stop and waits for it to complete all tasks before fully shutting down.
        void stop() {
            {
                std::lock_guard<std::mutex> locker(m_is_worker_started_mutex);
                if (!m_is_worker_started && !m_future.valid()) return;
            }

            const bool was_shutdown = m_shutdown.exchange(true);
            if (!m_future.valid()) {
                if (!was_shutdown) shutdown();
                return;
            }

            notify();
            try {
                m_future.wait();
                m_future.get();
            } catch(...) {
                KURLYK_HANDLE_ERROR(std::current_exception(), "Exception during NetworkWorker shutdown");
            };
        }

        /// \brief Shuts down the worker, clearing all active requests and pending tasks.
        ///
        /// Stops both HTTP and WebSocket managers and processes any remaining tasks in the queue.
        void shutdown() {
            std::unique_lock<std::mutex> lock(m_managers_mutex);
            for (auto* m : m_managers) m->shutdown();
            lock.unlock();
            process_tasks();
        }

    private:
        std::shared_future<void>    m_future;                           ///< Future for managing asynchronous worker execution.
        std::atomic<bool>           m_shutdown = ATOMIC_VAR_INIT(false);///< Flag indicating if shutdown has been requested.
        std::thread::id             m_worker_thread_id;                 ///< ID of the async worker thread, if started.
        std::mutex                  m_notify_mutex;                     ///< Mutex for managing worker notifications.
        std::condition_variable     m_notify_condition;                 ///< Condition variable for notifying the worker.
        bool                        m_notify = false;                   ///< Flag indicating whether a notification is pending. Access is always guarded by m_notify_mutex; atomic is unnecessary because the mutex provides the happens-before relation.
        std::mutex                  m_is_worker_started_mutex;          ///< Mutex to control worker thread initialization.
        bool                        m_is_worker_started = false;        ///< Flag indicating if the worker thread is started.
        mutable std::mutex          m_tasks_list_mutex;                 ///< Mutex for protecting access to the task list.
        std::list<std::function<void()>> m_tasks_list;                  ///< List of tasks queued for processing by the worker.
        mutable std::mutex          m_managers_mutex;                   ///< Mutex protecting access to registered managers.
        std::vector<INetworkTaskManager*> m_managers;                   ///< List of registered network task managers.
        std::mutex                  m_error_handlers_mutex;             ///< Mutex guarding the error handler list.
        std::vector<ErrorHandler>   m_error_handlers;                   ///< Collection of registered error handlers.


        /// \brief Private constructor to enforce singleton pattern.
        NetworkWorker() {
            utils::convert_user_agent_to_sec_ch_ua("");
            utils::is_valid_email_id("");
        }

        /// \brief Private destructor, ensuring the worker thread stops on destruction.
        ~NetworkWorker() {
            stop();
        }

        /// \brief Deleted copy constructor to enforce the singleton pattern.
        NetworkWorker(const NetworkWorker&) = delete;

        /// \brief Deleted copy assignment operator to enforce the singleton pattern.
        NetworkWorker& operator=(const NetworkWorker&) = delete;

        /// \brief Processes all tasks in the task list, then clears the list.
        ///
        /// This method moves tasks from the task list to a local list, processes each one, and then clears the local list.
        void process_tasks() {
            std::unique_lock<std::mutex> lock(m_tasks_list_mutex);
            if (m_tasks_list.empty()) return;
            auto tasks_list = std::move(m_tasks_list);
            m_tasks_list.clear();
            lock.unlock();
            for (auto &item : tasks_list) {
                item();
            }
            tasks_list.clear();
        }

        /// \brief Checks if there are any pending tasks in the task list.
        /// \return True if there are pending tasks, otherwise false.
        bool has_pending_tasks() const {
            std::lock_guard<std::mutex> lock(m_tasks_list_mutex);
            return !m_tasks_list.empty();
        }

        /// \brief Checks if the NetworkWorker has pending tasks or active network events.
        ///
        /// Checks if there are any tasks or events in the HTTP or WebSocket manager or in the task list.
        /// \return True if there are tasks or events to process; otherwise, false.
        bool is_loaded() const {
            std::unique_lock<std::mutex> lock(m_managers_mutex);
            for (auto* m : m_managers) {
                if (m->is_loaded()) return true;
            }
            lock.unlock();
            return has_pending_tasks();
        }

    }; // NetworkWorker

} // namespace core
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_CORE_NETWORK_WORKER_HPP_INCLUDED

#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HPP_INCLUDED

/// \file HttpRequestManager.hpp
/// \brief Manages and processes HTTP requests using a singleton pattern.

#include <type_traits>

#include "HttpRequestManager/HttpRequestContext.hpp"
#include "HttpRequestManager/HttpRequestHandler.hpp"
#include "HttpRequestManager/HttpRateLimitDelay.hpp"
#include "HttpRequestManager/HttpRateLimitHandle.hpp"
#include "HttpRequestManager/HttpRateLimiter.hpp"
#include "HttpRequestManager/HttpBatchRequestHandler.hpp"


namespace kurlyk {

    /// \class HttpRequestManager
    /// \brief Manages and processes HTTP requests using a singleton pattern.
    /// \note All process_*(), is_loaded(), and shutdown() must be called exclusively from the NetworkWorker thread. m_mutex guards public entry points and group waiters.
    class HttpRequestManager final : public core::INetworkTaskManager {
    public:

        /// \brief Get the singleton instance of HttpRequestManager.
        /// \return Reference to the singleton instance.
        static HttpRequestManager& get_instance() {
            static HttpRequestManager* instance = new HttpRequestManager();
            return *instance;
        }

        /// \brief Adds a new HTTP request to the manager.
        /// \param request_ptr Unique pointer to the HTTP request object containing request details.
        /// \param callback Callback function invoked when the request completes.
        /// \return True if the request was successfully added, false if admission was rejected.
        bool add_request(
                std::unique_ptr<HttpRequest> request_ptr,
                HttpResponseCallback callback) {
            return submit_request(std::move(request_ptr), std::move(callback)).accepted;
        }

        /// \brief Attempts to enqueue a new HTTP request and reports the admission result.
        /// \param request_ptr Unique pointer to the HTTP request object containing request details.
        /// \param callback Callback function invoked when the request completes.
        /// \return SubmitResult describing whether the request was accepted into the pending queue.
        SubmitResult submit_request(
                std::unique_ptr<HttpRequest> request_ptr,
                HttpResponseCallback callback) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_shutdown) {
                return SubmitResult{false, utils::make_error_code(utils::ClientError::ShuttingDown)};
            }

            const std::size_t queue_limit = m_max_pending_requests.load();
            if (queue_limit && m_pending_requests.size() >= queue_limit) {
                return SubmitResult{false, utils::make_error_code(utils::ClientError::QueueLimitExceeded)};
            }

#           if __cplusplus >= 201402L
            auto context = std::make_unique<HttpRequestContext>(std::move(request_ptr), std::move(callback));
#           else
            auto context = std::unique_ptr<HttpRequestContext>(
                new HttpRequestContext(std::move(request_ptr), std::move(callback)));
#           endif
            context->in_flight_token = m_next_in_flight_token.fetch_add(1, std::memory_order_relaxed);
            const uint64_t group_id = context->request ? context->request->group_id : 0;
            if (group_id != 0) {
                context->on_group_complete = [this, group_id]() {
                    complete_group_request(group_id);
                };
                ++m_group_request_counts[group_id];
            }
            try {
                m_pending_requests.push_back(std::move(context));
            } catch (...) {
                if (group_id != 0) {
                    auto count_it = m_group_request_counts.find(group_id);
                    if (count_it != m_group_request_counts.end()) {
                        if (count_it->second > 1) {
                            --count_it->second;
                        } else {
                            m_group_request_counts.erase(count_it);
                        }
                    }
                }
                throw;
            }
            return SubmitResult{true, std::error_code()};
        }

        /// \brief Creates a rate-limit handle with specified parameters.
        /// \param requests_per_period Maximum number of requests allowed in the specified period.
        /// \param period_ms Time period in milliseconds during which the rate limit applies.
        /// \param sequential When true, no other request sharing this limit may start until
        ///        the current request (including all its retries) has finished.
        /// \return RAII handle for the created rate limit.
        HttpRateLimitHandlePtr create_rate_limit(long requests_per_period, long period_ms, bool sequential = false) {
            return m_rate_limiter.create_limit_handle(requests_per_period, period_ms, sequential);
        }

        /// \brief Returns a handle for a registered rate limit ID.
        /// \note Returns empty if the manager-owned handle was already released.
        HttpRateLimitHandlePtr get_rate_limit(long limit_id) {
            return m_rate_limiter.get_limit(limit_id);
        }

        /// \brief Removes an existing rate limit with the specified identifier.
        /// \param limit_id The unique identifier of the rate limit to be removed.
        /// \return True if the rate limit was successfully removed, or false if the rate limit ID was not found.
        bool remove_limit(long limit_id) {
            return m_rate_limiter.remove_limit(limit_id);
        }

        /// \brief Releases manager-owned handle for the specified rate-limit handle.
        /// \note Physical limit data may remain alive while requests still hold handles.
        bool remove_limit(const HttpRateLimitHandlePtr& limit) {
            return m_rate_limiter.remove_limit(limit);
        }

        /// \brief Checks if a request is allowed by two optional rate-limit handles.
        /// \param general_limit General rate-limit handle (may be empty).
        /// \param specific_limit Specific rate-limit handle (may be empty).
        /// \param in_flight_token Token identifying the in-flight request; 0 skips sequential checks.
        /// \param general_key Partition key for the general limit; empty means default shared state.
        /// \param specific_key Partition key for the specific limit; empty means default shared state.
        /// \return True if the request is allowed, false otherwise.
        bool allow_request(
                const HttpRateLimitHandlePtr& general_limit,
                const HttpRateLimitHandlePtr& specific_limit,
                uint64_t in_flight_token,
                const std::string& general_key,
                const std::string& specific_key) {
            return m_rate_limiter.allow_request(
                general_limit, specific_limit, in_flight_token, general_key, specific_key);
        }

        /// \brief Releases in-flight tokens for sequential rate limits.
        /// \param general_limit General rate-limit handle (may be empty).
        /// \param specific_limit Specific rate-limit handle (may be empty).
        /// \param in_flight_token Token identifying the in-flight request; 0 is a no-op.
        /// \param general_key Partition key for the general limit.
        /// \param specific_key Partition key for the specific limit.
        void release_request(
                const HttpRateLimitHandlePtr& general_limit,
                const HttpRateLimitHandlePtr& specific_limit,
                uint64_t in_flight_token,
                const std::string& general_key,
                const std::string& specific_key) {
            m_rate_limiter.release_request(
                general_limit, specific_limit, in_flight_token, general_key, specific_key);
        }

        /// \brief Calculates delay until request is allowed by two handles.
        /// \tparam Duration Duration type; defaults to `std::chrono::milliseconds`.
        /// \param general_limit General rate-limit handle (may be empty).
        /// \param specific_limit Specific rate-limit handle (may be empty).
        /// \param general_key Partition key for the general limit; empty means default shared state.
        /// \param specific_key Partition key for the specific limit; empty means default shared state.
        /// \return RateLimitDelay describing the maximum delay across both dimensions.
        template<typename Duration = std::chrono::milliseconds>
        RateLimitDelay<Duration> time_until_next_allowed(
            const HttpRateLimitHandlePtr& general_limit,
            const HttpRateLimitHandlePtr& specific_limit,
            const std::string& general_key,
            const std::string& specific_key
            ) {
            return m_rate_limiter.time_until_next_allowed<Duration>(
                general_limit, specific_limit, general_key, specific_key);
        }

        /// \brief Generates a new unique request ID.
        /// \return A new unique request ID.
        uint64_t generate_request_id() {
            return m_request_id_counter++;
        }

        /// \brief Generates a new group ID.
        /// \return A new group ID.
        uint64_t generate_group_id() {
            return m_group_id_counter++;
        }

        /// \brief Sets the maximum number of pending requests accepted into the global queue.
        /// \param max_pending_requests Queue limit, or `0` to keep the queue unbounded.
        void set_max_pending_requests(std::size_t max_pending_requests) {
            m_max_pending_requests.store(max_pending_requests);
        }

        /// \brief Returns the current maximum pending request count.
        /// \return Configured queue limit, or `0` if the queue is unbounded.
        std::size_t max_pending_requests() const {
            return m_max_pending_requests.load();
        }

        /// \brief Checks whether an admitted request remains outstanding for a group.
        /// \param group_id Group ID to inspect.
        /// \return True if at least one managed request belongs to this group.
        bool has_requests_by_group_id(uint64_t group_id) const {
            return group_request_count(group_id) != 0;
        }

        /// \brief Counts admitted requests that have not delivered their final callback for a group.
        /// \param group_id Group ID to inspect.
        /// \return Number of managed requests that belong to this group.
        std::size_t group_request_count(uint64_t group_id) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return group_request_count_unlocked(group_id);
        }

        /// \brief Registers a callback invoked after all requests from a group finish.
        /// \param group_id Group ID to wait for.
        /// \param callback Callback invoked when the group becomes idle.
        /// \return Waiter ID that can be passed to cancel_wait_requests_by_group_id(), or zero when
        ///          the callback was invoked immediately.
        uint64_t wait_requests_by_group_id(uint64_t group_id, std::function<void()> callback) {
            bool invoke_now = false;
            uint64_t waiter_id = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_shutdown || group_id == 0 || group_request_count_unlocked(group_id) == 0) {
                    invoke_now = true;
                } else {
                    waiter_id = m_waiter_id_counter++;
                    m_group_waiters[group_id].push_back(
                        GroupWaiter{waiter_id, std::move(callback)});
                }
            }

            if (invoke_now && callback) {
                callback();
            }
            return waiter_id;
        }

        /// \brief Removes a previously registered group waiter.
        /// \param group_id Group ID associated with the waiter.
        /// \param waiter_id ID returned by wait_requests_by_group_id().
        /// \return True if the waiter was removed before notification.
        bool cancel_wait_requests_by_group_id(uint64_t group_id, uint64_t waiter_id) {
            if (group_id == 0 || waiter_id == 0) return false;

            std::lock_guard<std::mutex> lock(m_mutex);
            auto group_it = m_group_waiters.find(group_id);
            if (group_it == m_group_waiters.end()) return false;

            auto& waiters = group_it->second;
            for (auto waiter_it = waiters.begin(); waiter_it != waiters.end(); ++waiter_it) {
                if (waiter_it->id != waiter_id) continue;
                waiters.erase(waiter_it);
                if (waiters.empty()) m_group_waiters.erase(group_it);
                return true;
            }
            return false;
        }

        /// \brief Cancels one request by request ID.
        /// \param request_id The unique identifier of the request to cancel.
        /// \param callback An optional callback function to execute after cancellation.
        void cancel_request_by_id(uint64_t request_id, std::function<void()> callback) {
            if (m_shutdown || request_id == 0) {
                if (callback) callback();
                return;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_requests_to_cancel_by_id[request_id].push_back(std::move(callback));
        }

        /// \brief Cancels all requests with the specified group ID.
        /// \param group_id The group identifier of requests to cancel.
        /// \param callback An optional callback function to execute after cancellation.
        void cancel_requests_by_group_id(uint64_t group_id, std::function<void()> callback) {
            if (m_shutdown || group_id == 0) {
                if (callback) callback();
                return;
            }
            bool invoke_now = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (group_request_count_unlocked(group_id) == 0) {
                    invoke_now = true;
                } else {
                    m_groups_to_cancel[group_id].push_back(std::move(callback));
                }
            }
            if (invoke_now && callback) {
                callback();
            }
        }

        /// \brief Processes all requests in the manager.
        ///
        /// Executes pending, active, and retry-eligible failed requests.
        void process() override {
            if (m_shutdown) {
                notify_all_group_waiters();
                return;
            }
            process_pending_requests();
            process_active_requests();
            process_retry_failed_requests();
            process_cancel_requests();
            notify_group_waiters_if_idle();
        }

        /// \brief Shuts down the request manager, clearing all active and pending requests.
        /// Stops request processing and releases all resources tied to active and pending requests.
        void shutdown() override {
            if (m_shutdown.exchange(true)) {
                notify_all_group_waiters();
                return;
            }
            cleanup_pending_requests();
            process_cancel_requests();
            m_active_request_batches.clear();
            notify_all_group_waiters();
        }

        /// \brief Checks if there are active, pending, or failed requests.
        /// \return True if there are requests still being managed, otherwise false.
        bool is_loaded() const override {
            std::lock_guard<std::mutex> lock(m_mutex);
            return
                !m_pending_requests.empty() ||
                !m_failed_requests.empty() ||
                !m_active_request_batches.empty() ||
                !m_group_request_counts.empty() ||
                !m_requests_to_cancel_by_id.empty() ||
                !m_groups_to_cancel.empty();
        }

    private:
        mutable std::mutex                                  m_mutex;                  ///< Mutex to protect access to manager state shared with public entry points.
        std::list<std::unique_ptr<HttpRequestContext>>      m_pending_requests;       ///< List of pending HTTP requests awaiting processing.
        std::list<std::unique_ptr<HttpRequestContext>>      m_failed_requests;        ///< List of failed HTTP requests for retrying.
        std::list<std::unique_ptr<HttpBatchRequestHandler>> m_active_request_batches; ///< List of currently active HTTP request batches.
        using callback_list_t = std::list<std::function<void()>>;
        using cancel_map_t = std::unordered_map<uint64_t, callback_list_t>;
        struct GroupWaiter {
            uint64_t id;
            std::function<void()> callback;
        };
        using waiter_list_t = std::list<GroupWaiter>;
        using waiter_map_t = std::unordered_map<uint64_t, waiter_list_t>;
        cancel_map_t                                       m_requests_to_cancel_by_id; ///< Map of request IDs to their associated cancellation callbacks.
        cancel_map_t                                       m_groups_to_cancel;         ///< Map of group IDs to their associated cancellation callbacks.
        waiter_map_t                                       m_group_waiters;            ///< Map of group IDs to callbacks waiting until a group becomes idle.
        std::unordered_map<uint64_t, std::size_t>           m_group_request_counts;     ///< Outstanding admitted requests by group.
        HttpRateLimiter                                     m_rate_limiter;           ///< Rate limiter for controlling request frequency.
        std::atomic<uint64_t>                               m_next_in_flight_token{1}; ///< Atomic counter for sequential rate-limit tokens.
        std::atomic<uint64_t>                               m_request_id_counter = ATOMIC_VAR_INIT(1); ///< Atomic counter for unique request IDs.
        std::atomic<uint64_t>                               m_group_id_counter = ATOMIC_VAR_INIT(1); ///< Atomic counter for group IDs.
        uint64_t                                            m_waiter_id_counter = 1;    ///< Counter for group waiter IDs.
        std::atomic<bool>                                   m_shutdown = ATOMIC_VAR_INIT(false); ///< Flag indicating if shutdown has been requested.
        std::atomic<std::size_t>                            m_max_pending_requests = ATOMIC_VAR_INIT(0); ///< Maximum number of requests accepted into the pending queue, or zero if unbounded.

        std::size_t group_request_count_unlocked(uint64_t group_id) const {
            if (group_id == 0) return 0;

            auto it = m_group_request_counts.find(group_id);
            return it == m_group_request_counts.end() ? 0 : it->second;
        }

        void notify_group_waiters_if_idle() {
            waiter_map_t ready_waiters;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_group_waiters.begin();
                while (it != m_group_waiters.end()) {
                    if (group_request_count_unlocked(it->first) != 0) {
                        ++it;
                        continue;
                    }
                    ready_waiters.emplace(it->first, std::move(it->second));
                    it = m_group_waiters.erase(it);
                }
            }

            for (const auto& item : ready_waiters) {
                for (const auto& waiter : item.second) {
                    if (waiter.callback) waiter.callback();
                }
            }
        }

        void notify_all_group_waiters() {
            waiter_map_t waiters;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                waiters = std::move(m_group_waiters);
                m_group_waiters.clear();
            }

            for (const auto& item : waiters) {
                for (const auto& waiter : item.second) {
                    if (waiter.callback) waiter.callback();
                }
            }
        }

        void complete_group_request(uint64_t group_id) {
            if (group_id == 0) return;

            waiter_list_t ready_waiters;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto count_it = m_group_request_counts.find(group_id);
                if (count_it == m_group_request_counts.end()) return;
                if (count_it->second > 1) {
                    --count_it->second;
                } else {
                    m_group_request_counts.erase(count_it);
                    auto waiter_it = m_group_waiters.find(group_id);
                    if (waiter_it != m_group_waiters.end()) {
                        ready_waiters = std::move(waiter_it->second);
                        m_group_waiters.erase(waiter_it);
                    }
                }
            }

            for (const auto& waiter : ready_waiters) {
                if (waiter.callback) waiter.callback();
            }
        }

        /// \brief Processes all pending requests, moving valid requests to active batches or marking them as failed.
        void process_pending_requests() {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_pending_requests.empty()) return;

            std::vector<std::unique_ptr<HttpRequestContext>> pending_request;
            std::vector<std::unique_ptr<HttpRequestContext>> failed_requests;

            auto it = m_pending_requests.begin();
            while (it != m_pending_requests.end()) {
                auto& context = *it;
                auto& request = context->request;
                // Check if the request is valid.
                if (!request) {
                    failed_requests.push_back(std::move(context));
                    it = m_pending_requests.erase(it);
                    continue;
                }

                // Set up completion callback before allow_request so it is armed
                // even if an exception occurs after the token is committed.
                auto general_limit = request->general_rate_limit;
                auto specific_limit = request->specific_rate_limit;
                uint64_t token = context->in_flight_token;
                auto general_key = request->general_rate_limit_key;
                auto specific_key = request->specific_rate_limit_key;

                // Preserve any previous on_complete so a retry that already owns
                // a sequential token does not lose its cleanup callback.
                auto old_on_complete = std::move(context->on_complete);
                context->on_complete = [this, general_limit, specific_limit, token, general_key, specific_key]() {
                    m_rate_limiter.release_request(general_limit, specific_limit, token, general_key, specific_key);
                };

                // Check if the request is allowed by the rate limiter.
                const bool allowed = m_rate_limiter.allow_request(
                    general_limit,
                    specific_limit,
                    token,
                    request->general_rate_limit_key,
                    request->specific_rate_limit_key);
                if (!allowed) {
                    context->on_complete = std::move(old_on_complete);
                    ++it;
                    continue;
                }

                pending_request.push_back(std::move(context));
                it = m_pending_requests.erase(it);
            }
            lock.unlock();

            // Handle failed requests by calling their callback with a 400 status.
            if (!failed_requests.empty()) {
                for (const auto &context : failed_requests) {
#                   if __cplusplus >= 201402L
                    auto response = std::make_unique<HttpResponse>();
#                   else
                    auto response = std::unique_ptr<HttpResponse>(new HttpResponse());
#                   endif
                    const long BAD_REQUEST = 400;
                    response->error_code = utils::make_error_code(CURLE_OK);
                    response->status_code = BAD_REQUEST;
                    response->ready = true;
                    context->callback(std::move(response));
                    context->complete();
                }
                failed_requests.clear();
            }

            // If there are ready requests, create a new HttpBatchRequestHandler to manage them.
            if (pending_request.empty()) return;
            try {
#               if __cplusplus >= 201402L
                m_active_request_batches.push_back(std::make_unique<HttpBatchRequestHandler>(pending_request));
#               else
                m_active_request_batches.push_back(std::unique_ptr<HttpBatchRequestHandler>(new HttpBatchRequestHandler(pending_request)));
#               endif
            } catch (...) {
                for (auto& context : pending_request) {
                    if (!context || !context->callback) continue;
#                   if __cplusplus >= 201402L
                    auto response = std::make_unique<HttpResponse>();
#                   else
                    auto response = std::unique_ptr<HttpResponse>(new HttpResponse());
#                   endif
                    response->error_code = utils::make_error_code(utils::ClientError::AbortedDuringDestruction);
                    response->status_code = 499; // Client closed request
                    response->ready = true;
                    context->callback(std::move(response));
                    context->complete();
                }
                return;
            }
        }

        /// \brief Processes active requests, moving failed ones to the failed requests list for retrying.
        void process_active_requests() {
            auto it = m_active_request_batches.begin();
            while (it != m_active_request_batches.end()) {
                auto& request = *it;
                if (!request->process()) {
                    ++it;
                    continue;
                }
                auto failed_requests = request->extract_failed_requests();
                it = m_active_request_batches.erase(it);
                for (auto& request : failed_requests) {
                    m_failed_requests.push_back(std::move(request));
                }
            }
        }

        /// \brief Attempts to retry failed requests if their retry delay has passed.
        void process_retry_failed_requests() {
            auto it = m_failed_requests.begin();
            while (it != m_failed_requests.end()) {
                auto& request_context = *it;
                if (!request_context || !request_context->request) {
                    it = m_failed_requests.erase(it);
                    continue;
                }

                const auto now = std::chrono::steady_clock::now();
                const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - request_context->start_time);
                const auto& retry_delay_ms = request_context->request->retry_delay_ms;
                if (duration.count() >= retry_delay_ms) {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_pending_requests.push_back(std::move(request_context));
                    lock.unlock();
                    it = m_failed_requests.erase(it);
                    continue;
                }
                ++it;
            }
        }

        /// \brief Processes and cancels HTTP requests based on their request or group IDs.
        void process_cancel_requests() {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_requests_to_cancel_by_id.empty() && m_groups_to_cancel.empty()) return;

            auto requests_to_cancel = std::move(m_requests_to_cancel_by_id);
            auto groups_to_cancel = std::move(m_groups_to_cancel);
            m_requests_to_cancel_by_id.clear();
            m_groups_to_cancel.clear();

            std::list<std::unique_ptr<HttpRequestContext>> canceled_pending_requests;
            auto pending_it = m_pending_requests.begin();
            while (pending_it != m_pending_requests.end()) {
                const auto& ctx = *pending_it;
                if (!matches_cancel(ctx, requests_to_cancel, groups_to_cancel)) {
                    ++pending_it;
                    continue;
                }
                canceled_pending_requests.push_back(std::move(*pending_it));
                pending_it = m_pending_requests.erase(pending_it);
            }
            lock.unlock();

            for (const auto& request_context : canceled_pending_requests) {
                request_context->callback(make_cancelled_response());
                request_context->complete();
            }

            auto failed_it = m_failed_requests.begin();
            while (failed_it != m_failed_requests.end()) {
                const auto& request_context = *failed_it;
                if (!matches_cancel(request_context, requests_to_cancel, groups_to_cancel)) {
                    ++failed_it;
                    continue;
                }
                request_context->callback(make_cancelled_response());
                request_context->complete();
                failed_it = m_failed_requests.erase(failed_it);
            }

            for (const auto &handler : m_active_request_batches) {
                handler->cancel_requests(requests_to_cancel, groups_to_cancel);
            }

            invoke_cancel_callbacks(requests_to_cancel);
            invoke_cancel_callbacks(groups_to_cancel);
        }

        static bool matches_cancel(
                const std::unique_ptr<HttpRequestContext>& ctx,
                const cancel_map_t& requests_to_cancel,
                const cancel_map_t& groups_to_cancel) {
            if (!ctx || !ctx->request) return false;
            const auto request_id = ctx->request->request_id;
            const auto group_id = ctx->request->group_id;
            return
                (request_id != 0 && requests_to_cancel.count(request_id) > 0) ||
                (group_id != 0 && groups_to_cancel.count(group_id) > 0);
        }

        static HttpResponsePtr make_cancelled_response() {
#           if __cplusplus >= 201402L
            auto response = std::make_unique<HttpResponse>();
#           else
            auto response = std::unique_ptr<HttpResponse>(new HttpResponse());
#           endif
            response->error_code = utils::make_error_code(utils::ClientError::CancelledByUser);
            response->status_code = 499;
            response->ready = true;
            return response;
        }

        static void invoke_cancel_callbacks(const cancel_map_t& requests_to_cancel) {
            for (const auto& request : requests_to_cancel) {
                for (const auto& callback : request.second) {
                    if (callback) callback();
                }
            }
        }

        /// \brief Cleans up pending requests, marking each as failed and invoking its callback.
        void cleanup_pending_requests() {
            std::unique_lock<std::mutex> lock(m_mutex);
            auto pending_requests = std::move(m_pending_requests);
            auto failed_requests  = std::move(m_failed_requests);
            m_pending_requests.clear();
            m_failed_requests.clear();
            lock.unlock();

            for (const auto &request_context : pending_requests) {
#               if __cplusplus >= 201402L
                auto response = std::make_unique<HttpResponse>();
#               else
                auto response = std::unique_ptr<HttpResponse>(new HttpResponse());
#               endif
                const long CANCELED_REQUEST_CODE = 499;
                response->error_code = utils::make_error_code(CURLE_OK);
                response->status_code = CANCELED_REQUEST_CODE;
                response->ready = true;
                request_context->callback(std::move(response));
                request_context->complete();
            }
            for (const auto &request_context : failed_requests) {
#               if __cplusplus >= 201402L
                auto response = std::make_unique<HttpResponse>();
#               else
                auto response = std::unique_ptr<HttpResponse>(new HttpResponse());
#               endif
                const long CANCELED_REQUEST_CODE = 499;
                response->error_code = utils::make_error_code(CURLE_OK);
                response->status_code = CANCELED_REQUEST_CODE;
                response->ready = true;
                request_context->callback(std::move(response));
                request_context->complete();
            }
        }

        /// \brief Private constructor to initialize global resources (e.g., cURL).
        HttpRequestManager() {
            curl_global_init(CURL_GLOBAL_ALL);
        }

        /// \brief Private destructor to clean up global resources.
        virtual ~HttpRequestManager() {
            curl_global_cleanup();
        }

        /// \brief Deleted copy constructor to enforce the singleton pattern.
        HttpRequestManager(const HttpRequestManager&) = delete;

        /// \brief Deleted copy assignment operator to enforce the singleton pattern.
        HttpRequestManager& operator=(const HttpRequestManager&) = delete;

    }; // HttpRequestManager

}; // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HPP_INCLUDED

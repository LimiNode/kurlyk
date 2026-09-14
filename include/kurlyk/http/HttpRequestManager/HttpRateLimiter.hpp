#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_RATE_LIMITER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_RATE_LIMITER_HPP_INCLUDED

/// \file HttpRateLimiter.hpp
/// \brief Defines the HttpRateLimiter class for managing rate limits on HTTP requests.

namespace kurlyk {

    /// \class HttpRateLimiter
    /// \brief Manages rate limits for HTTP requests.
    ///
    /// Rate limits are referenced by RAII handles. A limit remains physically
    /// present while at least one HttpRateLimitHandlePtr exists. Calling
    /// remove_limit(id) only releases the manager-owned handle. If pending or
    /// active requests still hold copied handles, the limit remains active until
    /// those requests are destroyed.
    ///
    /// Each limit can be partitioned by string keys. Requests sharing the same key
    /// within the same limit ID share rate-limit state; different keys are independent.
    /// An empty key maps to the default shared state.
    class HttpRateLimiter {
    public:
        /// \brief Creates a new rate limit and returns its RAII handle.
        /// \param requests_per_period Maximum requests allowed within the period. 0 means unlimited.
        /// \param period_ms Period duration in milliseconds.
        /// \param sequential When \`true\`, no other request sharing this limit may start until
        ///        the current request (including all its retries) has finished.
        /// \return Shared RAII handle for the created limit.
        HttpRateLimitHandlePtr create_limit_handle(long requests_per_period, long period_ms, bool sequential = false) {
            std::lock_guard<std::mutex> lock(m_mutex);

            const long id = m_next_id++;

            m_limits[id] = LimitData{
                requests_per_period,
                period_ms,
                sequential,
                false,
                {}
            };

            // Do not use make_shared here: private constructor access through
            // std::make_shared can be problematic on some compilers.
            HttpRateLimitHandlePtr handle(
                new HttpRateLimitHandle(
                    id,
                    [this](long limit_id) {
                        remove_limit_internal(limit_id);
                    }
                )
            );

            m_owned_handles[id] = handle;
            return handle;
        }

        /// \brief Legacy API: creates a new rate limit and returns only its ID.
        ///
        /// \warning Prefer create_limit_handle(). Requests that only store the ID
        /// do not keep the limit alive by themselves. The manager-owned handle
        /// keeps the limit alive until remove_limit(id) is called.
        long create_limit(long requests_per_period, long period_ms) {
            const auto handle = create_limit_handle(requests_per_period, period_ms);
            return handle ? handle->id() : 0;
        }

        /// \brief Releases manager-owned handle for the specified limit ID.
        ///
        /// This does not necessarily erase LimitData immediately. If any pending
        /// or active request still holds a copied handle, the limit stays alive.
        /// Physical erase happens from HttpRateLimitHandle destructor.
        ///
        /// \param limit_id Rate-limit ID.
        /// \return \`true\` if manager-owned handle was found and released.
        bool remove_limit(long limit_id) {
            HttpRateLimitHandlePtr retired_handle;

            std::unique_lock<std::mutex> lock(m_mutex);

            auto it = m_owned_handles.find(limit_id);
            if (it == m_owned_handles.end()) {
                return false;
            }

            retired_handle = std::move(it->second);
            m_owned_handles.erase(it);

            lock.unlock();

            // retired_handle is destroyed after m_mutex is released.
            // If it is the last shared_ptr, its destructor calls
            // remove_limit_internal(limit_id).
            return true;
        }

        /// \brief Releases manager-owned handle for the specified limit handle.
        /// \param handle Rate-limit handle.
        /// \return \`true\` if manager-owned handle was found and released.
        bool remove_limit(const HttpRateLimitHandlePtr& handle) {
            return handle ? remove_limit(handle->id()) : false;
        }

        /// \brief Returns manager-owned handle by ID.
        ///
        /// \note This is mainly for compatibility with legacy ID-based API.
        HttpRateLimitHandlePtr get_limit(long limit_id) {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_owned_handles.find(limit_id);
            if (it == m_owned_handles.end()) {
                return HttpRateLimitHandlePtr();
            }

            return it->second;
        }

        /// \brief Checks if a request is allowed by two optional rate-limit handles
        ///        with in-flight token tracking for sequential limits.
        ///
        /// Counters and in-flight sets are updated only if both limits allow the request.
        /// \param general_limit General rate-limit handle (may be empty).
        /// \param specific_limit Specific rate-limit handle (may be empty).
        /// \param in_flight_token Token identifying the in-flight request; 0 skips sequential checks.
        /// \param general_key Partition key for the general limit; empty means default state.
        /// \param specific_key Partition key for the specific limit; empty means default state.
        /// \return \`true\` if the request is allowed, \`false\` otherwise (state unchanged on failure).
        bool allow_request(
                const HttpRateLimitHandlePtr& general_limit,
                const HttpRateLimitHandlePtr& specific_limit,
                uint64_t in_flight_token,
                const std::string& general_key,
                const std::string& specific_key
            ) {
            std::lock_guard<std::mutex> lock(m_mutex);

            const long general_id = general_limit ? general_limit->id() : 0;
            const long specific_id = specific_limit ? specific_limit->id() : 0;

            auto general_it = general_id != 0 ? m_limits.find(general_id) : m_limits.end();
            auto specific_it = specific_id != 0 ? m_limits.find(specific_id) : m_limits.end();

            if (general_it == m_limits.end() && specific_it == m_limits.end()) {
                return true;
            }

            const auto now = std::chrono::steady_clock::now();

            bool general_limit_allowed = true;
            bool specific_limit_allowed = true;

            if (general_it != m_limits.end()) {
                if (general_it->second.removed) {
                    general_limit_allowed = true;
                } else {
                    general_limit_allowed = can_pass(general_it->second, general_key, in_flight_token, now);
                }
            }

            if (specific_it != m_limits.end()) {
                if (specific_it->second.removed) {
                    specific_limit_allowed = true;
                } else {
                    specific_limit_allowed = can_pass(specific_it->second, specific_key, in_flight_token, now);
                }
            }

            if (!general_limit_allowed || !specific_limit_allowed) {
                return false;
            }

            const bool same_limit =
                general_id != 0 &&
                general_id == specific_id &&
                general_key == specific_key;

            if (general_it != m_limits.end() && !general_it->second.removed) {
                commit_limit(general_it->second, general_key, in_flight_token, now);
            }

            if (!same_limit && specific_it != m_limits.end() && !specific_it->second.removed) {
                commit_limit(specific_it->second, specific_key, in_flight_token, now);
            }

            // Periodic garbage collection of stale keys.
            if ((++m_gc_counter & 63) == 0) {
                gc_stale_keys(now);
            }

            return true;
        }

        /// \brief Handle-based overload without explicit token or keys (token = 0, keys empty).
        bool allow_request(
                const HttpRateLimitHandlePtr& general_limit,
                const HttpRateLimitHandlePtr& specific_limit
            ) {
            return allow_request(general_limit, specific_limit, 0, std::string(), std::string());
        }

        /// \brief Legacy API: checks if request is allowed by two limit IDs.
        ///
        /// \warning Prefer handle-based overload. ID-based requests do not keep
        /// limits alive by themselves.
        bool allow_request(long general_rate_limit_id, long specific_rate_limit_id) {
            return allow_request(
                get_limit(general_rate_limit_id),
                get_limit(specific_rate_limit_id),
                0,
                std::string(),
                std::string()
            );
        }

        /// \brief Releases in-flight tokens for sequential rate limits.
        ///
        /// Must be called when a request finishes (including after cancellation or destruction).
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
            if (in_flight_token == 0) return;

            std::lock_guard<std::mutex> lock(m_mutex);

            const long general_id = general_limit ? general_limit->id() : 0;
            const long specific_id = specific_limit ? specific_limit->id() : 0;

            const bool same_limit =
                general_id != 0 &&
                general_id == specific_id &&
                general_key == specific_key;

            auto general_it = general_id != 0 ? m_limits.find(general_id) : m_limits.end();
            auto specific_it = specific_id != 0 ? m_limits.find(specific_id) : m_limits.end();

            if (general_it != m_limits.end() && general_it->second.sequential) {
                release_key(general_it->second, general_key, in_flight_token);
            }

            if (!same_limit && specific_it != m_limits.end() && specific_it->second.sequential) {
                release_key(specific_it->second, specific_key, in_flight_token);
            }
        }

        /// \brief Legacy overload without keys.
        void release_request(
                const HttpRateLimitHandlePtr& general_limit,
                const HttpRateLimitHandlePtr& specific_limit,
                uint64_t in_flight_token) {
            release_request(general_limit, specific_limit, in_flight_token, std::string(), std::string());
        }

        /// \tparam Duration Duration type; defaults to `std::chrono::milliseconds`.
        /// \param general_limit General rate-limit handle (may be empty).
        /// \param specific_limit Specific rate-limit handle (may be empty).
        /// \param general_key Partition key for the general limit; empty means default shared state.
        /// \param specific_key Partition key for the specific limit; empty means default shared state.
        /// \return RateLimitDelay describing the maximum delay across both dimensions.
        ///         `duration` is 0 only when all present dimensions allow immediately.
        ///         `sequential_blocked` is true when at least one dimension is blocked
        ///         by a sequential in-flight request (`duration == Duration::max()`).
        template<typename Duration = std::chrono::milliseconds>
        RateLimitDelay<Duration> time_until_next_allowed(
            const HttpRateLimitHandlePtr& general_limit,
            const HttpRateLimitHandlePtr& specific_limit,
            const std::string& general_key,
            const std::string& specific_key
            ) {
            std::lock_guard<std::mutex> lock(m_mutex);

            const auto now = std::chrono::steady_clock::now();
            RateLimitDelay<Duration> result;
            result.duration = Duration{0};
            result.sequential_blocked = false;

            const long general_id = general_limit ? general_limit->id() : 0;
            const long specific_id = specific_limit ? specific_limit->id() : 0;

            auto it = general_id != 0 ? m_limits.find(general_id) : m_limits.end();
            if (it != m_limits.end()) {
                const Duration general_delay = time_until_limit_allows<Duration>(it->second, general_key, now);
                result.duration = (std::max)(result.duration, general_delay);
                if (general_delay == (Duration::max)()) {
                    result.sequential_blocked = true;
                }
            }

            it = specific_id != 0 ? m_limits.find(specific_id) : m_limits.end();
            if (it != m_limits.end()) {
                const Duration specific_delay = time_until_limit_allows<Duration>(it->second, specific_key, now);
                result.duration = (std::max)(result.duration, specific_delay);
                if (specific_delay == (Duration::max)()) {
                    result.sequential_blocked = true;
                }
            }

            return result;
        }

        /// \brief Legacy overload without explicit partition keys (uses default shared state).
        template<typename Duration = std::chrono::milliseconds>
        RateLimitDelay<Duration> time_until_next_allowed(
            const HttpRateLimitHandlePtr& general_limit,
            const HttpRateLimitHandlePtr& specific_limit
            ) {
            return time_until_next_allowed<Duration>(general_limit, specific_limit, std::string(), std::string());
        }

        /// \brief Legacy API: calculates delay by limit IDs.
        /// \tparam Duration Duration type; defaults to `std::chrono::milliseconds`.
        /// \param general_rate_limit_id General rate-limit ID.
        /// \param specific_rate_limit_id Specific rate-limit ID.
        /// \return RateLimitDelay describing the maximum delay across both dimensions.
        /// \warning Prefer handle-based overload. ID-based requests do not keep limits alive.
        template<typename Duration = std::chrono::milliseconds>
        RateLimitDelay<Duration> time_until_next_allowed(long general_rate_limit_id, long specific_rate_limit_id) {
            return time_until_next_allowed<Duration>(
                get_limit(general_rate_limit_id),
                get_limit(specific_rate_limit_id),
                std::string(),
                std::string()
            );
        }

        /// \brief Finds the shortest delay among all physically alive limits.
        /// \tparam Duration Duration type; defaults to `std::chrono::milliseconds`.
        /// \return RateLimitDelay where `duration` is the minimum positive delay across
        ///         all keys of all limits. If no limit reports a positive delay,
        ///         `duration` is 0. `sequential_blocked` is true when the selected
        ///         positive delay is `Duration::max()`, meaning no finite positive
        ///         delay was found among currently delayed keys.
        template<typename Duration = std::chrono::milliseconds>
        RateLimitDelay<Duration> time_until_any_limit_allows() {
            std::lock_guard<std::mutex> lock(m_mutex);

            const auto now = std::chrono::steady_clock::now();

            RateLimitDelay<Duration> result;
            result.duration = Duration{0};
            result.sequential_blocked = false;

            Duration min_delay = (Duration::max)();
            bool has_positive_delay = false;

            for (const auto& pair : m_limits) {
                const auto& limit = pair.second;
                for (const auto& key_pair : limit.keys) {
                    const Duration delay = time_until_key_allows<Duration>(limit, key_pair.second, now);
                    if (delay.count() <= 0) {
                        continue;
                    }
                    has_positive_delay = true;
                    if (delay < min_delay) {
                        min_delay = delay;
                    }
                }
            }

            if (has_positive_delay) {
                result.duration = min_delay;
                result.sequential_blocked = (min_delay == (Duration::max)());
            }

            return result;
        }

    private:
        using time_point_t = std::chrono::steady_clock::time_point;

        /// \struct KeyState
        /// \brief Per-key mutable runtime state inside a rate limit.
        struct KeyState {
            long count = 0;
            time_point_t start_time;
            std::unordered_set<uint64_t> in_flight_tokens;
        };

        /// \struct LimitData
        /// \brief Immutable limit parameters plus per-key mutable state.
        struct LimitData {
            long requests_per_period = 0;
            long period_ms = 0;
            bool sequential = false;                               ///< When \`true\`, blocks other requests until the current one finishes.
            bool removed = false;                                   ///< \`true\` when the manager-owned handle has been released; physical erase is deferred until all keys are empty.
            std::unordered_map<std::string, KeyState> keys;       ///< Mutable state per partition key.

            LimitData() {}

            LimitData(
                long request_limit,
                long period,
                bool is_sequential,
                bool is_removed,
                const std::unordered_map<std::string, KeyState>& key_states)
                : requests_per_period(request_limit),
                  period_ms(period),
                  sequential(is_sequential),
                  removed(is_removed),
                  keys(key_states) {}
        };

        /// \brief Marks a limit as removed and erases it only when no key state remains.
        ///
        /// This method is intentionally private. It must be called only from
        /// HttpRateLimitHandle destruction path.
        /// \note HttpRequestManager (and its embedded HttpRateLimiter) is intentionally
        /// never destroyed. The `this` captured in HttpRateLimitHandle destructor is
        /// therefore always valid. Changing the singleton to a destructible form requires
        /// replacing this raw capture.
        bool remove_limit_internal(long limit_id) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_limits.find(limit_id);
            if (it == m_limits.end()) {
                return false;
            }
            auto& limit = it->second;
            limit.removed = true;
            // Erase only if no keys hold runtime state.
            if (!limit.keys.empty()) {
                return false;
            }
            return m_limits.erase(limit_id) > 0;
        }

        /// \brief Looks up a KeyState by key, creating it lazily if necessary.
        KeyState& get_key_state(LimitData& limit, const std::string& key) {
            return limit.keys[key];
        }

        /// \brief Looks up a KeyState by key for read-only access (no insertion).
        const KeyState* find_key_state(const LimitData& limit, const std::string& key) const {
            auto it = limit.keys.find(key);
            if (it == limit.keys.end()) {
                return nullptr;
            }
            return &it->second;
        }

        /// \brief Checks if a key within a limit can pass both sequential and count/period constraints.
        bool can_pass(const LimitData& limit, const std::string& key, uint64_t token, const time_point_t& now) const {
            const KeyState* state = find_key_state(limit, key);
            if (!state) {
                // No state yet: only need to check the base limit parameters.
                if (limit.requests_per_period == 0) {
                    return true;
                }
                return true; // count is 0, so always under limit.
            }
            if (limit.sequential && token != 0) {
                if (!state->in_flight_tokens.empty() &&
                    state->in_flight_tokens.count(token) == 0) {
                    return false;
                }
            }
            return check_key(limit, *state, now);
        }

        bool check_key(const LimitData& limit_data, const KeyState& state, const time_point_t& now) const {
            if (limit_data.requests_per_period == 0) {
                return true;
            }

            const auto elapsed_time =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - state.start_time
                );

            if (elapsed_time.count() >= limit_data.period_ms) {
                return true;
            }

            return state.count < limit_data.requests_per_period;
        }

        /// \brief Commits in-flight token and count/period state after a successful can_pass.
        void commit_limit(LimitData& limit, const std::string& key, uint64_t token, const time_point_t& now) {
            KeyState& state = get_key_state(limit, key);
            if (limit.sequential && token != 0) {
                state.in_flight_tokens.insert(token);
            }

            // Retry attempts may reuse the same in-flight token to avoid self-blocking
            // sequential limits, but each actual HTTP attempt still consumes the
            // count-based rate limit.
            update_key(limit, state, now);
        }

        void update_key(LimitData& limit_data, KeyState& state, const time_point_t& now) {
            if (limit_data.requests_per_period == 0) {
                return;
            }

            const auto elapsed_time =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - state.start_time
                );

            if (elapsed_time.count() >= limit_data.period_ms) {
                state.start_time = now;
                state.count = 0;
            }

            ++state.count;
        }

        /// \brief Releases an in-flight token from a specific key and erases the key if it has no runtime state.
        void release_key(LimitData& limit, const std::string& key, uint64_t token) {
            auto it = limit.keys.find(key);
            if (it == limit.keys.end()) {
                return;
            }
            auto& state = it->second;
            state.in_flight_tokens.erase(token);
            if (state.in_flight_tokens.empty() && state.count == 0) {
                limit.keys.erase(it);
            }
            if (limit.removed && limit.keys.empty()) {
                // We cannot erase `limit` here because we are iterating or
                // the caller holds a reference. Deferred to remove_limit_internal
                // or next gc pass. In practice remove_limit_internal already
                // tries; here we rely on gc_stale_keys or the next remove_limit_internal.
            }
        }

        template<typename Duration>
        Duration time_until_limit_allows(
            const LimitData& limit,
            const std::string& key,
            const time_point_t& now
        ) const {
            const KeyState* state = find_key_state(limit, key);
            if (!state) {
                // No state means no in-flight tokens and count is 0.
                if (limit.sequential) {
                    return Duration{0};
                }
                if (limit.requests_per_period == 0) {
                    return Duration{0};
                }
                return Duration{0};
            }
            return time_until_key_allows<Duration>(limit, *state, now);
        }

        template<typename Duration>
        Duration time_until_key_allows(
            const LimitData& limit,
            const KeyState& state,
            const time_point_t& now
        ) const {
            if (limit.sequential &&
                !state.in_flight_tokens.empty()) {
                return (Duration::max)();
            }

            if (limit.requests_per_period == 0) {
                return Duration{0};
            }

            const auto elapsed =
                std::chrono::duration_cast<Duration>(now - state.start_time);

            const auto period_duration =
                std::chrono::duration_cast<Duration>(
                    std::chrono::milliseconds(limit.period_ms)
                );

            if (elapsed >= period_duration ||
                state.count < limit.requests_per_period) {
                return Duration{0};
            }

            return period_duration - elapsed;
        }

        /// \brief Erases keys that are empty or whose period has expired.
        void gc_stale_keys(const time_point_t& now) {
            for (auto limit_it = m_limits.begin(); limit_it != m_limits.end(); ) {
                auto& limit = limit_it->second;
                for (auto key_it = limit.keys.begin(); key_it != limit.keys.end(); ) {
                    const auto& state = key_it->second;
                    if (state.in_flight_tokens.empty() && state.count == 0) {
                        key_it = limit.keys.erase(key_it);
                    } else if (state.in_flight_tokens.empty()) {
                        const auto elapsed =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - state.start_time
                            );
                        if (elapsed.count() >= limit.period_ms) {
                            key_it = limit.keys.erase(key_it);
                        } else {
                            ++key_it;
                        }
                    } else {
                        ++key_it;
                    }
                }

                if (limit.removed && limit.keys.empty()) {
                    limit_it = m_limits.erase(limit_it);
                } else {
                    ++limit_it;
                }
            }
        }

    private:
        mutable std::mutex m_mutex;

        long m_next_id = 1;

        /// \brief Physically alive limit data.
        ///
        /// A record remains here while at least one HttpRateLimitHandlePtr exists.
        std::unordered_map<long, LimitData> m_limits;

        /// \brief Manager-owned handles for limits created through create_limit_handle().
        ///
        /// remove_limit(id) releases the handle from this map. If requests still
        /// hold copies, physical data stays alive until the last copy is destroyed.
        std::unordered_map<long, HttpRateLimitHandlePtr> m_owned_handles;

        size_t m_gc_counter = 0;
    };

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_RATE_LIMITER_HPP_INCLUDED

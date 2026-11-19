# Rate Limiting and Streaming

Use this file when changing `HttpRateLimiter`, `HttpRateLimitHandle`,
`HttpBatchRequestHandler`, `HttpRequestManager::process_pending_requests()`,
or any HTTP response streaming callback.

## Core Vocabulary

- `HttpRateLimiter` — singleton-embedded rate-limit engine inside
  `HttpRequestManager`. Manages per-ID rate limits; each ID can be partitioned by
  a string key.
- `HttpRateLimitHandle` — RAII shared handle that keeps a limit entry physically
  alive. When the last handle is destroyed, the limit is marked `removed=true`
  and erased once all keys are empty.
- `LimitData` — immutable parameters for one limit ID: `requests_per_period`,
  `period_ms`, `sequential`, `removed`.
- `KeyState` — mutable per-partition-key runtime state: `count`, `start_time`,
  `in_flight_tokens`.
- `HttpBatchRequestHandler` — executes one or more active HTTP requests via
  libcurl multi interface.
- `HttpRequestContext` — pending/active/failed request container. Holds the
  `in_flight_token` used for sequential limits and the `on_complete` callback
  that releases it.

## Partitioned Rate-Limit Semantics

A single limit ID can be partitioned by string key:

- Same limit ID + same key = shared state.
- Same limit ID + different keys = independent state.
- Empty key (`""`) is a regular map key; it behaves as the default shared state
  and preserves backward compatibility with the old global-bucket behavior.

There are **two** parallel dimensions: general limit and specific limit. Each
has its own key. A request passes only when **both** dimensions allow it.

### Example: general/specific with different keys

```cpp
auto general = manager.create_rate_limit(1, 60000);   // 1 req / 60s
auto specific = manager.create_rate_limit(10, 60000); // 10 req / 60s

// Allowed: gen_k has not been used yet.
manager.allow_request(general, specific, 1, "gen_k", "spec_k");

// Blocked: gen_k already used (count=1), even though spec_k2 is new.
manager.allow_request(general, specific, 2, "gen_k", "spec_k2");

// Allowed: gen_k2 is new, spec_k is under 10.
manager.allow_request(general, specific, 3, "gen_k2", "spec_k");
```

## Sequential Mode

When `sequential=true` for a limit, no other request sharing that **same limit
ID + same partition key** may start until the current request finishes (including
all retries). This is tracked through `in_flight_tokens` inside `KeyState`.

Sequential mode does **not** block different keys inside the same limit ID.

### Example: sequential inside a partition

```cpp
auto limit = manager.create_rate_limit(10, 60000, true); // sequential

// Allowed.
manager.allow_request(limit, limit, 1, "seq_key", "seq_key");

// Blocked: same limit + same key has an in-flight token.
manager.allow_request(limit, limit, 2, "seq_key", "seq_key");

// Allowed: different key is independent.
manager.allow_request(limit, limit, 3, "other_key", "other_key");

// Release frees only the correct partition.
manager.release_request(limit, limit, 1, "seq_key", "seq_key");
```

## Token Lifecycle and the Retry Callback Bug (Fixed)

When a request is promoted from pending to active, `process_pending_requests()`
assigns an `on_complete` lambda that releases the sequential token:

```cpp
context->on_complete = [this, general_limit, specific_limit, token,
                        general_key, specific_key]() {
    m_rate_limiter.release_request(general_limit, specific_limit,
                                   token, general_key, specific_key);
};
```

If the request later fails and enters the retry queue, `process_retry_failed_requests()`
moves it back to `m_pending_requests`. On the next `process_pending_requests()`,
`allow_request()` is called again. If `allow_request()` returns `false` (for
example, because the count/period limit is temporarily exhausted), the request
stays in pending.

**Original bug**: the code overwrote `context->on_complete = nullptr` on denial.
This destroyed the release callback, so when the request eventually succeeded on
a later attempt, the sequential token was never released — a permanent leak for
that key.

**Fix**: save `old_on_complete` before overwriting, restore it when `!allowed`:

```cpp
auto old_on_complete = std::move(context->on_complete);
context->on_complete = [/* release lambda */](...) { ... };

if (!allowed) {
    context->on_complete = std::move(old_on_complete);  // restore
    ++it;
    continue;
}
```

When modifying retry or sequential logic, always verify that token release
survives temporary denials.

## Garbage Collection

`KeyState` entries are cleaned via a hybrid strategy:

1. **Inline erase** during `allow_request()`: if a checked key has no in-flight
   tokens, its count has expired, and the key is not the one being committed, it
   may be erased.
2. **Periodic sweep** every 64 `allow_request()` calls (`m_gc_counter & 63`):
   `gc_stale_keys(now)` iterates all limits and all keys, erasing entries whose
   count is expired and that have no in-flight tokens.
3. **Physical limit erase** in `remove_limit_internal()`: a limit marked
   `removed=true` is erased from `m_limits` only when `limit.keys.empty()`.

The `HttpRateLimitHandle` destructor invokes `remove_limit_internal()`; if other
requests still hold copied handles, the limit stays alive until the last one
is gone.

## API Surface

### Per-request keys (on `HttpRequest`)

```cpp
std::string general_rate_limit_key;   // partition key for general limit
std::string specific_rate_limit_key;  // partition key for specific limit
```

### Client setters (on `HttpClient`)

```cpp
void set_rate_limit_key(const std::string& key);
void set_general_rate_limit_key(const std::string& key);
void set_specific_rate_limit_key(const std::string& key);
void set_rate_limit_keys(const std::string& general_key,
                         const std::string& specific_key);
```

### Manager methods (on `HttpRequestManager` / `HttpRateLimiter`)

```cpp
HttpRateLimitHandlePtr create_rate_limit(long rpp, long period_ms,
                                         bool sequential = false);

bool allow_request(const HttpRateLimitHandlePtr& general,
                   const HttpRateLimitHandlePtr& specific,
                   uint64_t in_flight_token,
                   const std::string& general_key,
                   const std::string& specific_key);

void release_request(const HttpRateLimitHandlePtr& general,
                     const HttpRateLimitHandlePtr& specific,
                     uint64_t in_flight_token,
                     const std::string& general_key,
                     const std::string& specific_key);

template<typename Duration = std::chrono::milliseconds>
RateLimitDelay<Duration> time_until_next_allowed(...);
```

### time_until_* queries

Two query functions let callers know how long they must wait before a request
will be accepted. Both return `RateLimitDelay<Duration>`, which carries both a
delay value and a semantic flag.

```cpp
template<typename Duration = std::chrono::milliseconds>
RateLimitDelay<Duration> time_until_next_allowed(general, specific, general_key, specific_key);

template<typename Duration = std::chrono::milliseconds>
RateLimitDelay<Duration> time_until_any_limit_allows();
```

**`duration`**
- `0` — at least one limit allows a request immediately.
- Positive finite value — a count/period limit is temporarily exhausted; the
caller may `sleep_for(duration)` and retry.
- `Duration::max()` — the delay is unbounded because a **sequential** in-flight
request is still running. The caller must **not** `sleep_for(duration)`; instead
it should wait for `release_request()` (or for the corresponding request to
finish) and query again.

**`sequential_blocked`**
- `false` — the delay is a normal time-based value (0 or positive finite).
- `true` — `duration == Duration::max()` because at least one relevant limit is
blocked by a sequential in-flight token. This is a signal to use event-driven
waiting rather than `sleep_for`.

Differences between the two functions:

- `time_until_next_allowed(general, specific, ...)` looks at **two specific**
limits (general + specific) and returns the **maximum** of their delays. A
request will be allowed only when *both* dimensions are ready, so the caller
must wait for the larger delay.
- `time_until_any_limit_allows()` scans **all** physically alive limits and
returns the **minimum** positive delay among them. It answers the question
"when will at least one limit become ready?" rather than "when will my
particular request be allowed".

## Streaming Callbacks

`HttpRequest::streaming` (or `HttpClient::set_streaming(true)`) enables
intermediate responses through the normal request callback:

```cpp
client.set_streaming(true);
client.get("/events", kurlyk::QueryParams(), kurlyk::Headers(),
    [](kurlyk::HttpResponsePtr response) {
        if (response->stream_chunk) {
            // Process response->content; ready is false.
        } else if (response->ready) {
            // Final accumulated response or terminal error.
        }
    });
```

These run on the `NetworkWorker` thread inside `HttpBatchRequestHandler` and
must not block. `HttpRequestContext` catches callback exceptions and reports
them through the worker error handler. A request receives one final callback;
group completion and sequential-token release happen after that callback returns.

### Response body budget

`HttpRequest::max_response_body_bytes`, `HttpRequest::set_max_response_body_bytes()`
and `HttpClient::set_max_response_body_bytes()` configure a per-attempt limit.
The default value `0` preserves the existing unlimited behavior. Set a finite
value when handling untrusted response bodies:

```cpp
client.set_max_response_body_bytes(1024 * 1024);
```

The libcurl write callback checks actual received bytes before appending them
to the accumulated response or emitting a streaming chunk. The count applies
after transfer decoding and any content decoding enabled for that request; it
does not trust `Content-Length`, so chunked and connection-close responses have
the same bound. Redirect response bodies in the same attempt share the budget.
When `debug_header` is enabled, libcurl also sends headers through that callback,
so those diagnostic bytes count against the budget too.

If an incoming chunk would exceed the remaining budget, the whole chunk is
rejected and reception stops. The final response has
`utils::ClientError::ResponseTooLarge` and a stable explanatory message. Its
content may retain the previously accepted prefix, never more than the limit;
it is not a successful or complete payload. The configured retry policy is
ignored for this error, including when no streaming chunk was emitted. The
normal final-callback, cancellation, group completion and resource cleanup
contracts still apply. This is a body-byte bound, not a total process-memory,
header-size or response-time bound.

## Safety Invariants

- `HttpRateLimitHandle` destructor must not be called while holding
  `m_rate_limiter.m_mutex`; it may invoke `remove_limit_internal()` which locks
  the same mutex.
- `allow_request()` commits state only after **both** general and specific
  dimensions approve. If either denies, no state changes.
- `release_request()` must use the `same_limit` guard (same ID + same key) to
  avoid double-release when general and specific point to the same limit with
  identical keys.
- `time_until_next_allowed()` reports the maximum delay across both dimensions.
  When `sequential_blocked == true`, the delay reflects `Duration::max()` because a
  sequential in-flight request is blocking. Callers must wait for
  `release_request()` rather than `sleep_for(duration)`.
- Do not add blocking drains or `close_and_wait()` to rate-limit callbacks;
  the model is cooperative: callbacks run on `NetworkWorker` and must return
  quickly.

For general thread-safety rules — callback/mutex ordering, worker blocking,
exception safety, and shutdown invariants — see `guides/concurrency.md`.

## Extension Recipes

### Add a New Rate-Limit Dimension

Use this when a request needs a third limit axis (for example, a per-host
limit alongside per-endpoint and global limits).

1. Add a new `HttpRateLimitHandlePtr` field to `HttpRequest`.
2. Add a matching `std::string` key field.
3. Thread the new handle + key through `HttpClient` builder methods and into
   `HttpRequestManager::process_pending_requests()`.
4. Update the `allow_request` / `release_request` calls in
   `process_pending_requests()` to include the third dimension.
   If the three handles might share an ID + key, add a `same_limit` guard for
   each pair to prevent double-commit / double-release.

### Tune GC Frequency

The periodic sweep triggers every 64 `allow_request()` calls via
`m_gc_counter`. If profiling shows key-map bloat, adjust the bitmask in
`HttpRateLimiter::allow_request()` (search for `m_gc_counter & 63`).
Keep it a power-of-two minus one so the branch is cheap.

### Add Cooldown Key Layer

If you need per-request cooldown keys that are not part of the rate-limit ID
system (for example, a backoff key after a 429 response), consider adding a
separate cooldown map inside `HttpRateLimiter` rather than overloading
`KeyState`. Document the new map in this file.

# Concurrency and Thread-Safety

Use this file when changing callback dispatch, mutex usage, shutdown/cancellation
logic, or any code that runs on `core::NetworkWorker`.

## Execution Model

- `core::NetworkWorker` is a single background thread.
- All HTTP and WebSocket manager `process()` methods are called from this thread.
- User callbacks (`on_complete`, `on_error`, `on_stream_chunk`, `on_stream_complete`)
  run on the `NetworkWorker` thread.

## Invariants

### Do Not Block the Worker

- Never perform heavy computation, synchronous I/O, or blocking waits inside
  `NetworkWorker::process()`, manager `process()` methods, or user callbacks.
- Keep callback bodies short; offload heavy work to another thread if needed.

### Mutex and Callback Ordering

- Do not call user callbacks while holding internal mutexes.
- Release locks before invoking `on_complete`, `on_error`, or streaming callbacks.
- This prevents deadlock when user code re-enters the library (e.g., chaining a
  new request inside a completion handler).

### Exception Safety in Callbacks

- Callbacks may throw. HTTP response and completion callback boundaries catch exceptions,
  dispatch them through the `NetworkWorker` error path, and continue processing.
- RAII guards inside `process()` must survive an exception without leaking state.

### Cancellation and Shutdown

- Cancellation must prevent new requests and complete or cancel existing ones safely.
- A cancelled request must release acquired rate-limit tokens exactly once.
- Completion must be idempotent: releasing an already-released token is a no-op.

### Rate-Limit Token Lifecycle

- `in_flight_token` is assigned at enqueue and committed in `allow_request()`.
- `release_request()` must run exactly once per successful commit.
- Retry logic must preserve the release callback across temporary denials;
  see `guides/rate-limiting-and-streaming.md` for the exact bug and fix.

### Handle Destructors

- `HttpRateLimitHandle` destructor may invoke `remove_limit_internal()`, which
  locks `m_mutex`. Therefore the destructor must never be called while already
  holding that mutex.

## Cross-References

- `guides/rate-limiting-and-streaming.md` — partitioned limits, sequential mode,
  token GC, and the retry-callback bug.
- `guides/codebase-orientation.md` — `NetworkWorker` design, layer rules, and
  safety invariants for public APIs.

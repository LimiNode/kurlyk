# Optional HTTP server foundation

`KURLYK_HTTP_SERVER_SUPPORT=ON` provides `kurlyk::http_server` and the
standalone `<kurlyk/http_server.hpp>` entry point. The option defaults to OFF;
`kurlyk.hpp` does not include the server, and the server target does not link
`HttpClient`, `WebSocketClient`, or `NetworkWorker`.

This foundation is plain HTTP for loopback/private deployment. Public HTTPS,
TLS configuration, Telegram authentication and acknowledgements belong to
separate adapters/deployment work. It does not register webhooks, parse
Telegram JSON, persist an inbox, or promise crash durability.

## Module boundaries

`<kurlyk/http_server.hpp>` remains the public aggregate and selects the default
SimpleWeb implementation at compile time. `<kurlyk/http/server/data.hpp>` is a
separate, self-contained public contract for `HttpServerConfig`,
`HttpServerRequest`, `HttpServerResponse`, and `HttpServerHandler`. It requires
only the standard library: a provider-specific request handler can use it
without including SimpleWeb, Asio, or the outbound HTTP client.
`HttpServer::Handler` remains an alias for the same handler type.

Ownership is divided as follows:

| Owner | Responsibility |
|---|---|
| `http/server/HttpServer.hpp` | Public listener facade and its documented lifecycle operations. |
| `http/server/detail/BasicHttpServer.hpp` | Exact route table, handler exceptions, one-shot flags, stop admission, and state retained by the caller-owned runner. |
| `http/server/detail/response_policy.hpp` | Status/header/body validation and HEAD/204/205/304 body/length policy, without vendor helpers. |
| `http/server/backend/SimpleWeb/SimpleWebHttpServerAdapter.hpp` | Backend request conversion, framing admission, connection bounds, I/O execution and drain, and serialization of the common response policy result. |
| `http/server/backend/SimpleWeb/HttpServerSocket.hpp` | Cumulative receive-time wire-byte budget before backend buffering. |
| `http/server/backend/SimpleWeb/headers.hpp` | Source-tree versus installed vendor-header layout bridge. |

The internal `BasicHttpServer<Backend>` contract is deliberately not a public
customization API. A backend is constructed from configuration, installs a
method dispatch callback, binds synchronously, runs serially until stopped and
drained, and offers a nonthrowing stop request. Dispatch receives an owned
request and fills an owned response; `false` means abort without writing a
response, not a successful empty response. The common state is the sole owner
of routing and lifecycle flags; adapters do not repeat that state machine.
A small fake backend tests this contract without vendor include paths or
network libraries. There is no public virtual server hierarchy or runtime
backend factory.

This remains a header-only implementation: the complete listener aggregate
necessarily includes its selected backend. Splitting headers is not binary
isolation. Only the data/handler contract and internal common state/policy can
be compiled without backend headers. Internal headers are implementation
details, even though their definitions must accompany installed public headers.

The installed bridge is generated at the same backend-owned path and chooses
flat parent-provided headers or the installed `simple-web-server/` layout.
The vendor pin and flat dependency-resolution order are unchanged.

## Implementation status and webhook boundary

The bounded, synchronous HTTP listener and its SimpleWeb adapter are
implemented. Telegram webhook ingestion is not implemented here. A future
Telegram-specific handler belongs above `HttpServerHandler`; it must not own
the listener or turn the generic HTTP DTO into a Telegram/JSON DTO. Request
body bytes are preserved after HTTP framing, without JSON parsing or
reserialization, and duplicate headers remain available for authentication
checks. Client request types remain separate from server request types.

A future webhook contract must state whether a successful HTTP response means
completed processing, durable inbox admission, or only volatile admission.
Returning a synchronous `2xx` alone proves none of those storage properties.
Deferred durable admission would require a separate, explicit completion
contract covering exactly-once response completion and lifetime on timeout,
disconnect, and stop; this structural split does not add asynchronous replies.

## Backend framing requirement

The pinned backend is the [LimiNode Simple-Web-Server corrective](https://github.com/LimiNode/Simple-Web-Server/pull/1)
at `f74866e83ae103cc6b40bb9f9f07d30e8ac09c28`, based on upstream
`546895a93a29062bb178367b46c7afb72da9881e`. It validates the complete chunk-size
token and extensions, checks every chunk-data CRLF across fragmented reads,
and rejects chunk sizes exceeding remaining buffer capacity without overflow.
The previous upstream pin accepted malformed chunk sizes and delimiters; a
receive-byte limit alone does not validate HTTP framing.

Only an empty trailer section is supported: after the zero-size chunk, the
backend requires the final CRLF. Nonempty trailers and malformed/truncated
terminators are rejected before an application handler is called. Valid token,
quoted, and escaped chunk extensions remain supported within configured bounds.

A custom `simple_web_server` parent target or
`KURLYK_SIMPLE_WEB_SERVER_SOURCE_DIR` provider must include equivalent framing
corrections. CMake cannot automatically determine the version or semantic
behavior behind an arbitrary parent-provided target; reusing that target is not
proof that its parser is corrected. Run the server conformance checks against
the chosen provider before accepting it.

## Ownership and lifecycle

1. Construct `HttpServer` with finite positive bounds and register exact
   method/path handlers. No routing expressions or implicit path decoding.
2. `start()` binds synchronously and returns the actual listening port. It may
   throw on bind/configuration failure. Each instance is one-shot, including a
   failed start; create a fresh instance for another lifecycle.
3. Call `run()` exactly once, normally on an application-owned thread. Handlers
   execute serially on this runner. Requests own their strings and headers;
   callbacks may retain or move them. Responses own their body and headers.
4. `request_stop()` is thread-safe, idempotent, and safe inside a handler. It
   stops the I/O context, not the executing callback. The callback must return.
   The runner then aborts connections and drains canceled network operations.
5. Wait for `run()` to return (join the application's runner) before declaring
   quiescence. The facade may be destroyed in a handler: `run()` retains internal
   state until completion and never joins its own thread.

Configuration, `start()`, and invocation of `run()` are owner-serialized.
Only `request_stop()` may race with `run()`. The application must not race
facade destruction with a call through another non-owning facade pointer.
Starting again or running twice throws `logic_error`.

Stop is abortive, not graceful delivery: an in-flight response may be lost.
A handler exception or invalid/oversized returned response becomes an empty
500 response when the connection remains writable. Backend exceptions from
`run()` abort the listener and propagate to the application's runner boundary.
Application handlers must not block indefinitely; network deadlines cannot
preempt arbitrary user code.

## Bounds and overload

`max_request_bytes` is a cumulative **wire-byte** budget per connection,
including the request line, headers, body, and chunk framing. The socket's
`async_read_some()` limits each receive before data enters a backend buffer.
Simple-Web-Server still owns HTTP parsing and its finite
`max_request_streambuf_size` limit; there is no second HTTP parser or body
accumulator. A large declared body can be rejected before reading it; fragmented
or chunked bodies are bounded by actual received bytes regardless of declared
sizes. The total limit is deliberately stricter than a body-only limit.
An empty asynchronous read completes successfully with zero bytes, including
at the exact budget boundary; it consumes no transport data. A nonempty read
after the budget is exhausted fails asynchronously without receiving bytes.

`max_connections` bounds admitted receiving/handling/writing connections. One
additional accept socket is allowed. At capacity, accepted connections close
without allocating a request buffer or invoking application code. The operating
system's bounded listen backlog is not an application work queue. There is no
unbounded dispatch queue and at most one application handler executes at once.

The header deadline starts at connection admission. The content deadline spans
body receipt through response transmission after the headers arrive. Timeouts,
overload, malformed requests, disconnects, and request limits may close without
an HTTP status; callers must tolerate transport failure. Each connection serves
one request and responses force `Connection: close`; keep-alive, upgrade,
pipelining, deferred responses, response streaming, and server-sent events are
not supported in this foundation.

Returned application response bodies and headers have independent finite
validation limits. The callback is trusted code: these checks cannot prevent
the callback itself from allocating an oversized object before returning it.
Framing headers are generated by the listener; application-supplied
Content-Length, Transfer-Encoding, Connection, invalid names, or invalid value
bytes are rejected. Values permit HTAB, SP, visible ASCII, and bytes 128–255;
other control bytes (including NUL/CR/LF) and DEL are rejected. Invalid output
becomes an empty 500 response. Duplicate or ambiguous Content-Length/Transfer-Encoding framing
does not reach application handlers. Unsupported methods may close without a
response unless explicitly registered.

Statuses 204, 205, and 304 never carry a response body. The listener omits
Content-Length for 204 and 304, and emits Content-Length: 0 for 205. The
response API has no separate representation length for 304. HEAD suppresses
the body while retaining the corresponding GET body length unless these
status-specific rules require an omitted or zero length.

## Local verification

Use pinned local source providers; configuration does not need downloads:

```sh
cmake -S tests/http_server -B build/server-cpp11-debug -G Ninja \
  -DCMAKE_CXX_STANDARD=11 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKURLYK_SIMPLE_WEB_SERVER_SOURCE_DIR=/path/to/Simple-Web-Server \
  -DKURLYK_ASIO_SOURCE_DIR=/path/to/asio
cmake --build build/server-cpp11-debug
ctest --test-dir build/server-cpp11-debug --output-on-failure
```

Repeat with C++17/Release. The loopback tests cover declared/fragmented/chunked
oversize requests, oversized headers, malformed chunk-size/extension/CRLF
framing, overflow, retained owned
requests, response bounds, disconnects, read/write timeouts, overload, in-flight stop,
stop before run, destruction from a callback, and fresh repeated lifecycles.
Regressions also cover the exact wire budget boundary, posted empty and denied
reads that consume no bytes, GET/HEAD status-specific body and length rules,
and every possible response header-value byte (including NUL and obs-text).
Chunk regressions assert zero application dispatch for malformed input and
verify that a valid request still succeeds after each rejection. Loopback
fragmented writes complement the backend's deterministic scripted-read tests;
TCP packet boundaries are not treated as parser-read boundaries.
CTest sets an independent 30-second liveness timeout.

The same configuration also builds a standard-library-only first-include data
test and a fake-backend test. They do not link `kurlyk::http_server` or inherit
its vendor include paths. The fake checks route replacement and exact matching,
handler exceptions, one-shot failure paths, stop rejection, retained runner
state after destruction in a handler, and common response policy boundaries.
All checks use active `CHECK` assertions in Release as well as Debug. The
independent installed consumer repeats the data, fake, and real backend tests
and compiles a combined client/server consumer against the installed exports.
CI also repeats that consumer with
`KURLYK_TEST_SIMPLE_WEB_SERVER_SOURCE_DIR=/path/to/Simple-Web-Server`, providing
a flat `simple_web_server` parent target before package discovery. This covers
both branches of the generated installed header-layout bridge.

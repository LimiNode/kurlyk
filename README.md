# kurlyk
![logo](docs/logo-mini.png)

**C++ library for easy networking**

[![MIT License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue)
![C++ Standard](https://img.shields.io/badge/C++-11--17-orange)
![CI Windows](https://img.shields.io/github/actions/workflow/status/LimiNode/kurlyk/ci.yml?branch=main&label=Windows&logo=windows)
![CI Linux](https://img.shields.io/github/actions/workflow/status/LimiNode/kurlyk/ci.yml?branch=main&label=Linux&logo=linux)
![CI macOS](https://img.shields.io/github/actions/workflow/status/LimiNode/kurlyk/ci.yml?branch=main&label=macOS&logo=apple)

[README на русском](README-RU.md)

## What it is

**kurlyk** is yet another library implementing HTTP and WebSocket clients for C++. Built as a wrapper around `curl` and `Simple-WebSocket-Server`, it provides a simplified interface for working with HTTP and WebSocket in C++ applications. It supports asynchronous HTTP requests with rate limiting and retry attempts, as well as WebSocket connections.

If, for some reason, other libraries such as *easyhttp-cpp, curl_request, curlpp-async, curlwrapper, curl-Easy-cpp, curlpp11, easycurl, curl-cpp-wrapper…* did not work for you, `kurlyk` may be worth trying.

## Features

- Asynchronous HTTP and WebSocket requests.
- Background worker or synchronous processing via `kurlyk::process()`.
- HTTP callback API and `std::future` API.
- Rate limits (partitioned by key), retry, proxy, custom headers, cookies, and timeouts.
- HTTP/HTTPS proxy checks using no-body requests and curl timing metrics.
- Streaming HTTP responses with a callback for each chunk.
- WebSocket events, message sending, and automatic reconnection.
- Bounded admission/backpressure for the HTTP pending queue and WebSocket send queue.
- Bearer token and API-key auth providers (`BearerTokenAuthProvider`, `ApiKeyAuthProvider`).
- OAuth2 Authorization Code + PKCE client (`OAuthPkceClient`) using standalone HTTP helpers.
- Support for C++11 and newer toolchains.

## Quick start

### Minimal HTTP GET

```cpp
#include <kurlyk.hpp>
#include <iostream>

int main() {
    kurlyk::HttpClient client("https://httpbin.org");

    auto response = client.get("/ip", kurlyk::QueryParams(), kurlyk::Headers()).get();

    if (response && response->ready) {
        std::cout << response->content << std::endl;
    }

    return 0;
}
```

Use `kurlyk::init()` / `kurlyk::deinit()` when explicit lifecycle management is required.

### Minimal WebSocket echo

```cpp
#include <kurlyk.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    kurlyk::WebSocketClient client("wss://echo-websocket.fly.dev/");

    client.on_event([](std::unique_ptr<kurlyk::WebSocketEventData> event) {
        if (event->event_type == kurlyk::WebSocketEventType::WS_OPEN) {
            event->sender->send_message("Hello");
        }

        if (event->event_type == kurlyk::WebSocketEventType::WS_MESSAGE) {
            std::cout << event->message << std::endl;
        }
    });

    client.connect();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    client.disconnect_and_wait();

    return 0;
}
```

## Building examples

Examples are located in the `examples` folder. Build all repository targets with CMake:

```powershell
cmake -S . -B build-examples -DKURLYK_BUILD_EXAMPLES=ON
cmake --build build-examples --config Release
```

For MinGW, you can select the generator and compilers explicitly:

```powershell
cmake -S . -B build-examples-mingw -G "MinGW Makefiles" `
    -DCMAKE_C_COMPILER=gcc `
    -DCMAKE_CXX_COMPILER=g++ `
    -DKURLYK_BUILD_EXAMPLES=ON
cmake --build build-examples-mingw --config Release
```

For MinGW, dependencies are available in the repository as git submodules in the `external` folder, and the fallback CMake options below can build missing dependencies automatically.

## Basic HTTP usage

The HTTP client can be used through callbacks, futures, or low-level helpers. With auto-init enabled by default, you can create `HttpClient` without manual `kurlyk::init()` / `kurlyk::deinit()` calls; use the lifecycle functions when synchronous or explicit worker control is required.

### Shared helper for examples

```cpp
#include <kurlyk.hpp>
#include <iostream>

void print_response(const kurlyk::HttpResponsePtr& response) {
    if (!response) {
        KURLYK_PRINT << "response is null" << std::endl;
        return;
    }

    KURLYK_PRINT
        << "ready: " << std::boolalpha << response->ready << std::endl
        << "response: " << response->content << std::endl
        << "error_code: " << response->error_code.message() << std::endl
        << "status_code: " << response->status_code << std::endl
        << "----------------------------------------" << std::endl;
}
```

### Callback API

Callback overloads of `get(...)`, `post(...)`, and `request(...)` return `bool`: `true` when the request is accepted into the queue and `false` when it is rejected during admission. The callback itself receives `kurlyk::HttpResponsePtr` and is not limited to the final response: in streaming mode it is called for each chunk with `stream_chunk == true`, and during retry it may receive an intermediate non-ready response for a failed attempt. The final result is identified by `response && response->ready`.

```cpp
int main() {
    kurlyk::HttpClient client("https://httpbin.org");

    client.get("/ip", kurlyk::QueryParams(), kurlyk::Headers(),
        [](const kurlyk::HttpResponsePtr response) {
            print_response(response);
        });

    client.post("/post", kurlyk::QueryParams(), {{"Content-Type", "application/json"}},
        "{\"text\":\"Sample POST Content\"}",
        [](const kurlyk::HttpResponsePtr response) {
            print_response(response);
        });

    KURLYK_PRINT << "Press Enter to exit..." << std::endl;
    std::cin.get();
    return 0;
}
```

### Future API

If a request is rejected before it enters the pending queue, the future becomes ready immediately and returns an `HttpResponse` with `error_code = QueueLimitExceeded` or `ShuttingDown`.

```cpp
int main() {
    kurlyk::HttpClient client("https://httpbin.org");

    auto future_response = client.get("/get", kurlyk::QueryParams{{"param", "value"}}, kurlyk::Headers());
    print_response(future_response.get());

    auto future_post = client.post("/post", kurlyk::QueryParams(),
        kurlyk::Headers{{"Header", "Value"}}, "Async POST Content");
    print_response(future_post.get());

    return 0;
}
```

### Proxy

```cpp
int main() {
    kurlyk::HttpClient client("https://httpbin.org");

    client.set_proxy("127.0.0.1", 8080, "username", "password", kurlyk::ProxyType::PROXY_HTTP);

    client.get("/ip", kurlyk::QueryParams(), kurlyk::Headers(),
        [](const kurlyk::HttpResponsePtr response) {
            print_response(response);
        });

    KURLYK_PRINT << "Press Enter to exit..." << std::endl;
    std::cin.get();
    return 0;
}
```

### Proxy checks

`ProxyChecker` checks a passive `ProxyConfig` with one asynchronous `HEAD` request through the proxy. The test URL is configurable and defaults to `https://example.com/`; use an endpoint you control when reproducible availability checks matter. Redirect following and proxy tunneling are disabled by default so that a probe measures one endpoint through a regular HTTP proxy; enable `options.proxy_tunnel` explicitly when a CONNECT tunnel is required. For an HTTPS endpoint signed by a private CA, set `options.ca_file` to the CA bundle path. The checker disables retries and does not download the response body, matching the lightweight availability probe used for host discovery.

```cpp
int main() {
    kurlyk::ProxyConfig proxy;
    proxy.set_proxy("127.0.0.1", 8080, kurlyk::ProxyType::PROXY_HTTP);
    proxy.set_proxy_auth("username", "password");
    proxy.use = true;

    kurlyk::ProxyCheckOptions options;
    options.test_url = "https://service.example/health";

    kurlyk::ProxyCheckResult result =
        kurlyk::check_proxy(proxy, options).get();

    std::cout << "HTTP: " << result.http_ok << '\n'
              << "HTTPS: " << result.https_ok << '\n'
              << "connect: " << result.connect_latency.count() << " ms\n"
              << "TLS/CONNECT: " << result.tls_latency.count() << " ms\n"
              << "TTFB: " << result.ttfb.count() << " ms\n"
              << "total: " << result.total_latency.count() << " ms\n";
}
```

`connect_latency` is the elapsed curl time until the proxy TCP connection is established and includes name resolution. `tls_latency` is the interval after TCP connect until TLS setup completes; for HTTPS through an HTTP proxy it also includes CONNECT negotiation. TTFB and total latency are measured from the beginning of the request. Timeout values are rounded up to whole seconds by the current HTTP transport. `reachable` means that an HTTP response was received through the proxy; it is not an ICMP ping. Calling `check(...)` explicitly checks the supplied DTO, so `ProxyConfig::use` is not consulted.

### Low-level helpers

Low-level helpers are useful when you need the ID of a concrete request or direct access to the standalone HTTP API.

```cpp
int main() {
    const uint64_t request_id = kurlyk::http_get(
        "https://httpbin.org/ip",
        kurlyk::QueryParams(),
        kurlyk::Headers(),
        [](const kurlyk::HttpResponsePtr response) {
            print_response(response);
        });

    KURLYK_PRINT << "Request id: " << request_id << std::endl;
    KURLYK_PRINT << "Press Enter to exit..." << std::endl;
    std::cin.get();

    kurlyk::cancel_request_by_id(request_id).wait();
    return 0;
}
```

```cpp
int main() {
    auto result = kurlyk::http_get(
        "https://httpbin.org/ip",
        kurlyk::QueryParams(),
        kurlyk::Headers());

    KURLYK_PRINT << "Request id: " << result.first << std::endl;
    print_response(result.second.get());

    return 0;
}
```

## Basic WebSocket usage

`WebSocketClient` connects to a server, reports events through `on_event(...)`, and lets you send messages through the sender from an event or through the client itself. For a simple scenario, handle `WS_OPEN`, `WS_MESSAGE`, `WS_CLOSE`, and `WS_ERROR`.

### Connecting and handling events

```cpp
#include <kurlyk.hpp>
#include <thread>
#include <chrono>

int main() {
    kurlyk::WebSocketClient client("wss://echo-websocket.fly.dev/");

    client.on_event([](std::unique_ptr<kurlyk::WebSocketEventData> event) {
        switch (event->event_type) {
            case kurlyk::WebSocketEventType::WS_OPEN:
                KURLYK_PRINT << "Connection established" << std::endl;
                event->sender->send_message("Hello, WebSocket!");
                break;

            case kurlyk::WebSocketEventType::WS_MESSAGE:
                KURLYK_PRINT << "Message received: " << event->message << std::endl;
                break;

            case kurlyk::WebSocketEventType::WS_CLOSE:
                KURLYK_PRINT << "Connection closed: " << event->message
                             << "; Status code: " << event->status_code << std::endl;
                break;

            case kurlyk::WebSocketEventType::WS_ERROR:
                KURLYK_PRINT << "Error: " << event->error_code.message() << std::endl;
                break;
        }
    });

    client.connect();
    std::this_thread::sleep_for(std::chrono::seconds(10));
    client.disconnect_and_wait();
    return 0;
}
```

### Sending messages

`send_message(...)` returns `bool` and fits simple code. `submit_message(...)` returns `SubmitResult` and lets you distinguish successful queue admission from an admission reject.

```cpp
client.send_message("Hello");

kurlyk::SubmitResult submit = client.submit_message(
    "Hello with admission check",
    0,
    [](const std::error_code& ec) {
        if (ec) {
            std::cout << "Send failed: " << ec.message() << std::endl;
        }
    });

if (!submit) {
    std::cout << "WebSocket submit rejected: " << submit.error_code.message() << std::endl;
}
```

### Limiting the send queue

To protect the producer, you can limit the size of the outbound WebSocket send/close queue. A value of `0` means the queue is unbounded.

```cpp
kurlyk::WebSocketClient client("wss://echo-websocket.fly.dev/");
client.set_max_send_queue_size(32);
```

## Initialization

Auto-initialization is available by default in C++11 and newer, so simple examples can create `HttpClient` or `WebSocketClient` immediately. Disable auto init and call `init()` / `deinit()` when explicit lifecycle control is required.

```cpp
#define KURLYK_AUTO_INIT 0
#include <kurlyk.hpp>

int main() {
    kurlyk::init(true);
    kurlyk::HttpClient client("https://httpbin.org");
    kurlyk::deinit();
    return 0;
}
```

`kurlyk::deinit()` is the normal cleanup call for both asynchronous `init(true)` and synchronous `init(false)` modes. `kurlyk::shutdown()` remains available for explicit manager cleanup/reset scenarios, but normal manual lifetime management should use the `init()` / `deinit()` pair.

## Advanced features

### Rate limits

A rate limit controls how quickly HTTP requests are released to the network backend. For a simple per-client speed limit, use `set_rate_limit_rps(...)` or `set_rate_limit_rpm(...)`.

```cpp
kurlyk::HttpClient client("https://api.example.com");
client.set_rate_limit_rps(1);
```

To share one limit between multiple clients, use `HttpRateLimitHandlePtr`:

```cpp
auto limit = kurlyk::create_rate_limit_rps(5);

kurlyk::HttpClient client_a("https://api.example.com");
kurlyk::HttpClient client_b("https://api.example.com");

client_a.set_rate_limit_handle(limit);
client_b.set_rate_limit_handle(limit);
```

`HttpRateLimitHandlePtr` keeps the physical limit data alive while at least one handle copy exists. Pending, active, and retrying requests copy their assigned handles, so `remove_limit(id)` or `remove_limit(handle)` only releases the manager-owned reference and does not invalidate already queued requests.

Prefer handle-based APIs for new code:

- `HttpClient::set_rate_limit_handle(...)`
- `HttpClient::assign_rate_limit_handle(...)`
- per-request overloads that accept `HttpRateLimitHandlePtr`

ID-based APIs such as `set_rate_limit_id(...)` and per-request `long specific_rate_limit_id` overloads remain legacy lookup helpers. If the manager-owned handle has already been removed, lookup by ID returns an empty handle and the request is submitted without that additional specific limit.

### HTTP request cancellation

HTTP requests can be cancelled by the ID of one concrete request or by the ID of a related request group. `HttpClient` assigns one `group_id` to all requests created by that client, so `HttpClient::cancel_requests()` cancels the client's request group.

```cpp
uint64_t request_id = kurlyk::http_get(
    "https://httpbin.org/delay/5",
    kurlyk::QueryParams(),
    kurlyk::Headers(),
    [](kurlyk::HttpResponsePtr response) {
        print_response(response);
    });

kurlyk::cancel_request_by_id(request_id).wait();
```

HTTP requests have two identifiers:

- `request_id` is the ID of one concrete request and is used by `cancel_request_by_id(...)`.
- `group_id` is the ID of a related request group and is used by `cancel_requests_by_group_id(...)`.

For manually constructed low-level requests, set `group_id` explicitly if you want to cancel a group:

```cpp
const uint64_t group_id = kurlyk::generate_group_id();

std::unique_ptr<kurlyk::HttpRequest> request(new kurlyk::HttpRequest());
request->request_id = kurlyk::generate_request_id();
request->group_id = group_id;
request->method = "GET";
request->set_url("https://httpbin.org", "/delay/5");

kurlyk::submit_http_request(std::move(request), [](kurlyk::HttpResponsePtr response) {
    print_response(response);
});

kurlyk::cancel_requests_by_group_id(group_id).wait();
```

### Backpressure

A rate limit slows request dispatch. Backpressure limits how many requests or messages are accepted into a queue at all.

```cpp
kurlyk::set_max_pending_requests(64);

std::unique_ptr<kurlyk::HttpRequest> request(new kurlyk::HttpRequest());
request->request_id = kurlyk::generate_request_id();
request->method = "GET";
request->set_url("https://httpbin.org/get", kurlyk::QueryParams());

kurlyk::SubmitResult submit = kurlyk::submit_http_request(
    std::move(request),
    [](kurlyk::HttpResponsePtr response) {
        if (response && response->error_code) {
            std::cout << "HTTP runtime error: " << response->error_code.message() << std::endl;
        }
    });

if (!submit) {
    std::cout << "HTTP submit rejected: " << submit.error_code.message() << std::endl;
}
```

Current bounded queues:

- HTTP uses a global pending-queue limit configured with `kurlyk::set_max_pending_requests(...)`.
- WebSocket uses a per-client outbound send/close queue limit configured with `WebSocketClient::set_max_send_queue_size(...)`.
- A value of `0` means an unbounded queue.

Public admission helpers:

- `kurlyk::SubmitResult`
- `kurlyk::submit_http_request(...)`
- `IWebSocketSender::submit_message(...)`
- `IWebSocketSender::submit_close(...)`
- `WebSocketClient::submit_message(...)`
- `WebSocketClient::submit_close(...)`

Compatibility notes:

- older `bool` APIs remain available as wrappers;
- HTTP future overloads become ready immediately on admission reject and return an `HttpResponse` with `error_code` instead of throwing `runtime_error`;
- this pass does not bound `NetworkWorker`, WebSocket FSM queues, or accumulated event queues.

### Streaming

Streaming lets you receive an HTTP body in parts instead of waiting for the whole response to finish. The callback is invoked for each chunk with `response->stream_chunk == true` and `response->ready == false`, and the last callback is the normal completed response with `ready == true`.

```cpp
int main() {
    kurlyk::http_post(
        "https://api.example.com/v1/chat/completions",
        kurlyk::QueryParams(),
        kurlyk::Headers{{"Content-Type", "application/json"}},
        R"({"stream":true})",
        true,
        [](kurlyk::HttpResponsePtr response) {
            if (!response) return;

            if (response->stream_chunk) {
                std::cout << response->content;
                return;
            }

            if (response->error_code) {
                std::cerr << response->error_code.message() << std::endl;
            }
        });

    std::cin.get();
    return 0;
}
```

For requests created through `HttpClient`, enable streaming on the client:

```cpp
kurlyk::HttpClient client("http://httpbin.org");
client.set_streaming(true);
```

Use `stream_chunk` first to classify body chunk callbacks. A chunk callback is not a success marker; the final `ready` response remains the authoritative transfer result. If at least one streaming chunk was emitted, kurlyk does not retry that transfer automatically, because the caller may already have forwarded bytes to a downstream client.

### Retry

Retry repeats a failed HTTP request after a configured delay. It is useful for temporary network errors and unstable upstream APIs.

```cpp
kurlyk::HttpClient client("https://httpbin.org");
client.set_retry_attempts(3, 1000);
```

The number of performed attempts is available in `HttpResponse::retry_attempt`. For streaming requests, automatic retry is not performed after at least one body chunk has been emitted.

### Proxy

Proxy settings can be configured on `HttpClient`, and then they apply to requests created by that client.

```cpp
kurlyk::HttpClient client("https://httpbin.org");
client.set_proxy("127.0.0.1", 8080, "username", "password", kurlyk::ProxyType::PROXY_HTTP);
```

## Installation and dependencies

### Supported toolchains

- **MSVC**
- **MinGW (GCC)**
- **AppleClang (macOS)**

The supported language baseline is **C++11**. Examples and integration tests use **C++17** where required by their source code. MSVC, MinGW, and AppleClang are covered by CI; macOS dependencies are provided by the system or Homebrew.

### Adding kurlyk

Add the path to the library headers to your project:

```text
kurlyk/include
```

**kurlyk** is a header-only library, so including it with `#include <kurlyk.hpp>` is enough to start using it.

### Dependencies

To use **kurlyk** in a MinGW environment, you need these dependencies:

1. For WebSocket:
   - [Simple-WebSocket-Server](https://gitlab.com/eidheim/Simple-WebSocket-Server)
   - Boost.Asio or [standalone Asio](https://github.com/chriskohlhoff/asio/tree/master)
   - [OpenSSL](https://slproweb.com/products/Win32OpenSSL.html) (*LTS version Win64 OpenSSL v3.0.15*)

2. For HTTP:
   - [libcurl](https://curl.se/windows/)

All dependencies are also included as submodules in the `external` folder. Asio and Simple-WebSocket-Server are used from the checked-out submodules when available.

### OpenSSL

Add OpenSSL paths to the project, for example for version *3.4.0*:

```text
OpenSSL-Win64/include
OpenSSL-Win64/lib/VC/x64/MD
OpenSSL-Win64/bin
```

Link OpenSSL libraries from `lib/VC/x64/MD`:

```text
capi.lib
dasync.lib
libcrypto.lib
libssl.lib
openssl.lib
ossltest.lib
padlock.lib
```

### Asio

Add the path to asio:

```text
asio/asio/include
```

For standalone Asio, define `ASIO_STANDALONE` in project settings or before including `kurlyk.hpp`:

```cpp
#define ASIO_STANDALONE
#include <kurlyk.hpp>
```

For Boost.Asio, you do not need to define `ASIO_STANDALONE`.

### curl

Add paths for `curl`, for example for version *8.11.0*:

```text
curl-8.11.0_1-win64-mingw/bin
curl-8.11.0_1-win64-mingw/include
curl-8.11.0_1-win64-mingw/lib
```

Link the `curl` libraries from the `lib` folder:

```text
libcurl.a
libcurl.dll.a
```

### Simple-WebSocket-Server

Add the path to the library headers:

```text
Simple-WebSocket-Server
```

### Other dependencies

Also add these libraries to the linker:

```text
ws2_32
wsock32
crypt32
```

### Fallback dependencies

The library supports automatically downloading dependencies when they are missing. Fallback availability depends on the compiler and linkage type.

| Dependency | MinGW (Shared) | MinGW (Static) | MSVC (Shared) | MSVC (Static) | macOS (system/Homebrew) |
|------------|---------------|---------------|---------------|---------------|-------------------------|
| OpenSSL    | yes           | yes           | yes           | yes           | yes                     |
| curl       | yes           | yes           | yes           | no            | yes                     |

Asio and Simple-WebSocket-Server are header-only libraries and work for all listed build variants.

#### CMake fallback options

| Option | Description |
|--------|-------------|
| `KURLYK_USE_FALLBACK_OPENSSL` | Enables OpenSSL fallback. |
| `KURLYK_USE_FALLBACK_CURL` | Enables libcurl fallback. |
| `KURLYK_USE_FALLBACK_ASIO` | Enables Asio fallback. |
| `KURLYK_USE_FALLBACK_SIMPLE_WS_SERVER` | Enables Simple-WebSocket-Server fallback. |
| `KURLYK_OPENSSL_SHARED` | Loads OpenSSL as a shared library when fallback is enabled. |
| `KURLYK_CURL_SHARED` | Loads libcurl as a shared library when fallback is enabled. |
| `KURLYK_BUILD_EXAMPLES` | Builds all targets from the `examples/` directory. |

## Configuration macros

Define these macros before including `kurlyk.hpp` to configure the library:

| Macro | Default | Description |
|-------|---------|-------------|
| `KURLYK_AUTO_INIT` | `1` | Automatically registers managers during static initialization. |
| `KURLYK_AUTO_INIT_USE_ASYNC` | `1` | Starts the network thread in the background when auto init is enabled. Set to `0` for manual processing. |
| `KURLYK_HTTP_SUPPORT` | `1` | Enables or disables the HTTP subsystem. |
| `KURLYK_WEBSOCKET_SUPPORT` | `1` | Enables or disables the WebSocket subsystem. |
| `KURLYK_AUTH_SUPPORT` | `1` | Enables authentication providers (`BearerTokenAuthProvider`, `ApiKeyAuthProvider`). |
| `KURLYK_OAUTH_SUPPORT` | `KURLYK_AUTH_SUPPORT` | Enables the OAuth2 PKCE client (`OAuthPkceClient`). Requires `KURLYK_AUTH_SUPPORT=1`. |
| `KURLYK_JSON_SUPPORT` | `0` | Enables nlohmann::json include and JSON-aware types, plus enum-to-JSON helpers in `type_utils.hpp`. |

## Repository layout

| Path | Purpose |
|------|---------|
| `include/` | Public header-only library. |
| `include/kurlyk/core` | Core infrastructure with `NetworkWorker` and base interfaces. |
| `include/kurlyk/http` | HTTP client and request management. |
| `include/kurlyk/websocket` | WebSocket client and connection management. |
| `include/kurlyk/types` | Shared enums, cookie, proxy config, and helpers. |
| `include/kurlyk/utils` | Encoding, URL, HTTP, path, and error helpers. |
| `tests/integration` | Cross-platform local HTTP/WebSocket integration build checks. |
| `tests/odr` | Header-only ODR checks. |
| `tests/smoke` | Portable header smoke checks. |
| `examples/` | Usage examples. |

## AI agent tooling

The repository includes agent configuration and orchestration metadata for AI coding tools (e.g., Claude Code with oh-my-claudecode). The following MCP servers and plugins are recommended for working with the codebase:

| Category | MCP server / plugin | Purpose |
|----------|---------------------|---------|
| Document lookup | context7 | SDK/API docs resolution before web search |
| Web search | DDG Search (no key) | Primary web search fallback |
| Web search | Tavily | Deep web search and extraction |
| Web content | Fetch | Markdown/JSON/TXT fetch for known URLs |
| Browser automation | Playwright | UI automation and screenshot testing |
| Repo operations | GitHub | Issues, PRs, and repository file operations |
| Code intelligence | Codebase Memory | Graph-based code discovery and call chains |
| Large output | Context-Mode | Batch execution, indexing, and analysis |
| Structural code | AST grep (OMC plugin) | Structural search and replace |
| Runtime | Python REPL (OMC plugin) | In-session script execution |
| Diagnostics | LSP | Symbols, definitions, and diagnostics |

For the full tool priority chain and fallback rules, see [`.claude/rules/tool-priority.md`](.claude/rules/tool-priority.md).

## Tests

Run the Windows integration suite:

```powershell
powershell -ExecutionPolicy Bypass -File tests/integration/run_integration_tests.ps1
```

Run the ODR suite:

```powershell
powershell -ExecutionPolicy Bypass -File tests/odr/run_odr_tests.ps1
```

Run the auth unit tests:

```bash
cmake -S tests/auth -B build-auth-tests -G Ninja \
    -DKURLYK_BUILD_EXAMPLES=OFF \
    -DKURLYK_USE_FALLBACK_ASIO=ON \
    -DKURLYK_USE_FALLBACK_SIMPLE_WS_SERVER=ON
cmake --build build-auth-tests
ctest --test-dir build-auth-tests
```

On macOS, install the system dependencies with Homebrew and point CMake at the
keg-only OpenSSL installation:

```bash
brew install ninja curl openssl@3
OSSL="$(brew --prefix openssl@3)"
CURL="$(brew --prefix curl)"
PATH="$CURL/bin:$PATH" cmake -S tests/integration -B build-macos -G Ninja \
    -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DOPENSSL_ROOT_DIR="$OSSL" -DCMAKE_PREFIX_PATH="$OSSL;$CURL" \
    -DKURLYK_USE_FALLBACK_ASIO=ON \
    -DKURLYK_USE_FALLBACK_SIMPLE_WS_SERVER=ON
cmake --build build-macos
ctest --test-dir build-macos --output-on-failure
```

Build the portable smoke test manually:

```bash
c++ tests/smoke/header_smoke.cpp -Iinclude -std=c++11 -o header_smoke
./header_smoke

c++ tests/smoke/header_smoke.cpp -Iinclude -std=c++17 -o header_smoke
./header_smoke

c++ tests/smoke/http_header_smoke.cpp -Iinclude -std=c++11 -o http_header_smoke
./http_header_smoke

c++ tests/smoke/proxy_config_smoke.cpp -Iinclude -std=c++11 -o proxy_config_smoke
./proxy_config_smoke

cmake -S tests/smoke -B build-full-cpp11-smoke -G Ninja \
    -DCMAKE_CXX_STANDARD=11 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DKURLYK_USE_STANDALONE_ASIO=ON \
    -DKURLYK_USE_FALLBACK_ASIO=ON \
    -DKURLYK_USE_FALLBACK_SIMPLE_WS_SERVER=ON
cmake --build build-full-cpp11-smoke
```

## Authentication helpers

`kurlyk` provides lightweight, header-only authentication helpers on top of the existing HTTP layer.

- **`BearerTokenAuthProvider`** — injects `Authorization: Bearer <token>`.
- **`ApiKeyAuthProvider`** — injects an API key as a custom header or as a query parameter (with automatic percent-encoding).
- **`OAuthPkceClient`** — builds authorization URLs and exchanges authorization codes for tokens using PKCE (RFC 7636). It uses the standalone `kurlyk::http_post` helper, so it does not require an `HttpClient` instance.

See the full guides for details:

- [OAuth2 / PKCE](guides/oauth.md)
- [Auth providers](guides/auth-providers.md)

## CI coverage

| Platform | Coverage |
|----------|----------|
| Windows | MinGW and MSVC integration builds with fallback dependencies, HTTP backpressure regression, and local WebSocket integration coverage. |
| Windows extras | ODR checks for singleton and auto-initialization headers. |
| Linux | C++11/C++17 header smoke, C++11 HTTP/proxy checks, full C++11 HTTP/WebSocket compile-only smoke, and C++17 integration examples. |
| macOS | C++11/C++17 header smoke, C++11 HTTP/proxy checks, full C++11 HTTP/WebSocket compile-only smoke, and C++17 integration tests. |

## Documentation

Generate Doxygen documentation:

```bash
doxygen Doxyfile
```

Published documentation: <https://newyaroslav.github.io/kurlyk/>.

## License

This library is distributed under the MIT license. See [LICENSE](LICENSE) for details.

## Support

If you have questions or issues while using the library, consult the documentation or ask a question in GitHub Issues.

In short, **kurlyk!**

![logo](docs/logo-mini-end.png)

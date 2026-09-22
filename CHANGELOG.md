# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

## [v1.1.0] - 2026-09-22

### Added
- Added lightweight HTTP authentication providers:
  - `BearerTokenAuthProvider` — injects `Authorization: Bearer <token>` header.
  - `ApiKeyAuthProvider` — injects an API key as a custom header or URL query parameter with automatic percent-encoding.
- Added `OAuthPkceClient` for OAuth2 Authorization Code + PKCE (RFC 7636) flow:
  - `build_authorization_url()`, `exchange_code()`, `refresh_access_token()`, `validate_state()`.
  - Uses standalone `kurlyk::http_post` / `kurlyk::http_request` helpers (no `HttpClient` coupling).
  - SHA-256 and secure random bytes via OpenSSL; Base64url helpers are provided
    by the public utility header `base64_url.hpp`.
  - Custom token parser fallback for non-JSON builds.
- Added `ITokenStorage` interface for caller-provided token persistence.
- Added `OAuthToken`, `OAuthConfig`, `AuthResult` data types with `AuthError` enum.
- Added `KURLYK_AUTH_SUPPORT`, `KURLYK_OAUTH_SUPPORT`, and `KURLYK_JSON_SUPPORT` feature macros.
- Added `tests/auth/` standalone test suite: Base64Url, PKCE, auth providers, URL construction, token parsing.
- Added auth examples: OpenRouter OAuth PKCE, Gemini API key (query), ChatGPT Bearer token.
- Added asynchronous `ProxyChecker` with HTTP/HTTPS no-body probes and
  curl timing metrics.
- Added relocatable CMake package installation and an overlay vcpkg port,
  including bundled Simple-WebSocket-Server license metadata.
- Added package consumers and C++11 install-tree checks for the public HTTP,
  proxy, PKCE, and Base64url headers.
- Added `guides/oauth.md` and `guides/auth-providers.md` documentation.

- Added `HttpClient::wait_requests()` to block until all callbacks for the client's request group are delivered.
- Added `HttpClient::wait_requests_for(timeout)` to block with a timeout; returns `false` on timeout.
- Added per-client in-flight admission cap via `HttpClient::set_max_in_flight()`, `max_in_flight()`, and `in_flight_requests()`; requests exceeding the cap are rejected with `QueueLimitExceeded`.
- Added `HttpRequestManager::wait_requests_by_group_id()` for lower-level group-waiter registration using callbacks.
- Added `HttpRequestManager::group_request_count()` and `has_requests_by_group_id()` to query pending, active, and retry requests for a group.
- Added `HttpBatchRequestHandler::group_request_count()` and `has_group_id()` for batch-level group visibility.
- Added integration test coverage for `wait_requests()` group isolation, `wait_requests_for()` timeout, per-client `max_in_flight`, retry chains, and sequential rate-limit retry non-blocking behavior.
- Added RAII-backed HTTP rate limit handles so pending requests keep their assigned limits alive until completion.
- Added HTTP response streaming callbacks via `HttpRequest::streaming`, `HttpClient::set_streaming(...)`, and callback overloads for standalone HTTP helpers.
- Added `HttpResponse::stream_chunk` to distinguish intermediate body chunks from the final ready response.
- Added CMake build integration with fallback dependency helpers for OpenSSL, libcurl, Asio, and Simple-WebSocket-Server.
- Added CI coverage for Linux and macOS smoke builds, Windows integration builds, and ODR regression tests.
- Added integration and ODR tests that verify singleton ownership across translation units and auto-initialization behavior.
- Added synchronous lifecycle regression coverage for repeated `deinit()` calls after `init(false)`.
- Added low-risk backpressure support with `SubmitResult`, a global HTTP pending queue cap, a per-client WebSocket send queue cap, explicit rejection errors, and integration/CI coverage for admission rejects.
- Added optional sequential rate-limit mode (`create_rate_limit(..., sequential=true)`) so no other request sharing the limit may start while any request (including retries) is still in-flight.
- Added integration test and example coverage for the sequential rate-limit feature.
- Added partitioned rate-limit keys so a single rate-limit ID can be split by string key. Requests with the same key share state; different keys are independent. Empty key preserves backward-compatible global behavior.
- Added `HttpClient::set_rate_limit_key(...)`, `set_general_rate_limit_key(...)`, `set_specific_rate_limit_key(...)`, and `set_rate_limit_keys(...)` for setting partition keys on outgoing requests.
- Added integration test coverage for partitioned rate limits covering key independence, same-key blocking, empty-key global fallback, sequential partitioning, per-key release, and per-key delay calculation.

### Changed
- OAuth authorization flows now rotate the `state` value and PKCE
  verifier whenever `build_authorization_url()` starts a new flow.
- Replaced dual `KURLYK_ENABLE_JSON` / `KURLYK_USE_JSON` macros with unified `KURLYK_JSON_SUPPORT`.
  Legacy aliases map automatically for backward compatibility.
- Documented retry chain callback contract: intermediate callbacks fire on each retry attempt; the final callback fires on success or retry exhaustion.
- Added separate HTTP `request_id` and `group_id` semantics so individual requests can be cancelled by request ID while `HttpClient::cancel_requests()` cancels the client's request group.
- Documented HTTP callback threading and disabled automatic retries after streaming chunks have been emitted.
- Switched Windows MinGW CI jobs from Chocolatey MinGW path assumptions to MSYS2 UCRT64 with Ninja.
- Updated auto-initialization cleanup to stop the `NetworkWorker` instead of only resetting managers.
- Refreshed README and README-RU usage, initialization, dependency, and configuration macro documentation.
- Added architecture documentation and expanded Doxygen mainpage guidance for startup, error handling, and manual lifecycle usage.
- Cleaned up public include compatibility around startup and utility headers while preserving the header-only API surface.
- Updated examples and comments for clearer console output, manual lifecycle handling, and non-pausing execution.
- Updated the Asio submodule to `asio-1-36-0`.
- Added in-flight token tracking so sequential rate-limit locks are only released when `HttpRequestContext::complete()` is called (success, retry exhaustion, cancellation, or handler destruction).

### Fixed
- Fixed group wait accounting across pending-to-active transitions and
  timeout/shutdown cleanup.
- Fixed HTTP response, completion, and streaming callback exception handling so
  request accounting is completed even when user callbacks throw.
- Fixed `HttpRequestManager::time_until_next_allowed()` wrapper to correctly handle `RateLimitDelay<Duration>` vs raw `Duration` return types.
- Fixed `HttpBatchRequestHandler::process()` batch completion to use `still_running == 0` as the source of truth instead of `m_handlers.empty()`.
- Fixed HTTP retry decisions so curl transfer errors can retry even when an HTTP status code was already received.
- Fixed Windows executable path conversion with C++20 `std::filesystem::path::u8string()`.
- Fixed lifecycle cleanup so `deinit()` cleans up both asynchronous `init(true)` and synchronous `init(false)` modes.

## [v1.0.1] - 2025-08-16
- Documented automatic initialization behavior on the Doxygen mainpage.
- Clarified example console output and refreshed example comments.
- Added error handler overview and documented centralized `KURLYK_HANDLE_ERROR` tracing.
- Documented configuration macros and corrected related Doxygen comments.
- Replaced example pause calls with `std::cin.get()` and improved usage comments.
- Fixed a hang during `cancel_requests()` and added error tracing dispatch through `NetworkWorker`.
- Improved the Doxygen main page structure and configuration macro coverage.
- Refined README configuration macro documentation.
- Added architecture and system-map guidance for contributors.

## [v1.0.0] - 2024-11-12
- Initial public release with header-only HTTP and WebSocket clients built on libcurl and Simple-WebSocket-Server.
- Added asynchronous HTTP request processing, callbacks, futures, retries, cancellation by request ID, and rate limiting.
- Added WebSocket client support with events, sending helpers, reconnection logic, proxy configuration, and timeout settings.
- Added proxy configuration, custom headers, `Accept-Language`, User-Agent helpers, and `sec-ch-ua` generation.
- Added HTTP timing metrics, HEAD request support, custom valid status codes, and timeout status handling.
- Added client, HTTP, curl, and WebSocket error categories with centralized error reporting.
- Added URL, path, encoding, email, percent-encoding, HTTP parsing, and case-insensitive header utilities.
- Added examples for simple HTTP requests, futures, proxies, redirects, nested requests, delayed requests, cancellation, and WebSocket echo usage.
- Reorganized the include structure into core, HTTP, WebSocket, types, utils, and startup modules.
- Added generated documentation styling and Doxygen configuration.
- Fixed Boost compatibility issues, C++17 compilation issues, Cyrillic path support, rate limiter timing, and blocked-site status handling.

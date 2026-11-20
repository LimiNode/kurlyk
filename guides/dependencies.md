# Dependencies

`kurlyk` is header-only but requires linked libraries for transport and crypto.

| Dependency | Role | Fallback |
|---|---|---|
| libcurl | HTTP transport | `DKURLYK_USE_FALLBACK_CURL=ON` |
| OpenSSL | TLS for curl | `DKURLYK_USE_FALLBACK_OPENSSL=ON` |
| Asio (Boost or standalone) | WebSocket I/O | `DKURLYK_USE_FALLBACK_ASIO=ON` |
| Simple-WebSocket-Server | WebSocket protocol layer | `DKURLYK_USE_FALLBACK_SIMPLE_WS_SERVER=ON` |
| nlohmann/json | Optional JSON support behind `KURLYK_JSON_SUPPORT` | None (disabled by default) |
| Emscripten | Alternative transport for web builds | Enabled automatically when compiling for Emscripten |

On macOS, use system or Homebrew `curl` and `openssl@3`; the Windows binary
fallbacks for libcurl and OpenSSL are not used on Apple platforms. Asio and
Simple-WebSocket-Server can use the checked-out headers in `external/`.

## Policy

- Prefer system packages; use bundled copies in `external/` only when the system
  package is unavailable.
- Do not edit vendored code under `external/` unless the task is an explicit
  dependency update.
- Optional features stay behind compile definitions:
  `KURLYK_JSON_SUPPORT`, `KURLYK_WEBSOCKET_SUPPORT`.

## Flat parent-provided dependencies

Kurlyk reuses existing `CURL::libcurl`, `OpenSSL::SSL`, `OpenSSL::Crypto`,
`asio::asio` (or `Boost::boost`) and `simple_ws_server` targets before selecting
its own providers. Examples also reuse `nlohmann_json::nlohmann_json` when supplied.
Integration tests reuse `simple_web_server` before inspecting any source directory.
Alternatively, set `KURLYK_SIMPLE_WEB_SERVER_SOURCE_DIR` to a local checkout
containing `server_http.hpp`. Nested Kurlyk submodules need not be initialized
when the parent provides the used dependencies.

Set `KURLYK_ALLOW_DOWNLOADS=OFF` for an offline configure. Missing dependencies
then fail with an actionable error instead of downloading. The default `ON`
preserves standalone fallback behavior; remote fallbacks use pinned commits,
including Simple-Web-Server `546895a93a29062bb178367b46c7afb72da9881e`.
Offline mode does not acquire or initialize dependencies: provide their targets,
installed packages, or local checkouts beforehand.

The installed package also reuses existing dependency targets. Its default SWS
header layout is `simple-websocket-server/client_ws.hpp`. A parent-provided
`simple_ws_server` with a direct include directory containing `client_ws.hpp`
is detected as flat. For a flat include path hidden behind generator expressions
or transitive targets, propagate `KURLYK_SIMPLE_WS_CLIENT_USE_FLAT` from that target.

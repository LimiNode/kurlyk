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

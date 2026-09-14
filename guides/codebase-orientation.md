# Codebase Orientation

## Repository Layout

| Path | Purpose |
|------|---------|
| `include/` | Public header-only library. |
| `include/kurlyk/core` | Core infrastructure with `NetworkWorker` and base interfaces. |
| `include/kurlyk/http` | HTTP client and request management. |
| `include/kurlyk/websocket` | WebSocket client and connection management. |
| `include/kurlyk/types` | Shared enums, cookie, proxy config, and helpers. |
| `include/kurlyk/utils` | Encoding, URL, HTTP, path, and error helpers. |
| `include/kurlyk/startup` | Optional auto-initialization helpers. |
| `examples/` | Usage examples built against bundled or system libs. |
| `docs/` | Generated and hand-written documentation. |
| `tests/integration` | Cross-platform local HTTP/WebSocket integration build checks. |
| `tests/odr` | Header-only ODR checks. |
| `tests/smoke` | Portable header smoke checks. |

## Architectural Patterns & Invariants

- Header-only design keeps all functionality in headers; no compiled library.
- `core::NetworkWorker` processes tasks on a single background thread; avoid blocking it.
- HTTP and WebSocket managers apply rate limits and retries consistently.
- Error dispatch flows through `NetworkWorker`; maintain exception safety.
- Public APIs rely on value semantics and RAII.
- Configuration is compile-time via macros with minimal defaults.
- Code remains portable across C++11/17 compilers and network stacks.
- Treat `include/` as the primary source of truth. Generated copies under build directories are not source files.

## Code Discovery Protocol

When exploring or modifying code, prefer the **codebase-memory MCP** tools over raw grep/Read for cross-file understanding.

### Mandatory sequence

1. **Index** — if the repository is not yet indexed, call `mcp__codebase-memory__index_repository` with `mode="moderate"` (fast for quick lookup, full for deep refactoring).
2. **Search** — use `mcp__codebase-memory__search_graph` or `mcp__codebase-memory__search_code` to find symbols, classes, and call sites.
3. **Trace** — use `mcp__codebase-memory__trace_path` to follow call chains or data flow.
4. **Read** — use `mcp__codebase-memory__get_code_snippet` for targeted source reading instead of loading entire files into context.

### Fallback

If codebase-memory MCP is unavailable, use `Grep` for symbol search, `Read` for file contents, and `LSP` for definitions/diagnostics. Never use raw `grep` or `cat` via Bash.

### Avoid large artifacts

Build directories (`build*/`, generated docs, and dependency artifacts under `external/`) are not source files. Exclude them from indexing and reading.

# Build and Test

## Dependencies

- **libcurl** — HTTP transport
- **OpenSSL** — TLS for both HTTP and WebSocket
- **Boost.Asio** or **standalone Asio** — WebSocket I/O
- **Simple-WebSocket-Server** — WebSocket protocol layer

Header-only dependencies such as Asio and Simple-WebSocket-Server are bundled
as git submodules under `external/`. libcurl and OpenSSL can be provided by the
system or fetched through the platform-specific fallback snapshot repositories.

## Quick Build

Add `include/` to your compiler's include path and link against the libraries above. Examples live in `examples/`.

Build all repository targets with CMake:

```bash
cmake -S . -B build-examples -DKURLYK_BUILD_EXAMPLES=ON
cmake --build build-examples --config Release
```

For MinGW, select the generator and compilers explicitly:

```bash
cmake -S . -B build-examples-mingw -G "MinGW Makefiles" \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DKURLYK_BUILD_EXAMPLES=ON
cmake --build build-examples-mingw --config Release
```

### Minimal manual compilation (HTTP only)

```bash
g++ examples/simple_http_request_example.cpp -Iinclude -std=c++17 \
    -pthread -lcurl -lssl -lcrypto -DKURLYK_WEBSOCKET_SUPPORT=0 -o simple_http_example
./simple_http_example
```

## Fallback Dependencies

CMake can automatically download missing dependencies. Availability depends on the compiler and linkage type.

| Dependency | MinGW (Shared) | MinGW (Static) | MSVC (Shared) | MSVC (Static) | macOS (system/Homebrew) |
|------------|---------------|---------------|---------------|---------------|-------------------------|
| OpenSSL    | yes           | yes           | yes           | yes           | yes                     |
| curl       | yes           | yes           | yes           | no            | yes                     |

Asio and Simple-WebSocket-Server are header-only and work for all build variants.

### CMake fallback options

| Option | Description |
|--------|-------------|
| `KURLYK_USE_FALLBACK_OPENSSL` | Enables OpenSSL fallback. |
| `KURLYK_USE_FALLBACK_CURL` | Enables libcurl fallback. |
| `KURLYK_USE_FALLBACK_ASIO` | Enables Asio fallback. |
| `KURLYK_USE_FALLBACK_SIMPLE_WS_SERVER` | Enables Simple-WebSocket-Server fallback. |
| `KURLYK_OPENSSL_SHARED` | Loads OpenSSL as a shared library when fallback is enabled. |
| `KURLYK_CURL_SHARED` | Loads libcurl as a shared library when fallback is enabled. |
| `KURLYK_BUILD_EXAMPLES` | Builds all targets from the `examples/` directory. |

## Testing

The repository includes auth unit tests, integration tests, ODR checks, and portable smoke checks. When modifying library headers, compile at least one example from `examples/` (e.g., `simple_http_request_example.cpp`) to ensure the code still builds.

### Windows integration suite

```powershell
powershell -ExecutionPolicy Bypass -File tests/integration/run_integration_tests.ps1
```

### macOS integration suite

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

### ODR suite

```powershell
powershell -ExecutionPolicy Bypass -File tests/odr/run_odr_tests.ps1
```

### Portable smoke test

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

## CI Coverage

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

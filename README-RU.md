# kurlyk
![logo](docs/logo-mini.png)

**C++ library for easy networking**

[![MIT License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue)
![C++ Standard](https://img.shields.io/badge/C++-11--17-orange)
![CI Windows](https://img.shields.io/github/actions/workflow/status/LimiNode/kurlyk/ci.yml?branch=main&label=Windows&logo=windows)
![CI Linux](https://img.shields.io/github/actions/workflow/status/LimiNode/kurlyk/ci.yml?branch=main&label=Linux&logo=linux)
![CI macOS](https://img.shields.io/github/actions/workflow/status/LimiNode/kurlyk/ci.yml?branch=main&label=macOS&logo=apple)

[Do you speak English?](README.md)

## Что это

**kurlyk** — это ещё одна очередная библиотека, реализующая HTTP и WebSocket клиенты для C++. Построена как обертка над `curl` и `Simple-WebSocket-Server`, предоставляя упрощённый интерфейс для работы с HTTP и WebSocket в C++ приложениях. Она поддерживает асинхронное выполнение HTTP-запросов с ограничением скорости и повторными попытками, а также работу с WebSocket-соединениями.

Если вам по каким-то причинам не подошли другие библиотеки, такие как *easyhttp-cpp, curl_request, curlpp-async, curlwrapper, curl-Easy-cpp, curlpp11, easycurl, curl-cpp-wrapper…* возможно, стоит попробовать `kurlyk`.

## Возможности

- Асинхронное выполнение HTTP и WebSocket запросов.
- Фоновый worker или синхронная обработка через `kurlyk::process()`.
- HTTP callback API и `std::future` API.
- Rate limits (с разбиением по ключу), retry, proxy, пользовательские заголовки, cookie и таймауты.
- Проверка HTTP/HTTPS proxy через запросы без body и timing-метрики curl.
- Streaming HTTP responses с callback'ом на каждый chunk.
- WebSocket events, отправка сообщений и автоматическое переподключение.
- Bounded admission/backpressure для HTTP pending queue и WebSocket send queue.
- Провайдеры аутентификации Bearer token и API key (`BearerTokenAuthProvider`, `ApiKeyAuthProvider`).
- OAuth2 Authorization Code + PKCE клиент (`OAuthPkceClient`) поверх standalone HTTP helpers.
- Поддержка C++11 и более новых toolchains.

## Быстрый старт

### Минимальный HTTP GET

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

Используйте `kurlyk::init()` / `kurlyk::deinit()`, если требуется явное управление жизненным циклом.

### Минимальный WebSocket echo

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

## Сборка примеров

Примеры находятся в папке `examples`. Собрать все targets из репозитория через CMake можно так:

```powershell
cmake -S . -B build-examples -DKURLYK_BUILD_EXAMPLES=ON
cmake --build build-examples --config Release
```

Для MinGW можно явно выбрать генератор и компиляторы:

```powershell
cmake -S . -B build-examples-mingw -G "MinGW Makefiles" `
    -DCMAKE_C_COMPILER=gcc `
    -DCMAKE_CXX_COMPILER=g++ `
    -DKURLYK_BUILD_EXAMPLES=ON
cmake --build build-examples-mingw --config Release
```

Для MinGW заголовочные зависимости находятся в репозитории как git
submodules в папке `external`. Fallback CMake-опции ниже загружают
отсутствующие бинарные зависимости, включая закреплённые Windows-снимки curl
и OpenSSL.

## Базовое использование HTTP

HTTP-клиент можно использовать через callbacks, futures или низкоуровневые helper'ы. При auto-init, включённом по умолчанию, `HttpClient` можно создавать без ручных `kurlyk::init()` / `kurlyk::deinit()`; используйте lifecycle-функции для синхронного режима или явного управления worker'ом.

### Общий helper для примеров

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

Callback-overload'ы `get(...)`, `post(...)` и `request(...)` возвращают `bool`: `true`, если запрос принят в очередь, и `false`, если он отклонён на этапе admission. Сам callback получает `kurlyk::HttpResponsePtr` и вызывается не только на финальный ответ: в streaming-режиме он приходит на каждый chunk с `stream_chunk == true`, а при retry может прийти промежуточный неготовый response для неуспешной попытки. Финальный результат определяется по `response && response->ready`.

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

Если запрос отклоняется до попадания в pending queue, future становится ready сразу и возвращает `HttpResponse` с `error_code = QueueLimitExceeded` или `ShuttingDown`.

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

### Проверка proxy

`ProxyChecker` проверяет пассивный `ProxyConfig` одним асинхронным `HEAD`-запросом через proxy. Тестовый URL настраивается и по умолчанию равен `https://example.com/`; для воспроизводимой проверки доступности лучше указывать контролируемый endpoint. Следование перенаправлениям и туннелирование через proxy по умолчанию отключены, поэтому проверка измеряет один endpoint через обычный HTTP proxy. `options.proxy_tunnel = true` принудительно включает HTTP CONNECT tunneling; HTTPS-запросы через HTTP proxy могут автоматически использовать CONNECT даже при значении `false`. Для HTTPS endpoint с приватным центром сертификации задайте путь к набору сертификатов через `options.ca_file`. Checker отключает retry и не загружает response body — это тот же облегчённый подход, который используется при поиске доступного host.

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
              << "connect: " << result.connect_latency_ms << " ms\n"
              << "TLS/CONNECT: " << result.tls_latency_ms << " ms\n"
              << "TTFB: " << result.ttfb_ms << " ms\n"
              << "total: " << result.total_latency_ms << " ms\n";
}
```

`connect_latency_ms` — время curl от начала проверки до установки TCP-соединения с proxy, включая разрешение имени. `tls_latency_ms` — интервал после TCP connect до завершения TLS; для HTTPS через HTTP proxy он также включает согласование CONNECT. `ttfb_ms` и `total_latency_ms` измеряются от начала запроса. Значения timeout задаются в секундах и напрямую передаются HTTP transport. `reachable` означает, что через proxy получен HTTP-ответ; это не ICMP ping. Вызов `check(...)` явно проверяет переданный DTO, поэтому поле `ProxyConfig::use` не учитывается.

### Low-level helpers

Низкоуровневые helper'ы удобны, когда нужен ID конкретного запроса или прямой доступ к standalone HTTP API.

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

## Базовое использование WebSocket

`WebSocketClient` подключается к серверу, сообщает о событиях через `on_event(...)` и позволяет отправлять сообщения через sender из события или через сам клиент. Для простого сценария достаточно обработать `WS_OPEN`, `WS_MESSAGE`, `WS_CLOSE` и `WS_ERROR`.

### Подключение и обработка событий

```cpp
#include <kurlyk.hpp>
#include <thread>
#include <chrono>

int main() {
    kurlyk::WebSocketClient client("wss://echo-websocket.fly.dev/");

    client.on_event([](std::unique_ptr<kurlyk::WebSocketEventData> event) {
        switch (event->event_type) {
            case kurlyk::WebSocketEventType::WS_OPEN:
                KURLYK_PRINT << "Соединение установлено" << std::endl;
                event->sender->send_message("Привет, WebSocket!");
                break;

            case kurlyk::WebSocketEventType::WS_MESSAGE:
                KURLYK_PRINT << "Получено сообщение: " << event->message << std::endl;
                break;

            case kurlyk::WebSocketEventType::WS_CLOSE:
                KURLYK_PRINT << "Соединение закрыто: " << event->message
                             << "; Код статуса: " << event->status_code << std::endl;
                break;

            case kurlyk::WebSocketEventType::WS_ERROR:
                KURLYK_PRINT << "Ошибка: " << event->error_code.message() << std::endl;
                break;
        }
    });

    client.connect();
    std::this_thread::sleep_for(std::chrono::seconds(10));
    client.disconnect_and_wait();
    return 0;
}
```

### Отправка сообщений

`send_message(...)` возвращает `bool` и подходит для простого кода. `submit_message(...)` возвращает `SubmitResult` и позволяет отличать успешное принятие сообщения в очередь от admission reject.

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

### Ограничение очереди отправки

Для защиты producer'а можно ограничить размер очереди исходящих WebSocket send/close операций. Значение `0` означает очередь без ограничения.

```cpp
kurlyk::WebSocketClient client("wss://echo-websocket.fly.dev/");
client.set_max_send_queue_size(32);
```

## Инициализация

Автоинициализация доступна по умолчанию в C++11 и новее, поэтому простые примеры могут сразу создавать `HttpClient` или `WebSocketClient`. Отключите auto init и вызовите `init()` / `deinit()`, если требуется явное управление жизненным циклом.

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

`kurlyk::deinit()` является обычным вызовом очистки как для асинхронного режима `init(true)`, так и для синхронного режима `init(false)`. `kurlyk::shutdown()` остаётся доступен для явной очистки/сброса менеджеров, но для обычного ручного жизненного цикла используйте пару `init()` / `deinit()`.

## Продвинутые возможности

### Rate limits

Rate limit ограничивает скорость выпуска HTTP-запросов в сетевой backend. Для простого ограничения скорости у одного клиента используйте `set_rate_limit_rps(...)` или `set_rate_limit_rpm(...)`.

```cpp
kurlyk::HttpClient client("https://api.example.com");
client.set_rate_limit_rps(1);
```

Для общего лимита между несколькими клиентами используйте `HttpRateLimitHandlePtr`:

```cpp
auto limit = kurlyk::create_rate_limit_rps(5);

kurlyk::HttpClient client_a("https://api.example.com");
kurlyk::HttpClient client_b("https://api.example.com");

client_a.set_rate_limit_handle(limit);
client_b.set_rate_limit_handle(limit);
```

`HttpRateLimitHandlePtr` удерживает физические данные лимита живыми, пока существует хотя бы одна копия handle. Pending, active и retrying requests копируют назначенные handles, поэтому `remove_limit(id)` или `remove_limit(handle)` только освобождает manager-owned reference и не инвалидирует уже поставленные в очередь запросы.

Для нового кода предпочтительны handle-based API:

- `HttpClient::set_rate_limit_handle(...)`
- `HttpClient::assign_rate_limit_handle(...)`
- per-request overload'ы, принимающие `HttpRateLimitHandlePtr`

ID-based API вроде `set_rate_limit_id(...)` и per-request `long specific_rate_limit_id` overload'ов остаются legacy lookup helpers. Если manager-owned handle уже удалён, lookup по ID вернёт пустой handle, и запрос будет отправлен без этого дополнительного specific limit.

### Отмена HTTP-запросов

HTTP-запросы можно отменять по ID конкретного запроса или по ID группы связанных запросов. `HttpClient` назначает один `group_id` всем запросам, созданным этим клиентом, поэтому `HttpClient::cancel_requests()` отменяет группу запросов клиента.

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

У HTTP-запросов есть два идентификатора:

- `request_id` — ID конкретного запроса, используется в `cancel_request_by_id(...)`;
- `group_id` — ID группы связанных запросов, используется в `cancel_requests_by_group_id(...)`.

Для manually constructed low-level запросов `group_id` нужно задавать явно, если вы хотите отменять группу:

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

Rate limit замедляет выпуск запросов. Backpressure ограничивает количество запросов или сообщений, которые вообще принимаются в очередь.

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

Что ограничивается сейчас:

- HTTP использует глобальный лимит pending queue через `kurlyk::set_max_pending_requests(...)`;
- WebSocket использует per-client лимит очереди исходящих send/close через `WebSocketClient::set_max_send_queue_size(...)`;
- значение `0` означает unbounded queue.

Публичные admission helper'ы:

- `kurlyk::SubmitResult`
- `kurlyk::submit_http_request(...)`
- `IWebSocketSender::submit_message(...)`
- `IWebSocketSender::submit_close(...)`
- `WebSocketClient::submit_message(...)`
- `WebSocketClient::submit_close(...)`

Совместимость:

- старые `bool`-API сохранены как wrappers;
- HTTP future-overload при admission reject становится ready сразу и возвращает `HttpResponse` с `error_code`, а не бросает `runtime_error`;
- в этом проходе не ограничиваются очереди `NetworkWorker`, WebSocket FSM и накопленные event queues.

### Streaming

Streaming позволяет получать HTTP body частями, не дожидаясь полного завершения ответа. Callback вызывается для каждого chunk с `response->stream_chunk == true` и `response->ready == false`, а последним приходит обычный завершённый response с `ready == true`.

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

Для запросов, создаваемых через `HttpClient`, включите streaming на клиенте:

```cpp
kurlyk::HttpClient client("http://httpbin.org");
client.set_streaming(true);
```

Используйте `stream_chunk` первым для классификации body chunk callback'ов. Chunk callback не является маркером успеха; итоговый `ready`-ответ остаётся авторитетным результатом передачи. Если был отдан хотя бы один streaming chunk, kurlyk не делает автоматический retry этой передачи, потому что вызывающий код уже мог переслать байты downstream-клиенту.

### Retry

Retry повторяет неуспешный HTTP-запрос после заданной задержки. Это удобно для временных сетевых ошибок и нестабильных upstream API.

```cpp
kurlyk::HttpClient client("https://httpbin.org");
client.set_retry_attempts(3, 1000);
```

Количество выполненных попыток доступно в `HttpResponse::retry_attempt`. Для streaming-запросов автоматический retry не выполняется после того, как был отдан хотя бы один body chunk.

### Proxy

Proxy можно настроить на уровне `HttpClient`, после чего настройки будут применяться к запросам этого клиента.

```cpp
kurlyk::HttpClient client("https://httpbin.org");
client.set_proxy("127.0.0.1", 8080, "username", "password", kurlyk::ProxyType::PROXY_HTTP);
```

## Установка и зависимости

### Поддерживаемые toolchains

- **MSVC**
- **MinGW (GCC)**
- **AppleClang (macOS)**

Базовый поддерживаемый стандарт языка — **C++11**. Примеры и integration-тесты используют **C++17**, если этого требует исходный код. MSVC, MinGW и AppleClang покрываются CI; зависимости macOS предоставляются системой или Homebrew.

### Подключение kurlyk

Добавьте в проект путь к заголовочным файлам библиотеки:

```text
kurlyk/include
```

**kurlyk** — header-only библиотека, поэтому достаточно подключить её через `#include <kurlyk.hpp>` и начать использовать.

### Установка как CMake-пакета

Установите заголовки и метаданные CMake в staging-префикс:

```bash
cmake -S . -B build-package \
    -DKURLYK_BUILD_EXAMPLES=OFF
cmake --build build-package
cmake --install build-package --prefix /path/to/kurlyk-install
```

Внешний проект CMake может использовать экспортируемую цель `kurlyk::kurlyk`:

```cmake
find_package(kurlyk CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE kurlyk::kurlyk)
```

Установленный пакет по-прежнему требует транспортные зависимости выбранной
конфигурации: OpenSSL, libcurl, standalone Asio или Boost.Asio, заголовки
Simple-WebSocket-Server. OAuth PKCE использует существующую зависимость OpenSSL. Если Asio
или Simple-WebSocket-Server не находятся через менеджер пакетов, передайте
`KURLYK_ASIO_INCLUDE_DIR` и `KURLYK_SIMPLE_WS_INCLUDE_DIR` при конфигурации
потребителя.

### Зависимости

Для работы библиотеки **kurlyk** в среде MinGW потребуются следующие зависимости:

1. Для WebSocket:
   - [Simple-WebSocket-Server](https://gitlab.com/eidheim/Simple-WebSocket-Server)
   - Boost.Asio или [standalone Asio](https://github.com/chriskohlhoff/asio/tree/master)
   - [OpenSSL](https://slproweb.com/products/Win32OpenSSL.html) (*LTS версия Win64 OpenSSL v3.5.8*)

2. Для HTTP:
   - [libcurl](https://curl.se/windows/)

Заголовочные зависимости, включая Asio и Simple-WebSocket-Server, находятся в
папке `external` как субмодули и используются из checkout, если он доступен.
Windows-сборки curl и OpenSSL для fallback-режима загружаются из закреплённых
репозиториев snapshot-зависимостей, когда включены соответствующие fallback-опции.

### OpenSSL

Добавьте в проект пути к OpenSSL, например для версии *3.5.8*:

```text
openssl-win64-v3.5.8/include
openssl-win64-v3.5.8/lib/VC/x64/MD
openssl-win64-v3.5.8/bin
```

Для shared-сборки OpenSSL подключите из `lib/VC/x64/MD`:

```text
libcrypto.lib
libssl.lib
```

Для static-сборки OpenSSL вместо них подключите:

```text
libcrypto_static.lib
libssl_static.lib
```

### Asio

Добавьте в проект путь к asio:

```text
asio/asio/include
```

Для standalone Asio задайте макрос `ASIO_STANDALONE` в параметрах проекта или перед подключением `kurlyk.hpp`:

```cpp
#define ASIO_STANDALONE
#include <kurlyk.hpp>
```

Для Boost.Asio указывать макрос `ASIO_STANDALONE` не нужно.

### curl

Добавьте в проект пути для `curl`, например для версии *8.22.0*:

```text
curl-8.22.0_1-win64-mingw/bin
curl-8.22.0_1-win64-mingw/include
curl-8.22.0_1-win64-mingw/lib
```

Подключите библиотеки `curl` из папки `lib`:

```text
libcurl.dll.a
```

### Simple-WebSocket-Server

Добавьте в проект путь к заголовочным файлам библиотеки:

```text
Simple-WebSocket-Server
```

### Остальные зависимости

Также добавьте следующие библиотеки в линкер:

```text
ws2_32
wsock32
crypt32
```

### Fallback зависимости

Библиотека поддерживает автоматическую загрузку зависимостей в случае их отсутствия. Наличие fallback'а зависит от компилятора и типа линковки.

| Dependency | MinGW (Shared) | MinGW (Static) | MSVC (Shared) | MSVC (Static) | macOS (system/Homebrew) |
|------------|---------------|---------------|---------------|---------------|-------------------------|
| OpenSSL    | yes           | yes           | yes           | yes           | yes                     |
| curl       | yes           | yes           | yes           | no            | yes                     |

Asio и Simple-WebSocket-Server — header-only библиотеки и подходят для всех указанных вариантов сборок.

#### Опции CMake fallback

| Опция | Описание |
|-------|----------|
| `KURLYK_USE_FALLBACK_OPENSSL` | Включает fallback OpenSSL. |
| `KURLYK_USE_FALLBACK_CURL` | Включает fallback libcurl. |
| `KURLYK_USE_FALLBACK_ASIO` | Включает fallback Asio. |
| `KURLYK_USE_FALLBACK_SIMPLE_WS_SERVER` | Включает fallback Simple-WebSocket-Server. |
| `KURLYK_OPENSSL_SHARED` | Загружает OpenSSL как shared library, если fallback включён. |
| `KURLYK_CURL_SHARED` | Загружает libcurl как shared library, если fallback включён. |
| `KURLYK_BUILD_EXAMPLES` | Собирает все targets из каталога `examples/`. |

## Конфигурационные макросы

Перед подключением `kurlyk.hpp` можно определить следующие макросы для настройки библиотеки:

| Макрос | По умолчанию | Описание |
|--------|--------------|----------|
| `KURLYK_AUTO_INIT` | `1` | Автоматическая регистрация менеджеров во время статической инициализации. |
| `KURLYK_AUTO_INIT_USE_ASYNC` | `1` | При включённом auto init запускает сетевой поток в фоне. Установите `0`, если требуется выполнять обработку вручную. |
| `KURLYK_HTTP_SUPPORT` | `1` | Включает или отключает HTTP-подсистему. |
| `KURLYK_WEBSOCKET_SUPPORT` | `1` | Включает или отключает WebSocket-подсистему. |
| `KURLYK_AUTH_SUPPORT` | `1` | Включает auth-провайдеры (`BearerTokenAuthProvider`, `ApiKeyAuthProvider`). |
| `KURLYK_OAUTH_SUPPORT` | `KURLYK_AUTH_SUPPORT` | Включает OAuth2 PKCE клиент (`OAuthPkceClient`). Требует `KURLYK_AUTH_SUPPORT=1`. |
| `KURLYK_JSON_SUPPORT` | `0` | Включает nlohmann::json и JSON-aware типы, а также enum-to-JSON helpers в `type_utils.hpp`. |

## Структура репозитория

| Путь | Назначение |
|------|------------|
| `include/` | Публичная header-only библиотека. |
| `include/kurlyk/core` | Core-инфраструктура с `NetworkWorker` и базовыми интерфейсами. |
| `include/kurlyk/http` | HTTP client и request management. |
| `include/kurlyk/websocket` | WebSocket client и connection management. |
| `include/kurlyk/types` | Общие enum, cookie, proxy config и helpers. |
| `include/kurlyk/utils` | Encoding, URL, HTTP, path и error helpers. |
| `tests/integration` | Кроссплатформенные локальные HTTP/WebSocket integration build checks. |
| `tests/odr` | Header-only ODR checks. |
| `tests/smoke` | Portable header smoke checks. |
| `tests/package` | Проверки install-tree и внешнего потребителя CMake. |
| `examples/` | Примеры использования. |

## Инструменты для AI-агентов

Репозиторий включает конфигурацию агентов и оркестрационные метаданные для AI-инструментов разработки (например, Claude Code с oh-my-claudecode). Рекомендуемые MCP-серверы и плагины для работы с кодовой базой:

| Категория | MCP-сервер / плагин | Назначение |
|-----------|---------------------|------------|
| Поиск документации | context7 | Разрешение SDK/API-документации до веб-поиска |
| Веб-поиск | DDG Search (без ключа) | Основной fallback веб-поиска |
| Веб-поиск | Tavily | Глубокий веб-поиск и извлечение контента |
| Веб-контент | Fetch | Загрузка Markdown/JSON/TXT по известным URL |
| Автоматизация браузера | Playwright | UI-автоматизация и скриншот-тестирование |
| Операции с репозиторием | GitHub | Issues, PR и файловые операции |
| Навигация по коду | Codebase Memory | Графовый поиск кода и цепочки вызовов |
| Большой вывод | Context-Mode | Пакетное выполнение, индексация и анализ |
| Структурный код | AST grep (OMC plugin) | Структурный поиск и замена |
| Runtime | Python REPL (OMC plugin) | Выполнение скриптов в сессии |
| Диагностика | LSP | Символы, определения и диагностика |

Полная цепочка приоритетов инструментов и fallback-правила — в [`.claude/rules/tool-priority.md`](.claude/rules/tool-priority.md).

## Тесты

Запуск Windows integration suite:

```powershell
powershell -ExecutionPolicy Bypass -File tests/integration/run_integration_tests.ps1
```

Запуск ODR suite:

```powershell
powershell -ExecutionPolicy Bypass -File tests/odr/run_odr_tests.ps1
```

Запуск unit-тестов авторизации:

```bash
cmake -S tests/auth -B build-auth-tests -G Ninja \
    -DKURLYK_BUILD_EXAMPLES=OFF \
    -DKURLYK_USE_FALLBACK_ASIO=ON \
    -DKURLYK_USE_FALLBACK_SIMPLE_WS_SERVER=ON
cmake --build build-auth-tests
ctest --test-dir build-auth-tests
```

На macOS установите системные зависимости через Homebrew и укажите CMake
keg-only установку OpenSSL:

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

Ручная сборка portable smoke test:

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

## Хелперы авторизации

`kurlyk` предоставляет легковесные header-only хелперы для аутентификации поверх существующего HTTP-слоя.

- **`BearerTokenAuthProvider`** — добавляет заголовок `Authorization: Bearer <token>`.
- **`ApiKeyAuthProvider`** — вставляет API-ключ в заголовок или в query-параметр (с автоматическим percent-encoding).
- **`OAuthPkceClient`** — строит authorization URL и обменивает authorization code на токен с помощью PKCE (RFC 7636). Использует standalone `kurlyk::http_post`, поэтому не требует `HttpClient`.

Подробнее:

- [OAuth2 / PKCE](guides/oauth.md)
- [Провайдеры авторизации](guides/auth-providers.md)

## CI-покрытие

| Платформа | Что проверяется |
|-----------|-----------------|
| Windows | Integration-сборки MinGW и MSVC с fallback-зависимостями, HTTP backpressure regression и локальным WebSocket integration coverage. |
| Windows extras | ODR-проверки singleton и auto-init заголовков. |
| Linux | C++11/C++17 header smoke, C++11 HTTP/proxy checks, полный C++11 compile-only smoke HTTP/WebSocket и C++17 integration examples. |
| macOS | C++11/C++17 header smoke, C++11 HTTP/proxy checks, полный C++11 compile-only smoke HTTP/WebSocket и C++17 integration tests. |
| Packaging | Проверка install-tree CMake и внешнего потребителя C++11. |

## Документация

Генерация Doxygen:

```bash
doxygen Doxyfile
```

Опубликованная документация: <https://newyaroslav.github.io/kurlyk/>.

## Лицензия

Эта библиотека распространяется под лицензией MIT. Подробности смотрите в файле [LICENSE](LICENSE) в репозитории.

## Поддержка

Если у вас возникли вопросы или проблемы при использовании библиотеки, вы можете обратиться к документации или оставить вопрос в разделе Issues на GitHub.

Одним словом, **kurlyk!**

![logo](docs/logo-mini-end.png)

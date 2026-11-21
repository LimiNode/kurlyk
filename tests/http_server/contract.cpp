#include <kurlyk/http/server/detail/BasicHttpServer.hpp>
#include <kurlyk/http/server/detail/response_policy.hpp>
#include <cstdlib>
#include <iostream>

#if defined(ASIO_VERSION) || defined(BOOST_ASIO_VERSION)
#error HTTP server state and policy must not include a transport backend
#endif

#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed line " << __LINE__ << ": " #condition << std::endl; std::abort(); } } while (false)

namespace {
typedef std::function<bool(kurlyk::HttpServerRequest, kurlyk::HttpServerResponse&)> Dispatch;

struct FakeControl {
    std::map<std::string, Dispatch> handlers;
    std::function<void()> execute;
    unsigned int starts = 0;
    unsigned int runs = 0;
    unsigned int stops = 0;
    unsigned int destroyed = 0;
    bool fail_start = false;
};

class FakeBackend {
public:
    explicit FakeBackend(const kurlyk::HttpServerConfig&) : m_control(std::make_shared<FakeControl>()) {
        latest() = m_control;
    }
    ~FakeBackend() { ++m_control->destroyed; }
    static std::shared_ptr<FakeControl>& latest() {
        static std::shared_ptr<FakeControl> control;
        return control;
    }
    void set_handler(const std::string& method, Dispatch dispatch) { m_control->handlers[method] = std::move(dispatch); }
    unsigned short start() {
        ++m_control->starts;
        if (m_control->fail_start) throw std::runtime_error("injected bind failure");
        return 43210;
    }
    void run() { ++m_control->runs; if (m_control->execute) m_control->execute(); }
    void request_stop() noexcept { ++m_control->stops; }
private:
    std::shared_ptr<FakeControl> m_control;
};

typedef kurlyk::detail::BasicHttpServer<FakeBackend> TestServer;

template<class Error, class Operation>
void rejects(Operation operation) {
    bool rejected = false;
    try { operation(); } catch (const Error&) { rejected = true; }
    CHECK(rejected);
}

kurlyk::HttpServerResponse invoke(const std::shared_ptr<FakeControl>& control,
                                  const std::string& method, const std::string& path) {
    kurlyk::HttpServerRequest request;
    request.method = method;
    request.path = path;
    kurlyk::HttpServerResponse response;
    CHECK(control->handlers.at(method)(std::move(request), response));
    return response;
}

void routing_and_lifecycle() {
    TestServer server{kurlyk::HttpServerConfig()};
    const auto control = FakeBackend::latest();
    rejects<std::logic_error>([&server] { server.run(); });
    rejects<std::invalid_argument>([&server] { server.set_handler("GET", "missing-slash", kurlyk::HttpServerHandler()); });
    server.set_handler("GET", "/item", [](kurlyk::HttpServerRequest) {
        kurlyk::HttpServerResponse response;
        response.body = "old";
        return response;
    });
    server.set_handler("GET", "/item", [](kurlyk::HttpServerRequest) {
        kurlyk::HttpServerResponse response;
        response.body = "replacement";
        return response;
    });
    server.set_handler("CUSTOM", "/throw", [](kurlyk::HttpServerRequest) -> kurlyk::HttpServerResponse {
        throw std::runtime_error("handler failure");
    });
    CHECK(server.start() == 43210);
    CHECK(control->starts == 1 && control->handlers.count("CUSTOM") == 1);
    CHECK(invoke(control, "GET", "/item").body == "replacement");
    CHECK(invoke(control, "GET", "/item/").status == 404);
    CHECK(invoke(control, "POST", "/item").status == 404);
    CHECK(invoke(control, "CUSTOM", "/throw").status == 500);
    CHECK(invoke(control, "CUSTOM", "/throw").body.empty());
    rejects<std::logic_error>([&server] { server.start(); });
    rejects<std::logic_error>([&server] { server.set_handler("GET", "/late", kurlyk::HttpServerHandler()); });
    server.run();
    CHECK(control->runs == 1);
    rejects<std::logic_error>([&server] { server.run(); });
    server.request_stop();
    server.request_stop();
    CHECK(control->stops == 1);
    kurlyk::HttpServerRequest request;
    kurlyk::HttpServerResponse response;
    CHECK(!control->handlers.at("GET")(std::move(request), response));
}

void failure_and_destruction() {
    kurlyk::HttpServerConfig invalid;
    invalid.max_request_bytes = 0;
    rejects<std::invalid_argument>([invalid] { TestServer server(invalid); });
    {
        TestServer server{kurlyk::HttpServerConfig()};
        server.request_stop();
        rejects<std::logic_error>([&server] { server.start(); });
    }
    {
        TestServer server{kurlyk::HttpServerConfig()};
        const auto control = FakeBackend::latest();
        control->fail_start = true;
        rejects<std::runtime_error>([&server] { server.start(); });
        rejects<std::logic_error>([&server] { server.start(); });
        CHECK(control->starts == 1);
    }
    {
        std::unique_ptr<TestServer> server(new TestServer(kurlyk::HttpServerConfig()));
        const auto control = FakeBackend::latest();
        server->set_handler("GET", "/destroy", [&server, control](kurlyk::HttpServerRequest) {
            server.reset();
            CHECK(control->stops == 1 && control->destroyed == 0);
            return kurlyk::HttpServerResponse();
        });
        server->start();
        // Weak capture prevents the test script from keeping its own control alive.
        const std::weak_ptr<FakeControl> weak(control);
        control->execute = [weak] {
            const auto active = weak.lock();
            CHECK(active);
            CHECK(invoke(active, "GET", "/destroy").status == 200);
            CHECK(active->destroyed == 0);
            kurlyk::HttpServerRequest request;
            kurlyk::HttpServerResponse response;
            CHECK(!active->handlers.at("GET")(std::move(request), response));
        };
        server->run();
        CHECK(!server && control->destroyed == 1);
    }
}

void response_policy() {
    kurlyk::HttpServerConfig config;
    const unsigned int statuses[] = {200, 204, 205, 304};
    for (const unsigned int status : statuses) {
        for (const std::string& method : {std::string("GET"), std::string("HEAD")}) {
            kurlyk::HttpServerResponse response;
            response.status = status;
            response.body = "x";
            const auto framing = kurlyk::detail::prepare_http_server_response(config, method, response);
            CHECK(response.status == status);
            CHECK(framing.emit_body == (status == 200 && method == "GET"));
            CHECK(framing.emit_content_length == (status != 204 && status != 304));
            CHECK(framing.content_length == (status == 200 ? 1 : 0));
        }
    }
    for (unsigned int byte = 0; byte <= 255; ++byte) {
        kurlyk::HttpServerResponse response;
        response.headers.push_back(std::make_pair("X-Byte", std::string(1, static_cast<char>(byte))));
        kurlyk::detail::prepare_http_server_response(config, "GET", response);
        CHECK(response.status == ((byte < 32 && byte != 9) || byte == 127 ? 500 : 200));
    }
    for (const std::string& name : {std::string("Content-Length"), std::string("TRANSFER-ENCODING"), std::string("cOnNeCtIoN"), std::string("X bad")}) {
        kurlyk::HttpServerResponse response;
        response.body = "must be removed";
        response.headers.push_back(std::make_pair(name, "value"));
        kurlyk::detail::prepare_http_server_response(config, "GET", response);
        CHECK(response.status == 500 && response.body.empty() && response.headers.empty());
    }
    config.max_response_header_bytes = 6;
    kurlyk::HttpServerResponse exact;
    exact.headers.push_back(std::make_pair("X", "y"));
    kurlyk::detail::prepare_http_server_response(config, "GET", exact);
    CHECK(exact.status == 200);
    exact.headers.push_back(std::make_pair("X", "y"));
    kurlyk::detail::prepare_http_server_response(config, "GET", exact);
    CHECK(exact.status == 500);
    config.max_response_body_bytes = 1;
    kurlyk::HttpServerResponse large;
    large.body = "xx";
    kurlyk::detail::prepare_http_server_response(config, "GET", large);
    CHECK(large.status == 500 && large.body.empty());
    kurlyk::HttpServerResponse invalid;
    invalid.status = 199;
    kurlyk::detail::prepare_http_server_response(config, "GET", invalid);
    CHECK(invalid.status == 500);
}
} // namespace

int main() {
    routing_and_lifecycle();
    failure_and_destruction();
    response_policy();
}

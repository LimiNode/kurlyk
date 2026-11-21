#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_SERVER_DETAIL_BASIC_HTTP_SERVER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_SERVER_DETAIL_BASIC_HTTP_SERVER_HPP_INCLUDED

#include <kurlyk/http/server/data.hpp>
#include <atomic>
#include <map>
#include <memory>
#include <stdexcept>

namespace kurlyk {
namespace detail {

// Internal compile-time backend contract (not a public customization point):
// construct(config); set_handler(method, bool(owned_request, response&));
// start() -> bound port; run() -> stopped and drained; request_stop() noexcept.
// Backend invokes callbacks serially. False means abort without any response.
// State owns routing/one-shot flags; backend owns only transport resources.
template<class Backend>
class BasicHttpServer {
public:
    explicit BasicHttpServer(const HttpServerConfig& config) : m_state(std::make_shared<State>(config)) {}
    BasicHttpServer(const BasicHttpServer&) = delete;
    BasicHttpServer& operator=(const BasicHttpServer&) = delete;
    ~BasicHttpServer() { request_stop(); }

    void set_handler(const std::string& method, const std::string& path, HttpServerHandler handler) {
        auto state = m_state;
        if (state->m_started) throw std::logic_error("HTTP server already started");
        if (method.empty() || path.empty() || path[0] != '/' || !handler)
            throw std::invalid_argument("Invalid HTTP server route");
        state->m_routes[std::make_pair(method, path)] = std::move(handler);
    }

    unsigned short start() {
        auto state = m_state;
        if (state->m_started || state->m_stopping.load()) throw std::logic_error("HTTP server is one-shot");
        state->m_started = true;
        const std::weak_ptr<State> weak(state);
        const char* methods[] = {"GET", "POST", "HEAD", "PUT", "PATCH", "DELETE", "OPTIONS", "TRACE", "CONNECT"};
        for (const char* method : methods) state->install(method, weak);
        for (const auto& route : state->m_routes) state->install(route.first.first, weak);
        return state->m_backend.start();
    }

    void run() {
        auto state = m_state; // Runner retains backend and callbacks after facade destruction.
        if (!state->m_started || state->m_run_called.exchange(true))
            throw std::logic_error("HTTP server requires start and exactly one runner");
        state->m_backend.run();
    }

    void request_stop() noexcept {
        auto state = m_state;
        if (!state->m_stopping.exchange(true)) state->m_backend.request_stop();
    }

private:
    struct State {
        Backend m_backend;
        std::map<std::pair<std::string, std::string>, HttpServerHandler> m_routes;
        bool m_started = false;
        std::atomic<bool> m_run_called{false};
        std::atomic<bool> m_stopping{false};

        explicit State(const HttpServerConfig& config) : m_backend(config) {
            if (config.max_request_bytes == 0 || config.max_connections == 0 ||
                config.max_response_body_bytes == 0 || config.max_response_header_bytes == 0 ||
                config.request_timeout_seconds <= 0 || config.content_timeout_seconds <= 0)
                throw std::invalid_argument("HTTP server requires finite positive bounds");
        }

        void install(const std::string& method, const std::weak_ptr<State>& weak) {
            m_backend.set_handler(method, [weak](HttpServerRequest request, HttpServerResponse& response) {
                auto state = weak.lock();
                if (!state || state->m_stopping.load()) return false;
                response.status = 404;
                try {
                    auto route = state->m_routes.find(std::make_pair(request.method, request.path));
                    if (route != state->m_routes.end()) response = route->second(std::move(request));
                } catch (...) {
                    response = HttpServerResponse();
                    response.status = 500;
                }
                return true;
            });
        }
    };
    std::shared_ptr<State> m_state;
};

} // namespace detail
} // namespace kurlyk
#endif

#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_SERVER_BACKEND_SIMPLEWEB_SIMPLE_WEB_HTTP_SERVER_ADAPTER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_SERVER_BACKEND_SIMPLEWEB_SIMPLE_WEB_HTTP_SERVER_ADAPTER_HPP_INCLUDED

#include "HttpServerSocket.hpp"
#include <kurlyk/http/server/detail/response_policy.hpp>
#include <iterator>
#include <memory>

namespace kurlyk {
namespace detail {

class HttpServerBackend : public SimpleWeb::ServerBase<HttpServerSocket> {
public:
    explicit HttpServerBackend(const HttpServerConfig& config)
        : SimpleWeb::ServerBase<HttpServerSocket>(config.port),
          m_limit(config.max_connections), m_wire_limit(config.max_request_bytes), m_port(0) {}
    unsigned short port() const { return m_port; }
protected:
    void after_bind() override { m_port = acceptor->local_endpoint().port(); }
    void accept() override {
        auto connection = create_connection(*io_service, m_wire_limit);
        acceptor->async_accept(*connection->socket, [this, connection](const SimpleWeb::error_code& error) {
            auto runner = connection->handler_runner->continue_lock();
            if (!runner) return;
            if (!error) {
                bool admitted;
                {
                    SimpleWeb::LockGuard guard(connections->mutex);
                    admitted = connections->set.size() <= m_limit;
                }
                if (admitted) {
                    auto session = std::make_shared<Session>(config.max_request_streambuf_size, connection);
                    read(session);
                } else {
                    // No HTTP work or request buffer is allocated for overload.
                    SimpleWeb::error_code ignored;
                    connection->socket->close(ignored);
                }
            }
            if (error != SimpleWeb::error::operation_aborted && acceptor->is_open()) accept();
        });
    }
private:
    std::size_t m_limit;
    std::size_t m_wire_limit;
    unsigned short m_port;
};

// Concrete transport: no route table or independent lifecycle state machine.
class SimpleWebHttpServerAdapter {
public:
    typedef std::function<bool(HttpServerRequest, HttpServerResponse&)> Dispatch;

    explicit SimpleWebHttpServerAdapter(const HttpServerConfig& config)
        : m_config(config), m_io(std::make_shared<SimpleWeb::io_context>()), m_backend(config) {
        m_backend.io_service = m_io;
        m_backend.config.address = config.address;
        m_backend.config.max_request_streambuf_size = config.max_request_bytes;
        m_backend.config.timeout_request = config.request_timeout_seconds;
        m_backend.config.timeout_content = config.content_timeout_seconds;
        m_backend.on_error = [](std::shared_ptr<HttpServerBackend::Request>, const SimpleWeb::error_code&) {};
    }

    void set_handler(const std::string& method, Dispatch dispatch) {
        m_backend.default_resource[method] = [this, dispatch](std::shared_ptr<HttpServerBackend::Response> output,
                                                            std::shared_ptr<HttpServerBackend::Request> input) {
            output->close_connection_after_response = true;
            HttpServerResponse response;
            try {
                bool valid_framing = input->header.count("Content-Length") <= 1 && input->header.count("Transfer-Encoding") <= 1;
                const auto length = input->header.find("Content-Length");
                const auto encoding = input->header.find("Transfer-Encoding");
                if (length != input->header.end()) {
                    valid_framing = valid_framing && !length->second.empty() &&
                        length->second.find_first_not_of("0123456789") == std::string::npos && encoding == input->header.end();
                }
                if (encoding != input->header.end()) valid_framing = valid_framing && encoding->second == "chunked";
                if (!valid_framing) response.status = 400;
                else {
                    HttpServerRequest request;
                    request.method = input->method;
                    request.path = input->path;
                    request.query = input->query_string;
                    request.headers.assign(input->header.begin(), input->header.end());
                    request.body.assign(std::istreambuf_iterator<char>(input->content), std::istreambuf_iterator<char>());
                    if (!dispatch(std::move(request), response)) return;
                }
            } catch (...) {
                response = HttpServerResponse();
                response.status = 500;
            }
            write_response(output, input->method, response);
        };
    }

    unsigned short start() { m_backend.start(); return m_backend.port(); }

    void run() {
        try { m_io->run(); }
        catch (...) { m_backend.stop(); throw; }
        m_backend.stop();
        // Drain aborted network operations to release their buffers and sockets.
        SimpleWeb::restart(*m_io);
        m_io->run();
    }

    void request_stop() noexcept { m_io->stop(); }

private:
    void write_response(const std::shared_ptr<HttpServerBackend::Response>& output,
                        const std::string& method, HttpServerResponse& response) {
        const auto framing = prepare_http_server_response(m_config, method, response);
        *output << "HTTP/1.1 " << response.status << " Response\r\nConnection: close\r\n";
        for (const auto& header : response.headers) *output << header.first << ": " << header.second << "\r\n";
        if (framing.emit_content_length) *output << "Content-Length: " << framing.content_length << "\r\n";
        *output << "\r\n";
        if (framing.emit_body) output->write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
    }

    HttpServerConfig m_config;
    std::shared_ptr<SimpleWeb::io_context> m_io;
    HttpServerBackend m_backend;
};

} // namespace detail
} // namespace kurlyk
#endif

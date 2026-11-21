#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_SERVER_BACKEND_SIMPLEWEB_HTTP_SERVER_SOCKET_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_SERVER_BACKEND_SIMPLEWEB_HTTP_SERVER_SOCKET_HPP_INCLUDED

#include "headers.hpp"

namespace kurlyk {
namespace detail {
#ifdef ASIO_STANDALONE
namespace server_asio = ::asio;
#else
namespace server_asio = SimpleWeb::asio;
#endif

// Bound actual received bytes independently of declared lengths and chunk arithmetic.
class HttpServerSocket : public server_asio::ip::tcp::socket {
public:
    HttpServerSocket(SimpleWeb::io_context& io, std::size_t limit)
        : server_asio::ip::tcp::socket(io), m_io(io), m_remaining(limit) {}

    template<class Buffers, class Handler>
    void async_read_some(const Buffers& buffers, Handler handler) {
        auto it = server_asio::buffer_sequence_begin(buffers);
        const auto end = server_asio::buffer_sequence_end(buffers);
        while (it != end && server_asio::buffer_size(*it) == 0) ++it;
        if (it == end) {
            SimpleWeb::post(m_io, [handler]() mutable {
                handler(SimpleWeb::error_code(), 0);
            });
            return;
        }
        if (m_remaining == 0) {
            SimpleWeb::post(m_io, [handler]() mutable {
                handler(SimpleWeb::make_error_code::make_error_code(SimpleWeb::errc::message_size), 0);
            });
            return;
        }
        const server_asio::mutable_buffer buffer = server_asio::buffer(*it, m_remaining);
        server_asio::ip::tcp::socket::async_read_some(buffer,
            [this, handler](const SimpleWeb::error_code& error, std::size_t count) mutable {
                m_remaining -= count;
                handler(error, count);
            });
    }
private:
    SimpleWeb::io_context& m_io;
    std::size_t m_remaining;
};

} // namespace detail
} // namespace kurlyk
#endif

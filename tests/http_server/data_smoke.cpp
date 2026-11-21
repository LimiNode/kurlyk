#include <kurlyk/http/server/data.hpp>

// This translation unit has only the Kurlyk include root, never vendor paths.
#if defined(SERVER_HTTP_HPP) || defined(ASIO_HPP) || defined(ASIO_VERSION) || defined(BOOST_ASIO_VERSION)
#error HTTP server data contracts must not include a transport backend
#endif

int main() {
    kurlyk::HttpServerConfig config;
    kurlyk::HttpServerRequest request;
    request.body.assign("a\0b", 3);
    request.headers.push_back(std::make_pair("X-Token", "first"));
    request.headers.push_back(std::make_pair("X-Token", "second"));
    kurlyk::HttpServerHandler handler = [](kurlyk::HttpServerRequest owned) {
        kurlyk::HttpServerResponse response;
        response.body = std::move(owned.body);
        return response;
    };
    return config.max_request_bytes > 0 && request.headers.size() == 2 &&
        handler(request).body == std::string("a\0b", 3) ? 0 : 1;
}

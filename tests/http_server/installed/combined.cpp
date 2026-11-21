#define KURLYK_AUTO_INIT 0
#include <kurlyk.hpp>
#include <kurlyk/http_server.hpp>

int main() {
    kurlyk::HttpServer server;
    server.start();
    server.request_stop();
    server.run();
}

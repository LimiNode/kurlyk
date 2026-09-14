#define KURLYK_AUTO_INIT 0
#define KURLYK_WEBSOCKET_SUPPORT 0
#define KURLYK_AUTH_SUPPORT 0
#define KURLYK_OAUTH_SUPPORT 0

#include <kurlyk.hpp>

int main() {
    kurlyk::HttpRequest request;
    request.set_url("https://example.com", "/");
    return request.url == "https://example.com/" ? 0 : 1;
}

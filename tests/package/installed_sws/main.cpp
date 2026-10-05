#define KURLYK_AUTO_INIT 0
#define KURLYK_HTTP_SUPPORT 0
#define KURLYK_WEBSOCKET_SUPPORT 1

#include <kurlyk.hpp>

int main() {
    kurlyk::WebSocketClient client("ws://localhost:1");
    (void)client;
    return 0;
}

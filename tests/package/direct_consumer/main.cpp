#define KURLYK_AUTO_INIT 0
#define KURLYK_HTTP_SUPPORT 0
#define KURLYK_WEBSOCKET_SUPPORT 1

#include <kurlyk.hpp>

int main() {
    using WsClient = SimpleWeb::SocketClient<SimpleWeb::WS>;
    using WssClient = SimpleWeb::SocketClient<SimpleWeb::WSS>;

    return sizeof(WsClient) > 0 && sizeof(WssClient) > 0 ? 0 : 1;
}

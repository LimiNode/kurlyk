#pragma once
#ifndef KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_SIMPLE_WEB_SIMPLE_WEB_SOCKET_CLIENT_ADAPTER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_SIMPLE_WEB_SIMPLE_WEB_SOCKET_CLIENT_ADAPTER_HPP_INCLUDED

/// \file WebSocketClientAdapter.hpp
/// \brief Defines the SimpleWebSocketClientAdapter class, an adapter for managing WebSocket connections using the Simple-WebSocket-Server library.
/// This file declares the SimpleWebSocketClientAdapter class, which serves as an adapter
/// for managing WebSocket client connections with the Simple-WebSocket-Server library.
/// For more details on the library, see the Simple-Web-Server project on GitLab:
/// \see https://gitlab.com/eidheim/Simple-Web-Server

namespace kurlyk {

    /// \class SimpleWebSocketClientAdapter
    /// \brief A WebSocket client adapter that leverages the Simple WebSocket Server library for managing WebSocket connections.
    /// This class implements core WebSocket functionalities, such as connecting, sending messages, handling events, and
    /// managing the connection lifecycle with Simple WebSocket Server's `SocketClient` and `SocketClient<WS>` types.
    class SimpleWebSocketClientAdapter final : public BaseWebSocketClient {
    public:
        using WsClient  = SimpleWeb::SocketClient<SimpleWeb::WS>;
        using WssClient = SimpleWeb::SocketClient<SimpleWeb::WSS>;
        using BaseWebSocketClient::send_close;
        using BaseWebSocketClient::send_message;

        /// \brief Constructs the WebSocket client and initializes the io_context.
        SimpleWebSocketClientAdapter() :
                BaseWebSocketClient() {
            m_io_context = SimpleWebSocketWorker::get_instance().get_io_context();
            SimpleWebSocketWorker::get_instance().start();
        }

        /// \brief Default destructor for cleanup.
        virtual ~SimpleWebSocketClientAdapter() = default;

        SimpleWebSocketClientAdapter(const SimpleWebSocketClientAdapter&) = delete;
        void operator = (const SimpleWebSocketClientAdapter&) = delete;

        /// \brief Retrieves the HTTP version used in the WebSocket connection.
        /// \return The HTTP version string.
        std::string get_http_version() override final {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            if (m_wss_connection) return m_wss_connection->http_version;
            if (m_ws_connection) return m_ws_connection->http_version;
            return std::string();
        }

        /// \brief Retrieves the headers associated with the WebSocket connection.
        /// \return A Headers object containing the HTTP headers.
        Headers get_headers() override final {
            Headers headers;
            std::lock_guard<std::mutex> lock(m_client_mutex);
            if (m_wss_connection){
                copy_headers(m_wss_connection->header, headers);
                return headers;
            }
            if (m_ws_connection){
                copy_headers(m_ws_connection->header, headers);
                return headers;
            }
            return Headers();
        }

        /// \brief Retrieves the remote endpoint information.
        /// \return The remote endpoint as a string in the format "IP:Port".
        std::string get_remote_endpoint() override final {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            if (m_wss_connection) return endpoint_to_string(m_wss_connection);
            if (m_ws_connection) return endpoint_to_string(m_ws_connection);
            return std::string();
        }

    private:
        std::mutex                              m_client_mutex;
        std::shared_ptr<SimpleWeb::io_context>  m_io_context;
        std::shared_ptr<WsClient>               m_ws_client;
        std::shared_ptr<WssClient>              m_wss_client;
        std::shared_ptr<WsClient::Connection>   m_ws_connection;
        std::shared_ptr<WssClient::Connection>  m_wss_connection;

#       ifdef BOOST_ASIO_VERSION
        /// \brief Converts a Boost error code to a standard error code.
        /// \param boost_ec The Boost error code.
        /// \return The equivalent standard error code.
        std::error_code convert_boost_to_std(const boost::system::error_code& boost_ec) {
            return std::error_code(boost_ec.value(), std::generic_category());
        }
#       endif

        /// \brief Initializes and starts a WebSocket connection.
        /// \return True if initialization was successful, false otherwise.
        bool init_websocket() override final {
            // Проверяем инициализацию конфигурации и контекста
            const size_t MIN_URL_SIZE = 6;
            if (!m_io_context ||
                !m_config ||
                m_config->url.size() < MIN_URL_SIZE) {
                return false;
            }

            std::string protocol = utils::extract_protocol(m_config->url);
            if (protocol != "wss" && protocol != "ws") {
                return false;
            }

            try {
                std::lock_guard<std::mutex> lock(m_client_mutex);
                if (protocol == "wss") {
                    init_client<
                        WssClient,
                        WssClient::Connection,
                        WssClient::InMessage>();
                } else {
                    init_client<
                        WsClient,
                        WsClient::Connection,
                        WsClient::InMessage>();
                }
            } catch(...) {
                return false;
            }

            return true;
        }

        /// \brief Deinitializes the WebSocket connection.
        void deinit_websocket() override final {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            m_wss_client.reset();
            m_ws_client.reset();
            m_ws_connection.reset();
            m_wss_connection.reset();
        }

        /// \brief Sends a WebSocket message.
        /// \param send_info Information about the message, including callback and rate limit.
        void send_message(std::shared_ptr<WebSocketSendInfo>& send_info) override final {
            std::unique_lock<std::mutex> lock(m_client_mutex);
            if (!m_wss_connection && !m_ws_connection) {
                lock.unlock();
                if (!send_info->callback) return;
                send_info->callback(utils::make_error_code(utils::ClientError::NotConnected));
                return;
            }
            if (m_wss_connection) send_message(m_wss_connection, send_info);
            else send_message(m_ws_connection, send_info);
        }

        /// \brief Sends a WebSocket close request.
        /// \param send_info Information about the close request, including callback and status.
        void send_close(std::shared_ptr<WebSocketSendInfo>& send_info) override final {
            std::unique_lock<std::mutex> lock(m_client_mutex);
            if (!m_wss_connection && !m_ws_connection) {
                lock.unlock();
                if (!send_info->callback) return;
                send_info->callback(utils::make_error_code(utils::ClientError::NotConnected));
                return;
            }
            if (m_wss_connection) send_close(m_wss_connection, send_info);
            else send_close(m_ws_connection, send_info);
        };

        /// \brief Helper to send a message on a specific connection type.
        /// \tparam ConnectionType The connection type (`WsClient` or `WssClient`).
        /// \param connection The connection to send the message on.
        /// \param send_info Information about the message, including callback.
        template<class ConnectionType>
        void send_message(
                const ConnectionType& connection,
                const std::shared_ptr<WebSocketSendInfo>& send_info) {
            connection->send(
                    send_info->message,
                    [this, send_info](const SimpleWeb::error_code &ec) {
                if (!send_info->callback) return;
#               ifdef ASIO_STANDALONE
                add_send_callback(ec, send_info->callback);
#               else
                add_send_callback(convert_boost_to_std(ec), send_info->callback);
#               endif
            });
        }

        /// \brief Helper to send a close request on a specific connection type.
        /// \tparam ConnectionType The connection type (`WsClient` or `WssClient`).
        /// \param connection The connection to send the close request on.
        /// \param send_info Information about the close request, including callback.
        template<class ConnectionType>
        void send_close(
                const ConnectionType& connection,
                const std::shared_ptr<WebSocketSendInfo>& send_info) {
            connection->send_close(
                    send_info->status,
                    send_info->message,
                    [this, send_info](const SimpleWeb::error_code& ec) {
                if (!send_info->callback) return;
#               ifdef ASIO_STANDALONE
                add_send_callback(ec, send_info->callback);
#               else
                add_send_callback(convert_boost_to_std(ec), send_info->callback);
#               endif
            });
        }

        /// \brief Initializes a WebSocket client based on the provided type and sets up callbacks.
        template<class ClientType, class ConnectionType, class MessageType>
        void init_client() {
            auto client = create_client<ClientType>();
            client->io_service = m_io_context;
            client->config.timeout_idle = m_config->idle_timeout;
            client->config.timeout_request = m_config->request_timeout;

            if (!m_config->headers.empty()) {
                copy_headers(m_config->headers, client->config.header);
            }
            if (!m_config->user_agent.empty() &&
                m_config->headers.find("User-Agent") == m_config->headers.end()) {
                client->config.header.insert({"User-Agent", m_config->user_agent});
            }
            if (!m_config->accept_encoding.empty() &&
                m_config->headers.find("Accept-Encoding") == m_config->headers.end()) {
                client->config.header.insert({"Accept-Encoding", m_config->accept_encoding});
            }
            if (!m_config->cookie.empty() &&
                m_config->headers.find("Cookie") == m_config->headers.end()) {
                client->config.header.insert({"Cookie", m_config->cookie});
            }
            if (!m_config->proxy_server.empty()) {
                client->config.proxy_server = m_config->proxy_server;
            }
            if (!m_config->proxy_auth.empty()) {
                client->config.proxy_auth = m_config->proxy_auth;
            }

            // Настираиваем фунции обратного вызова
            client->on_open = [this](std::shared_ptr<ConnectionType> connection) {
                std::lock_guard<std::mutex> lock(m_client_mutex);
                init_connection<ConnectionType>(connection);
                add_fsm_event(FsmEvent::ConnectionOpened, create_websocket_open_event(connection));
            };

            client->on_message = [this](
                    std::shared_ptr<ConnectionType> connection,
                    std::shared_ptr<MessageType> message) {
                add_fsm_event(FsmEvent::MessageReceived, create_websocket_message_event(message));
            };

            client->on_close = [this](
                    std::shared_ptr<ConnectionType> connection,
                    const int status,
                    const std::string &reason) {
                add_fsm_event(FsmEvent::ConnectionClosed, create_websocket_close_event(reason, status));
            };

            // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
            client->on_error = [this](
                    std::shared_ptr<ConnectionType> connection,
                    const SimpleWeb::error_code &ec) {
                add_fsm_event(FsmEvent::ConnectionError, create_websocket_error_event(ec));
            };

            client->start();
            SimpleWebSocketWorker::get_instance().notify();
        }

        template<class T>
        std::shared_ptr<T> create_client(
                typename std::enable_if<std::is_same<T, WssClient>::value>::type* = 0) {
            if (m_ws_client) m_ws_client.reset();
            m_wss_client = std::make_shared<T>(
                utils::remove_ws_prefix(m_config->url),
                m_config->verify_cert,
                m_config->cert_file,
                m_config->key_file,
                m_config->ca_file);
            return m_wss_client;
        }

        template<class T>
        std::shared_ptr<T> create_client(
                typename std::enable_if<std::is_same<T, WsClient>::value>::type* = 0) {
            if (m_wss_client) m_wss_client.reset();
            m_ws_client = std::make_shared<T>(utils::remove_ws_prefix(m_config->url));
            return m_ws_client;
        }

        template<class ConnectionType>
        std::unique_ptr<WebSocketEventData> create_websocket_open_event(
                std::shared_ptr<ConnectionType>& connection) {
            auto websocket_event = create_websocket_event();
            websocket_event->event_type  = WebSocketEventType::WS_OPEN;
            websocket_event->status_code = static_cast<long>(SimpleWeb::status_code(connection->status_code));
            return websocket_event;
        }

        template<class MessageType>
        std::unique_ptr<WebSocketEventData> create_websocket_message_event(
                std::shared_ptr<MessageType>& message) {
            auto websocket_event = create_websocket_event();
            websocket_event->event_type = WebSocketEventType::WS_MESSAGE;
            websocket_event->message    = message->string();
            return websocket_event;
        }

        template<class T>
        void init_connection(
                std::shared_ptr<T> &connection,
                typename std::enable_if<std::is_same<T, WssClient::Connection>::value>::type* = 0) {
            m_wss_connection = connection;
        }

        template<class T>
        void init_connection(
                std::shared_ptr<T> &connection,
                typename std::enable_if<std::is_same<T, WsClient::Connection>::value>::type* = 0) {
             m_ws_connection = connection;
        }

        template<class ConnectionType>
        std::string endpoint_to_string(const std::shared_ptr<ConnectionType>& connection) const {
            return connection->remote_endpoint().address().to_string() + ":" +
                   std::to_string(connection->remote_endpoint().port());
        }

        template<typename SrcMap, typename DstMap>
        void copy_headers(const SrcMap& src, DstMap& dst) {
            dst.clear();
            for (const auto& header : src) {
                dst.insert(header);
            }
        }
    };

}; // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_SIMPLE_WEB_SIMPLE_WEB_SOCKET_CLIENT_ADAPTER_HPP_INCLUDED

/*
* kurlyk - C++ library for easy HTTP requests
*
* Copyright (c) 2021-2024 Elektro Yar. Email: git.electroyar@gmail.com
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#pragma once
#ifndef KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_EMSCRIPTEN_EMSCRIPTEN_WEB_SOCKET_CLIENT_ADAPTER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_EMSCRIPTEN_EMSCRIPTEN_WEB_SOCKET_CLIENT_ADAPTER_HPP_INCLUDED

#include <functional>
#include <string>
#include <emscripten/fetch.h>
#include <emscripten/websocket.h>
#include <system_error>

namespace kurlyk {

    /// \class EmscriptenWebSocketClientAdapter
    /// \brief A WebSocket client implementation using the Emscripten API.
    class EmscriptenWebSocketClientAdapter {
    public:

        /// \brief Constructor: Initializes the WebSocket client.
        EmscriptenWebSocketClientAdapter() :
                IWebSocketClient() {
        }

        /// \brief Destructor: Cleans up resources and resets the WebSocket client state.
        ~EmscriptenWebSocketClientAdapter() override final {
            reset();
        }

        EmscriptenWebSocketClientAdapter(const EmscriptenWebSocketClientAdapter&) = delete;
        void operator = (const EmscriptenWebSocketClientAdapter&) = delete;

        /// \brief Accessor for the event handler function.
        ///
        /// This method provides access to the event handler function used for handling WebSocket events. The returned
        /// reference allows getting or setting the function that will be called when a WebSocket event occurs.
        ///
        /// \return A reference to a `std::function<void(std::unique_ptr<WebSocketEventData>)>` representing the event handler function.
        std::function<void(std::unique_ptr<WebSocketEventData>)>& event_handler() override final {
            return m_on_event;
        }

        /// \brief Sets the WebSocket configuration.
        /// \param config The configuration object to set.
        /// \return True if the configuration was successfully updated.
        bool set_config(std::unique_ptr<WebSocketConfig> config) override final {
            std::lock(m_client_mutex, m_config_mutex);
            std::lock_guard<std::mutex> lock1(m_client_mutex, std::adopt_lock);
            std::lock_guard<std::mutex> lock2(m_config_mutex, std::adopt_lock);

            m_pending_config = std::move(config);
            m_is_config_updated = true;

            // If the connection is active, update the configuration immediately.
            if (m_is_connection_active) {
                m_is_running = true;
                m_fsm_event_queue.push_event(FsmEvent::UpdateConfig);
            }
            return true;
        }

        /// \brief Initiate connection to the WebSocket server.
        void connect() override final {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            m_fsm_event_queue.push_event(FsmEvent::RequestConnect);
            m_is_connection_active = true;
            m_is_running = true;
        }

        /// \brief Disconnect from the WebSocket server.
        void disconnect() override final {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            m_fsm_event_queue.push_event(FsmEvent::RequestDisconnect);
            m_is_connection_active = false;
        }

        /// \brief Checks if the WebSocket is connected.
        /// \return True if the WebSocket is connected.
        bool is_connected() override final {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            return m_ws_state == WebSocketState::WORKING;
        }

        /// \brief Checks if the client is currently running.
        /// \return True if the client is running.
        bool is_running() override final {
            return m_is_running;
        }

        /// \brief Send a message through the WebSocket.
        /// \param message The message to send.
        /// \param rate_limit_type The rate limit type to apply.
        /// \param callback The callback to be invoked after sending.
        /// \return True if the message was successfully queued, false otherwise.
        bool send_message(
                const std::string &message,
                const RateLimitType& rate_limit_type = RateLimitType::General,
                std::function<void(const std::error_code& ec)> callback = nullptr) override final {
            if (message.empty()) return false;
            if (!is_connected()) return false;
            std::lock_guard<std::mutex> lock(m_message_queue_mutex);
#           if __cplusplus >= 201402L
            m_message_queue.push_back(std::make_shared<WebSocketSendInfo>(message, rate_limit_type, false, 0, callback));
#           else
            m_message_queue.push_back(std::shared_ptr<WebSocketSendInfo>(new WebSocketSendInfo(message, rate_limit_type, false, 0, callback)));
#           endif
            return true;
        }

        /// \brief Send a close request through the WebSocket.
        /// \param status The status code to send with the close request.
        /// \param reason The reason for closing the connection.
        /// \param callback The callback to be invoked after sending.
        /// \return True if the close request was successfully queued, false otherwise.
        bool send_close(
                const int status = 1000,
                const std::string &reason = std::string(),
                std::function<void(const std::error_code& ec)> callback = nullptr) override final {
            if (!is_connected()) return false;
            std::lock_guard<std::mutex> lock(m_message_queue_mutex);
#           if __cplusplus >= 201402L
            m_message_queue.push_back(std::make_shared<WebSocketSendInfo>(reason, RateLimitType::General, true, status, callback));
#           else
            m_message_queue.push_back(std::shared_ptr<WebSocketSendInfo>(new WebSocketSendInfo(reason, RateLimitType::General, true, status, callback)));
#           endif
            return true;
        }

        /// \brief Retrieve all pending events.
        /// \return A list of unique pointers to WebSocket events.
        std::list<std::unique_ptr<WebSocketEventData>> receive_events() override final {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            auto event_queue = std::move(m_event_queue);
            m_event_queue.clear();
            return event_queue;
        }

        /// \brief Retrieve the next pending event.
        /// \return A unique pointer to the next WebSocket event, or nullptr if no events are available.
        std::unique_ptr<WebSocketEventData> receive_event() override final {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            if (m_event_queue.empty()) return nullptr;
            std::unique_ptr<WebSocketEventData> event = std::move(*m_event_queue.begin());
            m_event_queue.erase(m_event_queue.begin());
            return event;
        }

        /// \brief Process pending operations such as connecting, sending, and handling events.
        void process() override final {
            process_update_config();
            process_fsm_state();
            process_message_queue();
            process_handle_event();
        }

        /// \brief Reset the WebSocket client state.
        void reset() override final {
            reset_websocket();
            process_handle_event();
        }

    private:
        using time_point_t      = std::chrono::steady_clock::time_point;
        using event_data_ptr_t  = std::unique_ptr<WebSocketEventData>;
        using send_info_ptr_t   = std::shared_ptr<WebSocketSendInfo>;

        /// \brief State for the WebSocket connection.
        enum class WebSocketState {
            START,              ///< Initialization.
            CONNECTING,         ///< Waiting for connection establishment.
            WORKING,            ///< Connection is active.
            DISCONNECTING,      ///< Waiting for disconnection.
            STOPPED             ///< Connection stopped.
        } m_ws_state = WebSocketState::START;

        /// \brief Finite state machine states controlling client workflow.
        enum class FsmState {
            INIT,               ///< Initialization.
            CONNECTING,         ///< Waiting for connection.
            WORKING,            ///< Connection active.
            RECONNECTING,       ///< Attempting reconnection.
            STOPPED             ///< Stopped.
        } m_fsm_state = FsmState::INIT;

        /// \brief Represents events in the finite state machine.
        enum class FsmEvent {
            RequestConnect,     ///< Request to connect.
            RequestDisconnect,  ///< Request to disconnect.
            ConnectionOpened,   ///< Connection opened.
            ConnectionClosed,   ///< Connection closed.
            ConnectionError,    ///< Connection error occurred.
            UpdateConfig        ///< Configuration update requested.
        };

        std::function<void(std::unique_ptr<WebSocketEventData>)> m_on_event;
        utils::EventQueue<FsmEvent> m_fsm_event_queue;
        std::unique_ptr<WebSocketConfig>        m_config;
        std::unique_ptr<WebSocketConfig>        m_pending_config;
        std::mutex                              m_config_mutex;
        bool                                    m_is_config_updated  = false;

        //
        WebSocketRateLimiter            m_rate_limiter;
        time_point_t                    m_start_time;
        EMSCRIPTEN_WEBSOCKET_T          m_client_ws;
        std::mutex                      m_client_mutex;

        std::shared_ptr<WsClient::Connection>   m_ws_connection;
        std::shared_ptr<WssClient::Connection>  m_wss_connection;
        std::list<event_data_ptr_t>     m_event_queue;

        std::mutex                      m_message_queue_mutex;
        std::list<send_info_ptr_t>      m_message_queue;

        long                            m_reconnect_attempt = 0;
        std::atomic<bool>               m_is_connection_active = ATOMIC_VAR_INIT(false);
        std::atomic<bool>               m_is_running = ATOMIC_VAR_INIT(false);

        bool init_websocket() {
            // Проверка поддержки Websocket
            if (!emscripten_websocket_is_supported()) {
                send_event_websocket_error(std::error_code(10022, std::system_category()));
                return false;
            }

            // Валидация url
            if (!utils::is_valid_url(m_config->url, {"ws", "wss"})) {
                send_event_websocket_error(std::error_code(10022, std::system_category()));
                return false;
            }

            // Инициализация протоколов

            // Добавляем протоколы, если они уже есть в m_config->protocols
            std::vector<const char*> protocols_c_str;
            for (const auto& protocol : m_config->protocols) {
                protocols_c_str.push_back(protocol.c_str());
            }

            // Если нет протоколов в m_config, пытаемся извлечь их из заголовка "Sec-WebSocket-Protocol"
            if (m_config->m_config->protocols.empty() &&
                m_config->headers.find("Sec-WebSocket-Protocol") != m_config->headers.end()) {
                // Извлекаем строку протоколов
                std::string protocol_header = m_config->headers["Sec-WebSocket-Protocol"];

                // Разбиваем строку по запятым
                std::stringstream ss(protocol_header);
                std::string protocol;
                while (std::getline(ss, protocol, ',')) {
                    // Удаляем лишние пробелы в начале и конце протоколов
                    protocol.erase(0, protocol.find_first_not_of(' '));
                    protocol.erase(protocol.find_last_not_of(' ') + 1);

                    protocols_c_str.push_back(protocol.c_str());
                }
            }

            protocols_c_str.push_back(nullptr);

            EmscriptenWebSocketCreateAttributes ws_attrs = {
                m_config->url.c_str(),
                protocols_c_str.data(),
                EM_TRUE
            };

            m_client_ws = emscripten_websocket_new(&ws_attrs);
            if (m_client_ws <= 0) {
                send_event_websocket_error(std::error_code(10022, std::system_category()));
                return false;
            }

            emscripten_websocket_set_onopen_callback(m_client_ws, this, on_open_cb);
            emscripten_websocket_set_onerror_callback(m_client_ws, this, on_close_cb);
            emscripten_websocket_set_onclose_callback(m_client_ws, this, on_error_cb);
            emscripten_websocket_set_onmessage_callback(m_client_ws, this, on_message_cb);
            return true;
        }

        void reset_websocket() {
            std::unique_lock<std::mutex> lock_message(m_message_queue_mutex);
            m_message_queue.clear();
            lock_message.unlock();

            std::unique_lock<std::mutex> lock_client(m_client_mutex);
            emscripten_websocket_delete(m_client_ws);

            if ((m_is_connection_active &&
                 m_ws_state != WebSocketState::DISCONNECTING &&
                 m_ws_state != WebSocketState::STOPPED) ||
                m_ws_state == WebSocketState::WORKING ||
                m_ws_state == WebSocketState::CONNECTING) {
                m_ws_state = WebSocketState::STOPPED;
                lock_client.unlock();
                //send_event_websocket_error(std::make_error_code(std::errc::connection_reset));
                send_event_websocket_error(std::error_code(995, std::system_category()));
            }
        }

        /// \brief Process the queue of messages waiting to be sent.
        void process_message_queue() {
            if (m_client_ws <= 0) return;

            std::unique_lock<std::mutex> lock(m_message_queue_mutex);
            if (m_message_queue.empty()) return;

            std::list<send_info_ptr_t> message_queue;
            auto it = m_message_queue.begin();
            while (it != m_message_queue.end()) {
                auto& send_info = *it;
                // Check if the message is allowed by the rate limiter.
                if (!m_rate_limiter.allow_request(send_info->rate_limit_type)) {
                    ++it;
                    continue;
                }
                message_queue.push_back(std::move(send_info));
                it = m_message_queue.erase(it);
            }
            lock.unlock();
            if (message_queue.empty()) return;

            // Обрабатываем очередь сообщений на отправку
            EMSCRIPTEN_RESULT result;
            for (const auto &send_info : message_queue) {
                std::unique_lock<std::mutex> lock(m_client_mutex);
                if ((!m_wss_connection && !m_ws_connection) || m_ws_state != WebSocketState::WORKING) {
                    lock.unlock();
                    if (!send_info->callback) continue;
                    send_info->callback(std::make_error_code(std::errc::not_connected));
                    continue;
                }
                lock.unlock();

                // Закрываем соединение
                if (send_info->is_send_close) {
                    std::unique_lock<std::mutex> lock(m_client_mutex);
                    result = emscripten_websocket_close(m_client_ws, send_info->status, "normal closure");
                    lock.unlock();
                    if (!send_info->callback) continue;
                    if (result != EMSCRIPTEN_RESULT_SUCCESS) {
                        send_info->callback(std::make_error_code(std::errc::not_connected));
                    } else {
                        std::error_code ec;
                        send_info->callback(ec);
                    }
                    continue;
                }

                // Отправляем сообщение
                std::unique_lock<std::mutex> lock(m_client_mutex);
                result = emscripten_websocket_send_binary(m_client_ws, send_info->message.c_str(), send_info->message.size());
                lock.unlock();
                if (!send_info->callback) continue;
                if (result != EMSCRIPTEN_RESULT_SUCCESS) {
                    send_info->callback(std::make_error_code(std::errc::not_connected));
                } else {
                    std::error_code ec;
                    send_info->callback(ec);
                }
            }
        }

        /// \brief Process the events queue.
        void process_handle_event() {
            if (!m_on_event) return;

            std::unique_lock<std::mutex> lock(m_client_mutex);
            if (m_event_queue.empty()) return;
            auto event_queue = std::move(m_event_queue);
            m_event_queue.clear();
            lock.unlock();

            for (auto &event : event_queue) {
                m_on_event(std::move(event));
            }
        }

        void process_fsm_state() {
            std::unique_lock<std::mutex> lock(m_client_mutex);
            ws_state = m_ws_state;
            lock.unlock();

            switch (ws_state) {
            case WebSocketState::START: {
                process_fsm_start();
                break;
            }
            case WebSocketState::CONNECTING: {

                break;
            }
            case WebSocketState::WORKING: {

                break;
            }
            case WebSocketState::DISCONNECTING: {

                break;
            }
            default:

                break;
            };
        }

        void process_fsm_start() {
            if (!m_fsm_event_queue.has_events()) return;
            switch (m_fsm_event_queue.pop_event()) {
            case FsmEvent::RequestConnect:
                if (!init_websocket()) {
                    m_fsm_state = FsmState::STOPPED;
                    break;
                }
                m_fsm_state = FsmState::CONNECTING;
                break;
            default:
                break;
            };
        }

        void process_fsm_connecting() {
            if (!m_fsm_event_queue.has_events()) return;
            switch (m_fsm_event_queue.pop_event()) {
            case FsmEvent::ConnectionOpened:
                m_reconnect_attempt = 0;
                m_fsm_state = FsmState::WORKING;
                break;
            case FsmEvent::ConnectionClosed:
            case FsmEvent::ConnectionError:
                reset_websocket();
                if (!m_config) {
                    m_fsm_state = FsmState::STOPPED;
                    break;
                }
                if (!m_config->reconnect) {
                    m_fsm_state = FsmState::INIT;
                    break;
                }
                ++m_reconnect_attempt;
                m_start_time = std::chrono::steady_clock::now();
                m_fsm_state = FsmState::RECONNECTING;
                break;
            case FsmEvent::RequestDisconnect:
                reset_websocket();
                m_reconnect_attempt = 0;
                m_fsm_state = FsmState::INIT;
                break;
            case FsmEvent::UpdateConfig:
                reset_websocket();
                m_reconnect_attempt = 0;
                if (!init_websocket()) {
                    m_fsm_state = FsmState::STOPPED;
                    break;
                }
                m_fsm_state = FsmState::CONNECTING;
                break;
            default:
                break;
            };
        }

        //----------------------------------------------------------------------

        static EM_BOOL on_open_cb(
                int event_type,
                const EmscriptenWebSocketOpenEvent* event_data,
                void* user_data) {
            auto* client = static_cast<EmscriptenWebSocketClientAdapter*>(user_data);
            if (client) client->on_open(event_data);
            return EM_TRUE;
        }

        static EM_BOOL on_close_cb(
                int event_type,
                const EmscriptenWebSocketCloseEvent* event_data,
                void* user_data) {
            auto* client = static_cast<EmscriptenWebSocketClientAdapter*>(user_data);
            if (client) client->on_close(event_data);
            return EM_TRUE;
        }

        static EM_BOOL on_error_cb(
                int event_type,
                const EmscriptenWebSocketErrorEvent* event_data,
                void* user_data) {
            auto* client = static_cast<EmscriptenWebSocketClientAdapter*>(user_data);
            if (client) client->on_error(event_data);
            return EM_TRUE;
        }

        static EM_BOOL on_message_cb(
                int event_type,
                const EmscriptenWebSocketMessageEvent* event_data,
                void* user_data) {
            auto* client = static_cast<EmscriptenWebSocketClientAdapter*>(user_data);
            if (client) client->on_message(event_data);
            return EM_TRUE;
        }

        //----------------------------------------------------------------------

        void on_open(const EmscriptenWebSocketOpenEvent* event_data) {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            m_ws_state = WebSocketState::WORKING;
            m_fsm_event_queue.push_event(FsmEvent::ConnectionOpened);
            m_event_queue.push_back(create_websocket_event(event_data));
        }

        void on_message(const EmscriptenWebSocketMessageEvent* event_data) {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            m_event_queue.push_back(create_websocket_event(event_data));
        }

        void on_error(const EmscriptenWebSocketErrorEvent* event_data) {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            if (m_ws_state != WebSocketState::DISCONNECTING) {
                m_event_queue.push_back(create_websocket_event(event_data));
            }
            m_ws_state = WebSocketState::DISCONNECTING;
            m_fsm_event_queue.push_event(FsmEvent::ConnectionError);
        }

        void on_close(const EmscriptenWebSocketCloseEvent* event_data) {
            std::lock_guard<std::mutex> lock(m_client_mutex);
            m_ws_state = WebSocketState::DISCONNECTING;
            m_fsm_event_queue.push_event(FsmEvent::ConnectionClosed);
            m_event_queue.push_back(create_websocket_event(event_data));
        }

        //----------------------------------------------------------------------


        std::unique_ptr<WebSocketEventData> create_websocket_event() {
#           if __cplusplus >= 201402L
            auto event = std::make_unique<WebSocketEventData>();
#           else
            auto event = std::unique_ptr<WebSocketEventData>(new WebSocketEventData());
#           endif
            event->sender = shared_from_this();
            return event;
        }

        std::unique_ptr<WebSocketEventData> create_websocket_event(
                const EmscriptenWebSocketOpenEvent* event_data) {
            auto event = create_websocket_event();
            event->event_type   = WebSocketEventType::Open;
            return event;
        }

        std::unique_ptr<WebSocketEventData> create_websocket_event(
                const EmscriptenWebSocketMessageEvent* event_data) {
            auto event = create_websocket_event();
            event->event_type = WebSocketEventType::Message;
            event->message.assign(event_data->data, event_data->numBytes);
            return event;
        }

        std::unique_ptr<WebSocketEventData> create_websocket_event(
                const EmscriptenWebSocketErrorEvent* event_data) {
            auto event = create_websocket_event();
            event->event_type = WebSocketEventType::Error;
            event->error_code = std::error_code(995, std::system_category());
            return event;
        }

        std::unique_ptr<WebSocketEventData> create_websocket_event(
                const EmscriptenWebSocketCloseEvent* event_data) {
            auto event = create_websocket_event();
            event->event_type   = WebSocketEventType::Close;
            event->reason       = event_data->reason;
            event->status_code  = event_data->code;
            return event;
        }
    };

};

#endif // KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_EMSCRIPTEN_EMSCRIPTEN_WEB_SOCKET_CLIENT_ADAPTER_HPP_INCLUDED

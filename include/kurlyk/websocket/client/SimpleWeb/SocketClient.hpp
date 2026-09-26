#pragma once
#ifndef KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_SIMPLE_WEB_SOCKET_CLIENT_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_SIMPLE_WEB_SOCKET_CLIENT_HPP_INCLUDED

/// \file SocketClient.hpp
/// \brief Defines a WebSocket client with SSL support, extending the functionality of the Simple-Web-Server library.
///
/// This file contains the definition of the `SocketClient<WSS>` class, which is a modification of the WebSocket client
/// from the Simple-Web-Server library to support SSL/TLS connections. It adds the ability to load root certificates
/// from the system using the `add_root_certificates()` method.
/// For more details on the Simple-Web-Server library, see:
/// \see https://gitlab.com/eidheim/Simple-Web-Server

#include "../../../config.hpp"
#include <client_ws.hpp>

#ifdef ASIO_STANDALONE
#include <asio/ssl.hpp>
#else
#include <boost/asio/ssl.hpp>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace SimpleWeb {
    using WSS = asio::ssl::stream<asio::ip::tcp::socket>;

    /// \brief A WebSocket client with SSL support.
    ///
    /// This class extends the `SocketClientBase<WSS>` class to provide SSL/TLS support for WebSocket connections.
    /// It allows you to connect to a WebSocket server over a secure connection using SSL/TLS. The class also
    /// includes functionality to load root certificates from the system to facilitate secure connections.
    template <>
    class SocketClient<WSS> : public SocketClientBase<WSS> {
    public:

        /// \brief Constructs a WebSocket client with SSL/TLS support.
        ///
        /// \param server_port_path   Server resource specified by host[:port][/path].
        /// \param verify_certificate Set to true (default) to enable verification of the server's certificate and hostname according to RFC 2818.
        /// \param certification_file If provided, specifies the file containing the client certificate to use. Requires private_key_file.
        /// \param private_key_file   If provided, specifies the file containing the private key corresponding to the client certificate. Requires certification_file.
        /// \param verify_file        If provided, specifies a certificate authority file for verification.
        SocketClient(const std::string &server_port_path, bool verify_certificate = true,
                     const std::string &certification_file = std::string(), const std::string &private_key_file = std::string(),
                     const std::string &verify_file = std::string())
            : SocketClientBase<WSS>::SocketClientBase(server_port_path, 443),
#           if (defined(ASIO_STANDALONE) && ASIO_VERSION >= 103300) || BOOST_ASIO_VERSION >= 103300
                context(asio::ssl::context::tls_client) {
            // Disabling TLS 1.0 and 1.1 (see RFC 8996)
            context.set_options(asio::ssl::context::no_tlsv1);
            context.set_options(asio::ssl::context::no_tlsv1_1);
#           else
                context(asio::ssl::context::tlsv12) {
#           endif

            if(certification_file.size() > 0 && private_key_file.size() > 0) {
                context.use_certificate_chain_file(certification_file);
                context.use_private_key_file(private_key_file, asio::ssl::context::pem);
            }

            if(verify_certificate) {
#				if (defined(ASIO_STANDALONE) && ASIO_VERSION >= 103300) || BOOST_ASIO_VERSION >= 103300
                context.set_verify_callback(asio::ssl::host_name_verification(host));
#               else
                context.set_verify_callback(asio::ssl::rfc2818_verification(host));
#               endif
            }

            if(verify_file.size() > 0) {
                context.load_verify_file(verify_file);
            } else {
                add_root_certificates();
            }

            if(verify_certificate) {
                context.set_verify_mode(asio::ssl::verify_peer);
            } else {
                context.set_verify_mode(asio::ssl::verify_none);
            }
        };

    private:

        /// \brief Adds root certificates from the system to the SSL context.
        ///
        /// On Windows, this method loads certificates from the system ROOT store.
        /// On Unix-like systems, it uses OpenSSL default verify paths.
        void add_root_certificates() {
#ifdef _WIN32
            X509_STORE* store = ::SSL_CTX_get_cert_store(context.native_handle());
            HCERTSTORE hCertStore = CertOpenSystemStore(0LL, "ROOT");
            if (!hCertStore) {
                return;
            }

            PCCERT_CONTEXT certContext = nullptr;
            while (true) {
                certContext = CertEnumCertificatesInStore(hCertStore, certContext);
                if (!certContext) {
                    break;
                }
                const unsigned char* encoded = certContext->pbCertEncoded;
                X509* x509 = d2i_X509(nullptr, &encoded, certContext->cbCertEncoded);
                if (x509) {
                    X509_STORE_add_cert(store, x509);
                    X509_free(x509);
                }
            }
            CertCloseStore(hCertStore, 0);
#else
            context.set_default_verify_paths();
#endif
        }

    protected:
        asio::ssl::context context;

        /// \brief Initiates the connection process.
        ///
        /// This method starts the process of connecting to the WebSocket server, including resolving the server
        /// address and establishing the connection. It handles both direct and proxy connections and performs
        /// the necessary SSL/TLS handshake.
        void connect() override {
            LockGuard connection_lock(connection_mutex);
            auto connection = this->connection = std::shared_ptr<Connection>(new Connection(handler_runner, config.timeout_idle, *io_service, context));
            connection_lock.unlock();

            std::pair<std::string, std::string> host_port;
            if(config.proxy_server.empty())
                host_port = {host, std::to_string(port)};
            else {
                auto proxy_host_port = parse_host_port(config.proxy_server, 8080);
                host_port = {proxy_host_port.first, std::to_string(proxy_host_port.second)};
            }

            auto resolver = std::make_shared<asio::ip::tcp::resolver>(*io_service);
            connection->set_timeout(config.timeout_request);
            async_resolve(*resolver, host_port, [this, connection, resolver](const error_code &ec, resolver_results results) {
                connection->cancel_timeout();
                auto lock = connection->handler_runner->continue_lock();
                if(!lock)
                    return;
                if(!ec) {
                    connection->set_timeout(this->config.timeout_request);
                    asio::async_connect(connection->socket->lowest_layer(), results, [this, connection, resolver](const error_code &ec, async_connect_endpoint /*endpoint*/) {
                        connection->cancel_timeout();
                        auto lock = connection->handler_runner->continue_lock();
                        if(!lock)
                            return;
                        if(!ec) {
                            asio::ip::tcp::no_delay option(true);
                            error_code ec;
                            connection->socket->lowest_layer().set_option(option, ec);

                            if(!this->config.proxy_server.empty()) {
                                auto streambuf = std::make_shared<asio::streambuf>();
                                std::ostream ostream(streambuf.get());
                                auto host_port = this->host + ':' + std::to_string(this->port);
                                ostream << "CONNECT " + host_port + " HTTP/1.1\r\n"
                                        << "Host: " << host_port << "\r\n";
                                if(!this->config.proxy_auth.empty())
                                    ostream << "Proxy-Authorization: Basic " << Crypto::Base64::encode(this->config.proxy_auth) << "\r\n";
                                ostream << "\r\n";
                                connection->set_timeout(this->config.timeout_request);
                                asio::async_write(connection->socket->next_layer(), *streambuf, [this, connection, streambuf](const error_code &ec, std::size_t /*bytes_transferred*/) {
                                    connection->cancel_timeout();
                                    auto lock = connection->handler_runner->continue_lock();
                                    if(!lock)
                                        return;
                                    if(!ec) {
                                        connection->set_timeout(this->config.timeout_request);
                                        connection->in_message = std::shared_ptr<InMessage>(new InMessage());
                                        asio::async_read_until(connection->socket->next_layer(), connection->in_message->streambuf, "\r\n\r\n", [this, connection](const error_code &ec, std::size_t /*bytes_transferred*/) {
                                            connection->cancel_timeout();
                                            auto lock = connection->handler_runner->continue_lock();
                                            if(!lock)
                                                return;
                                            if(!ec) {
                                                if(!ResponseMessage::parse(*connection->in_message, connection->http_version, connection->status_code, connection->header))
                                                    this->connection_error(connection, make_error_code::make_error_code(errc::protocol_error));
                                                else {
                                                    if(connection->status_code.compare(0, 3, "200") != 0)
                                                        this->connection_error(connection, make_error_code::make_error_code(errc::permission_denied));
                                                    else
                                                        this->handshake(connection);
                                                }
                                            } else
                                                this->connection_error(connection, ec);
                                        });
                                    } else
                                        this->connection_error(connection, ec);
                                });
                            } else
                                this->handshake(connection);
                        } else
                            this->connection_error(connection, ec);
                    });
                } else
                    this->connection_error(connection, ec);
            });
        }

        /// \brief Performs the SSL/TLS handshake.
        ///
        /// This method performs the SSL/TLS handshake with the WebSocket server. It sets the server name for SNI
        /// and handles the asynchronous handshake process. Once the handshake is complete, it calls the `upgrade`
        /// method to finalize the connection.
        ///
        /// \param connection A shared pointer to the connection object.
        void handshake(const std::shared_ptr<Connection> &connection) {
            SSL_set_tlsext_host_name(connection->socket->native_handle(), this->host.c_str());

            connection->set_timeout(this->config.timeout_request);
            connection->socket->async_handshake(asio::ssl::stream_base::client, [this, connection](const error_code &ec) {
                connection->cancel_timeout();
                auto lock = connection->handler_runner->continue_lock();
                if(!lock)
                    return;
                if(!ec)
                    upgrade(connection);
                else
                    this->connection_error(connection, ec);
            });
        }
    }; // SocketClient

} // namespace SimpleWeb

#endif // KURLYK_HEADER_KURLYK_WEBSOCKET_CLIENT_SIMPLE_WEB_SOCKET_CLIENT_HPP_INCLUDED

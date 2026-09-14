#pragma once
#ifndef KURLYK_HEADER_KURLYK_TYPES_PROXY_CONFIG_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_TYPES_PROXY_CONFIG_HPP_INCLUDED

/// \file ProxyConfig.hpp
/// \brief Defines the ProxyConfig structure for proxy server settings.

#include <string>
#include <stdexcept>
#include <utility>

#include "enums.hpp"
#include "../utils/string_utils.hpp"

#if defined(KURLYK_JSON_SUPPORT) && KURLYK_JSON_SUPPORT
#include <nlohmann/json.hpp>
#endif

namespace kurlyk {

    /// \struct ProxyConfig
    /// \brief Stores proxy server, authentication, and transport settings.
    struct ProxyConfig {
        std::string proxy_server; ///< Proxy address in host:port format.
        std::string proxy_auth;   ///< Proxy authentication in username:password format.
        ProxyType proxy_type;     ///< Proxy transport type.
        bool use;                 ///< Indicates whether the proxy is enabled.

        /// \brief Constructs an empty HTTP proxy configuration.
        ProxyConfig()
            : proxy_type(ProxyType::PROXY_HTTP), use(false) {}

        /// \brief Constructs a proxy configuration with server and authentication values.
        /// \param server Proxy server address.
        /// \param auth Proxy authentication credentials.
        /// \param type Proxy transport type.
        ProxyConfig(std::string server, std::string auth, ProxyType type = ProxyType::PROXY_HTTP)
            : proxy_server(std::move(server)),
              proxy_auth(std::move(auth)),
              proxy_type(type),
              use(false) {}

        /// \brief Sets the proxy server address and transport type.
        /// \param ip Proxy server host or IP address.
        /// \param port Proxy server port.
        /// \param type Proxy transport type.
        void set_proxy(const std::string& ip, int port, ProxyType type = ProxyType::PROXY_HTTP) {
            proxy_server = ip + ":" + std::to_string(port);
            proxy_type = type;
        }

        /// \brief Sets the proxy server and authentication values.
        /// \param ip Proxy server host or IP address.
        /// \param port Proxy server port.
        /// \param username Proxy username.
        /// \param password Proxy password.
        /// \param type Proxy transport type.
        void set_proxy(
            const std::string& ip,
            int port,
            const std::string& username,
            const std::string& password,
            ProxyType type = ProxyType::PROXY_HTTP) {
            set_proxy(ip, port, type);
            set_proxy_auth(username, password);
        }

        /// \brief Sets proxy authentication credentials.
        /// \param username Proxy username.
        /// \param password Proxy password.
        void set_proxy_auth(const std::string& username, const std::string& password) {
            proxy_auth = username + ":" + password;
        }

        /// \brief Returns the host part of the proxy server address.
        /// \return Host string, or an empty string when the address is malformed.
        std::string get_ip() const {
            const std::string::size_type pos = proxy_server.rfind(':');
            if (pos == std::string::npos || pos == 0) return std::string();
            return proxy_server.substr(0, pos);
        }

        /// \brief Returns the proxy port.
        /// \return Port number, or 0 when the address is malformed.
        int get_port() const {
            const std::string::size_type pos = proxy_server.rfind(':');
            if (pos == std::string::npos || pos + 1 >= proxy_server.size()) return 0;
            try {
                const int port = std::stoi(proxy_server.substr(pos + 1));
                return port > 0 && port <= 65535 ? port : 0;
            } catch (...) {
                return 0;
            }
        }

        /// \brief Returns the proxy username.
        /// \return Username string, or an empty string when credentials are absent.
        std::string get_username() const {
            const std::string::size_type pos = proxy_auth.find(':');
            if (pos == std::string::npos) return std::string();
            return proxy_auth.substr(0, pos);
        }

        /// \brief Returns the proxy password.
        /// \return Password string, or an empty string when credentials are absent.
        std::string get_password() const {
            const std::string::size_type pos = proxy_auth.find(':');
            if (pos == std::string::npos || pos + 1 >= proxy_auth.size()) return std::string();
            return proxy_auth.substr(pos + 1);
        }

        /// \brief Checks whether the proxy server address is valid.
        /// \return True when a host and a port are present.
        bool is_valid() const {
            return !get_ip().empty() && get_port() != 0;
        }
    };

#if defined(KURLYK_JSON_SUPPORT) && KURLYK_JSON_SUPPORT
    namespace detail {

        inline const char* proxy_type_name(ProxyType type) {
            switch (type) {
                case ProxyType::PROXY_HTTP: return "PROXY_HTTP";
                case ProxyType::PROXY_HTTPS: return "PROXY_HTTPS";
                case ProxyType::PROXY_HTTP_1_0: return "PROXY_HTTP_1_0";
                case ProxyType::PROXY_SOCKS4: return "PROXY_SOCKS4";
                case ProxyType::PROXY_SOCKS4A: return "PROXY_SOCKS4A";
                case ProxyType::PROXY_SOCKS5: return "PROXY_SOCKS5";
                case ProxyType::PROXY_SOCKS5_HOSTNAME: return "PROXY_SOCKS5_HOSTNAME";
            }
            return "PROXY_HTTP";
        }

        inline ProxyType proxy_type_from_json(const nlohmann::json& value) {
            if (value.is_number_integer()) {
                const int raw = value.get<int>();
                if (raw >= static_cast<int>(ProxyType::PROXY_HTTP) &&
                    raw <= static_cast<int>(ProxyType::PROXY_SOCKS5_HOSTNAME)) {
                    return static_cast<ProxyType>(raw);
                }
            }

            const std::string name = utils::to_upper_case(value.get<std::string>());
            if (name == "PROXY_HTTP") return ProxyType::PROXY_HTTP;
            if (name == "PROXY_HTTPS") return ProxyType::PROXY_HTTPS;
            if (name == "PROXY_HTTP_1_0") return ProxyType::PROXY_HTTP_1_0;
            if (name == "PROXY_SOCKS4") return ProxyType::PROXY_SOCKS4;
            if (name == "PROXY_SOCKS4A") return ProxyType::PROXY_SOCKS4A;
            if (name == "PROXY_SOCKS5") return ProxyType::PROXY_SOCKS5;
            if (name == "PROXY_SOCKS5_HOSTNAME") return ProxyType::PROXY_SOCKS5_HOSTNAME;
            throw std::invalid_argument("Invalid ProxyType: " + name);
        }

    } // namespace detail

    /// \brief Serializes ProxyConfig to JSON.
    inline void to_json(nlohmann::json& json, const ProxyConfig& config) {
        json = nlohmann::json{
            {"proxy_server", config.proxy_server},
            {"proxy_auth", config.proxy_auth},
            {"proxy_type", detail::proxy_type_name(config.proxy_type)},
            {"use", config.use}
        };
    }

    /// \brief Deserializes ProxyConfig from JSON.
    inline void from_json(const nlohmann::json& json, ProxyConfig& config) {
        if (json.contains("proxy_server")) config.proxy_server = json.at("proxy_server").get<std::string>();
        if (json.contains("proxy_auth")) config.proxy_auth = json.at("proxy_auth").get<std::string>();
        if (json.contains("proxy_type")) config.proxy_type = detail::proxy_type_from_json(json.at("proxy_type"));
        if (json.contains("use")) config.use = json.at("use").get<bool>();
    }
#endif

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_TYPES_PROXY_CONFIG_HPP_INCLUDED

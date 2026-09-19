#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_AUTH_O_AUTH_PKCE_CLIENT_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_AUTH_O_AUTH_PKCE_CLIENT_HPP_INCLUDED

/// \file OAuthPkceClient.hpp
/// \brief OAuth2 Authorization Code + PKCE client using standalone HTTP helpers.

#include "data/OAuthConfig.hpp"
#include "data/AuthResult.hpp"
#include "kurlyk/http/utils.hpp"
#include "kurlyk/utils/http_parser.hpp"
#include "kurlyk/utils/percent_encoding.hpp"
#include "kurlyk/utils/pkce.hpp"
#include <functional>
#include <chrono>
#include <sstream>

#if KURLYK_JSON_SUPPORT
#   include <nlohmann/json.hpp>
#endif

namespace kurlyk {
namespace http {
namespace auth {

    /// \class OAuthPkceClient
    /// \brief Implements the OAuth2 Authorization Code flow with optional PKCE.
    ///
    /// Uses the standalone free functions `kurlyk::http_post` and `kurlyk::http_request`
    /// so it does not depend on any particular `HttpClient` instance.
    class OAuthPkceClient {
    public:
        /// \brief Callback type for custom token parsing when JSON support is disabled.
        using TokenParser = std::function<bool(
            const std::string& raw_response,
            OAuthToken& out_token,
            std::string& out_error)>;

        /// \brief Constructs a client with the given OAuth configuration.
        /// \param config OAuth client configuration.
        explicit OAuthPkceClient(const OAuthConfig& config)
            : m_config(config) {}

        /// \brief Sets a custom token parser for non-JSON builds.
        /// \param parser Callback invoked when JSON support is unavailable.
        void set_token_parser(TokenParser parser) {
            m_token_parser = parser;
        }

        /// \brief Builds the full authorization URL with query parameters.
        /// \return The authorization URL ready to open in a browser.
        std::string build_authorization_url() {
            if (m_config.authorization_endpoint.empty() ||
                m_config.client_id.empty() ||
                m_config.redirect_uri.empty()) {
                return std::string();
            }

            if (m_state.empty()) {
                m_state = utils::generate_code_verifier(64);
            }

            QueryParams params;
            params.emplace("response_type", "code");
            params.emplace("client_id", m_config.client_id);
            params.emplace("redirect_uri", m_config.redirect_uri);
            if (!m_config.scope.empty()) {
                params.emplace("scope", m_config.scope);
            }
            params.emplace("state", m_state);

            if (m_config.use_pkce) {
                utils::PkcePair pair = utils::make_pkce_pair();
                m_code_verifier = pair.code_verifier;
                m_code_challenge = pair.code_challenge;
                params.emplace("code_challenge", m_code_challenge);
                params.emplace("code_challenge_method", pair.code_challenge_method);
            }

            if (!m_config.audience.empty()) {
                params.emplace("audience", m_config.audience);
            }
            if (!m_config.prompt.empty()) {
                params.emplace("prompt", m_config.prompt);
            }
            if (!m_config.access_type.empty()) {
                params.emplace("access_type", m_config.access_type);
            }

            return m_config.authorization_endpoint + utils::to_query_string(params, "?");
        }

        /// \brief Exchanges an authorization code for an access token.
        /// \param code The authorization code received from the OAuth server.
        /// \param out_result Receives the authentication result.
        /// \return `true` on success; inspect `out_result` on failure.
        bool exchange_code(const std::string& code, AuthResult& out_result) {
            out_result = AuthResult();

            if (code.empty()) {
                out_result.error = AuthError::InvalidConfig;
                out_result.error_message = "authorization code is empty";
                return false;
            }

            if (m_config.token_endpoint.empty()) {
                out_result.error = AuthError::InvalidConfig;
                out_result.error_message = "token_endpoint is not configured";
                return false;
            }

            QueryParams body_params;
            body_params.emplace("grant_type", "authorization_code");
            body_params.emplace("code", code);
            body_params.emplace("redirect_uri", m_config.redirect_uri);
            body_params.emplace("client_id", m_config.client_id);
            if (!m_config.client_secret.empty()) {
                body_params.emplace("client_secret", m_config.client_secret);
            }
            if (m_config.use_pkce && !m_code_verifier.empty()) {
                body_params.emplace("code_verifier", m_code_verifier);
            }

            const std::string body = utils::to_query_string(body_params);
            Headers headers;
            headers.emplace("Content-Type", "application/x-www-form-urlencoded");

            auto request_result = http_post(m_config.token_endpoint, QueryParams(), headers, body);
            (void)request_result.first;

            HttpResponsePtr response;
            try {
                response = request_result.second.get();
            } catch (const std::exception& e) {
                out_result.error = AuthError::HttpError;
                out_result.error_message = std::string("HTTP request failed: ") + e.what();
                return false;
            }

            if (!response) {
                out_result.error = AuthError::HttpError;
                out_result.error_message = "HTTP request returned null response";
                return false;
            }

            out_result.raw_response = response->content;

            if (response->status_code < 200 || response->status_code >= 300) {
                out_result.error = AuthError::HttpError;
                out_result.error_message = "Token endpoint returned HTTP " +
                    std::to_string(response->status_code);
                return false;
            }

            return parse_token_response(response->content, out_result);
        }

        /// \brief Refreshes an access token using a refresh token.
        /// \param refresh_token The refresh token.
        /// \param out_result Receives the authentication result.
        /// \return `true` on success; inspect `out_result` on failure.
        bool refresh_access_token(const std::string& refresh_token, AuthResult& out_result) {
            out_result = AuthResult();

            if (refresh_token.empty()) {
                out_result.error = AuthError::InvalidConfig;
                out_result.error_message = "refresh_token is empty";
                return false;
            }

            if (m_config.token_endpoint.empty()) {
                out_result.error = AuthError::InvalidConfig;
                out_result.error_message = "token_endpoint is not configured";
                return false;
            }

            QueryParams body_params;
            body_params.emplace("grant_type", "refresh_token");
            body_params.emplace("refresh_token", refresh_token);
            body_params.emplace("client_id", m_config.client_id);
            if (!m_config.client_secret.empty()) {
                body_params.emplace("client_secret", m_config.client_secret);
            }

            const std::string body = utils::to_query_string(body_params);
            Headers headers;
            headers.emplace("Content-Type", "application/x-www-form-urlencoded");

            auto request_result = http_post(m_config.token_endpoint, QueryParams(), headers, body);
            (void)request_result.first;

            HttpResponsePtr response;
            try {
                response = request_result.second.get();
            } catch (const std::exception& e) {
                out_result.error = AuthError::HttpError;
                out_result.error_message = std::string("HTTP request failed: ") + e.what();
                return false;
            }

            if (!response) {
                out_result.error = AuthError::HttpError;
                out_result.error_message = "HTTP request returned null response";
                return false;
            }

            out_result.raw_response = response->content;

            if (response->status_code < 200 || response->status_code >= 300) {
                out_result.error = AuthError::HttpError;
                out_result.error_message = "Token endpoint returned HTTP " +
                    std::to_string(response->status_code);
                return false;
            }

            return parse_token_response(response->content, out_result);
        }

        /// \brief Validates the state parameter returned by the authorization server.
        /// \param returned_state The state parameter from the redirect.
        /// \return `true` if the state matches the internally stored value.
        bool validate_state(const std::string& returned_state) const {
            return !m_state.empty() && m_state == returned_state;
        }

        /// \brief Returns the code verifier used in the last PKCE exchange.
        const std::string& code_verifier() const {
            return m_code_verifier;
        }

        /// \brief Returns the state parameter used in the last authorization request.
        const std::string& state() const {
            return m_state;
        }

    protected:
        bool parse_token_response(const std::string& raw_response, AuthResult& out_result) {
#if KURLYK_JSON_SUPPORT
            try {
                nlohmann::json j = nlohmann::json::parse(raw_response);

                if (j.contains("error")) {
                    out_result.error = AuthError::InvalidResponse;
                    out_result.error_message = j.value("error_description",
                        j.value("error", "unknown error"));
                    return false;
                }

                OAuthToken token;
                token.raw_response = raw_response;
                if (j.contains("access_token")) token.access_token = j["access_token"].get<std::string>();
                if (j.contains("refresh_token")) token.refresh_token = j["refresh_token"].get<std::string>();
                if (j.contains("token_type")) token.token_type = j["token_type"].get<std::string>();
                if (j.contains("scope")) token.scope = j["scope"].get<std::string>();

                if (j.contains("expires_in")) {
                    int64_t expires_in = j["expires_in"].get<int64_t>();
                    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    token.expires_at_ms = now_ms + (expires_in * 1000);
                }

                if (token.access_token.empty()) {
                    out_result.error = AuthError::InvalidResponse;
                    out_result.error_message = "access_token is missing in response";
                    return false;
                }

                out_result.token = token;
                out_result.success = true;
                return true;
            } catch (const std::exception& e) {
                out_result.error = AuthError::InvalidResponse;
                out_result.error_message = std::string("JSON parse error: ") + e.what();
                return false;
            }
#else
            if (m_token_parser) {
                std::string error_msg;
                bool ok = m_token_parser(raw_response, out_result.token, error_msg);
                if (!ok) {
                    out_result.error = AuthError::InvalidResponse;
                    out_result.error_message = error_msg.empty() ? "custom token parser failed" : error_msg;
                    return false;
                }
                out_result.success = true;
                return true;
            }

            out_result.error = AuthError::UnsupportedFlow;
            out_result.error_message = "JSON support disabled and no custom token parser set";
            return false;
#endif
        }

    private:
        OAuthConfig m_config;
        std::string m_code_verifier;
        std::string m_code_challenge;
        std::string m_state;
        TokenParser m_token_parser;
    };

} // namespace auth
} // namespace http
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_AUTH_O_AUTH_PKCE_CLIENT_HPP_INCLUDED

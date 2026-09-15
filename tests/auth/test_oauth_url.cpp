#include <kurlyk.hpp>
#include <string>

int main() {
    kurlyk::OAuthConfig config;
    config.client_id = "client123";
    config.authorization_endpoint = "https://example.com/auth";
    config.token_endpoint = "https://example.com/token";
    config.redirect_uri = "https://example.com/callback";
    config.scope = "read write";
    config.use_pkce = true;
    config.audience = "my-audience";
    config.prompt = "consent";
    config.access_type = "offline";

    kurlyk::http::auth::OAuthPkceClient oauth(config);
    std::string url = oauth.build_authorization_url();

    if (url.empty()) return 1;

    // Must start with the authorization endpoint and '?'
    if (url.find("https://example.com/auth?") != 0) return 1;

    // Required parameters
    if (url.find("response_type=code") == std::string::npos) return 1;
    if (url.find("client_id=client123") == std::string::npos) return 1;
    if (url.find("redirect_uri=") == std::string::npos) return 1;
    if (url.find("state=") == std::string::npos) return 1;
    if (url.find("code_challenge=") == std::string::npos) return 1;
    if (url.find("code_challenge_method=S256") == std::string::npos) return 1;

    // Optional parameters
    if (url.find("scope=") == std::string::npos) return 1;
    if (url.find("audience=my-audience") == std::string::npos) return 1;
    if (url.find("prompt=consent") == std::string::npos) return 1;
    if (url.find("access_type=offline") == std::string::npos) return 1;

    // State and verifier must be non-empty
    if (oauth.state().empty()) return 1;
    if (oauth.code_verifier().empty()) return 1;
    const std::string state1 = oauth.state();
    const std::string verifier1 = oauth.code_verifier();

    // Two consecutive calls must regenerate state and verifier
    std::string url2 = oauth.build_authorization_url();
    if (url == url2) return 1;
    if (state1 == oauth.state()) return 1;
    if (verifier1 == oauth.code_verifier()) return 1;

    // Missing required config returns empty
    kurlyk::OAuthConfig bad_config;
    kurlyk::http::auth::OAuthPkceClient bad_oauth(bad_config);
    if (!bad_oauth.build_authorization_url().empty()) return 1;

    return 0;
}

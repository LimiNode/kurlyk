#define KURLYK_AUTO_INIT 0
#ifndef KURLYK_OAUTH_SUPPORT
#   define KURLYK_OAUTH_SUPPORT 0
#endif
#include <kurlyk.hpp>
#include <iostream>

/// Minimal example showing BearerTokenAuthProvider without OAuth.
/// KURLYK_OAUTH_SUPPORT=0 excludes the OAuth2 PKCE client from the build.
int main() {
    kurlyk::init(true);

    // Replace with your actual token or load from environment.
    const std::string token = "YOUR_BEARER_TOKEN";

    kurlyk::HttpClient client("https://api.example.com");

    kurlyk::Headers headers;
    kurlyk::http::auth::BearerTokenAuthProvider auth(token);
    auth.authorize(headers);

    auto future = client.get("/resource", kurlyk::QueryParams(), headers);

    auto response = future.get();
    if (response && response->ready) {
        KURLYK_PRINT << "Status: " << response->status_code << std::endl;
        KURLYK_PRINT << "Body: " << response->content << std::endl;
    } else {
        KURLYK_PRINT << "Request failed: "
                     << (response ? response->error_code.message() : "null")
                     << std::endl;
    }

    kurlyk::deinit();
    return 0;
}

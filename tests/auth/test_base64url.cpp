#include <kurlyk/utils/base64_url.hpp>
#include <string>
#include <vector>

int main() {
    // round-trip
    std::string input = "Hello, World!";
    std::string encoded = kurlyk::utils::base64url_encode(input);

    // Base64url must not contain padding '='
    if (encoded.find('=') != std::string::npos) return 1;

    std::vector<uint8_t> decoded = kurlyk::utils::base64url_decode(encoded);
    std::string decoded_str(decoded.begin(), decoded.end());
    if (decoded_str != input) return 1;

    // empty input
    std::string empty_encoded = kurlyk::utils::base64url_encode("");
    if (!empty_encoded.empty()) return 1;
    std::vector<uint8_t> empty_decoded = kurlyk::utils::base64url_decode(empty_encoded);
    if (!empty_decoded.empty()) return 1;

    // binary data round-trip
    std::vector<uint8_t> binary(256);
    for (int i = 0; i < 256; ++i) binary[i] = static_cast<uint8_t>(i);
    std::string bin_encoded = kurlyk::utils::base64url_encode(binary.data(), binary.size());
    std::vector<uint8_t> bin_decoded = kurlyk::utils::base64url_decode(bin_encoded);
    if (bin_decoded != binary) return 1;

    return 0;
}

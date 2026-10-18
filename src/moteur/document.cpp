#include "moteur/document.hpp"

#include <stdexcept>
#include <string>

#include "moteur/cbor_check.hpp"

namespace moteur {

bool is_cbor_document(std::string_view bytes) {
    if (bytes.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(bytes.front());
    return first >= 0x80 && first <= 0xBF;  // CBOR major types 4 (array) and 5 (map)
}

nlohmann::json parse_document(std::string_view bytes) {
    if (is_cbor_document(bytes)) {
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        std::string problem;
        if (!check_cbor_structure(data, bytes.size(), problem)) {
            throw std::runtime_error("invalid CBOR: " + problem);
        }
        try {
            return nlohmann::json::from_cbor(data, data + bytes.size());
        } catch (const nlohmann::json::exception& e) {
            throw std::runtime_error(std::string("invalid CBOR: ") + e.what());
        }
    }
    try {
        return nlohmann::json::parse(bytes);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("invalid JSON: ") + e.what());
    }
}

std::vector<std::uint8_t> shipped_document(const nlohmann::json& document) {
    return nlohmann::json::to_cbor(document);
}

}  // namespace moteur

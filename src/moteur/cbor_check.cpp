#include "moteur/cbor_check.hpp"

#include <cstdint>
#include <vector>

namespace moteur {

namespace {

constexpr std::uint64_t kIndefinite = ~0ull;  // a container closed by a "break" byte (0xFF)

}  // namespace

bool check_cbor_structure(const std::uint8_t* data, std::size_t size, std::string& error, int max_depth) {
    // Each entry: how many items the open container still expects (kIndefinite: until a break). The
    // document itself is one item.
    std::vector<std::uint64_t> open;
    open.reserve(static_cast<std::size_t>(max_depth) + 2);
    open.push_back(1);
    std::size_t at = 0;
    while (!open.empty()) {
        if (open.back() == 0) {
            open.pop_back();
            continue;
        }
        if (at >= size) {
            error = "CBOR: truncated (an item is missing at byte " + std::to_string(at) + ")";
            return false;
        }
        const std::uint8_t initial = data[at++];
        const int major = initial >> 5;
        const int info = initial & 31;

        if (initial == 0xFF) {  // break: closes the innermost indefinite container
            if (open.back() != kIndefinite) {
                error = "CBOR: unexpected break at byte " + std::to_string(at - 1);
                return false;
            }
            open.pop_back();
            continue;
        }
        if (open.back() != kIndefinite) {
            --open.back();  // this item is one of the container's
        }

        // The argument: in the initial byte, or in the 1, 2, 4 or 8 bytes after it.
        std::uint64_t argument = 0;
        bool indefinite = false;
        if (info < 24) {
            argument = static_cast<std::uint64_t>(info);
        } else if (info <= 27) {
            const std::size_t bytes = std::size_t{1} << (info - 24);
            if (size - at < bytes) {
                error = "CBOR: truncated (an argument at byte " + std::to_string(at - 1) + ")";
                return false;
            }
            for (std::size_t i = 0; i < bytes; ++i) {
                argument = (argument << 8) | data[at + i];
            }
            at += bytes;
        } else if (info == 31 && major >= 2 && major <= 5) {
            indefinite = true;
        } else {
            error = "CBOR: invalid item at byte " + std::to_string(at - 1);
            return false;
        }

        std::uint64_t items = 0;  // what this item contains, as items
        switch (major) {
            case 0:
            case 1:
            case 7:  // integers, simple values and floats: their bytes were the argument
                continue;
            case 2:
            case 3:  // byte and text strings: the bytes, or chunks until a break
                if (indefinite) {
                    items = kIndefinite;
                    break;
                }
                if (size - at < argument) {
                    error = "CBOR: truncated (a string at byte " + std::to_string(at) + ")";
                    return false;
                }
                at += static_cast<std::size_t>(argument);
                continue;
            case 4:  // array
                items = indefinite ? kIndefinite : argument;
                break;
            case 5:  // map: a key and a value per entry
                if (!indefinite && argument > (kIndefinite - 1) / 2) {
                    error = "CBOR: impossible map size at byte " + std::to_string(at);
                    return false;
                }
                items = indefinite ? kIndefinite : argument * 2;
                break;
            case 6:  // tag: one item follows
                items = 1;
                break;
            default:
                break;
        }
        if (items == 0) {
            continue;  // an empty container opens nothing
        }
        // A definite container cannot hold more items than there are bytes left (one byte at least each).
        if (items != kIndefinite && items > size - at) {
            error = "CBOR: truncated (a container of " + std::to_string(items) + " items at byte " + std::to_string(at) + ")";
            return false;
        }
        if (static_cast<int>(open.size()) > max_depth) {
            error = "CBOR: nested deeper than " + std::to_string(max_depth) + " levels";
            return false;
        }
        open.push_back(items);
    }
    return true;
}

}  // namespace moteur

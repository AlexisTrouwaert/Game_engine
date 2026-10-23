#include "moteur/locale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace moteur {

namespace {

struct LanguageRule {
    const char* language;
    int function;
};

struct NumberSymbols {
    const char* language;
    const char* decimal;
    const char* group;
    const char* minus;
    const char* percent_prefix;
    const char* percent_suffix;
    int primary_group;    // 3; 0: no grouping
    int secondary_group;  // 3, or 2 in Hindi ("12,34,567")
    int min_grouping;     // groups only from this many digits before the first separator (2: "1234" in Spanish)
};

#include "moteur/locale_tables.inc"

// The entry for the longest prefix of `language` ("pt-BR" -> "pt"), or null.
template <typename Entry, std::size_t N>
const Entry* find_language(const Entry (&table)[N], std::string_view language) {
    std::string tag(language);
    std::replace(tag.begin(), tag.end(), '_', '-');
    while (true) {
        for (const Entry& entry : table) {
            if (tag == entry.language) {
                return &entry;
            }
        }
        const std::size_t dash = tag.rfind('-');
        if (dash == std::string::npos) {
            return nullptr;
        }
        tag.resize(dash);
    }
}

}  // namespace

const char* plural_category_name(PluralCategory category) {
    switch (category) {
        case PluralCategory::Zero: return "zero";
        case PluralCategory::One: return "one";
        case PluralCategory::Two: return "two";
        case PluralCategory::Few: return "few";
        case PluralCategory::Many: return "many";
        case PluralCategory::Other:
        default: return "other";
    }
}

const char* cldr_version() {
    return kCldrVersion;
}

PluralOperands PluralOperands::of(double value, int fraction_digits) {
    fraction_digits = std::clamp(fraction_digits, 0, 15);
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f", fraction_digits, std::fabs(value));
    return parse(text);
}

PluralOperands PluralOperands::parse(std::string_view text) {
    PluralOperands o;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        text.remove_prefix(1);
    }
    const std::size_t point = text.find('.');
    const std::string_view integer = text.substr(0, point);
    const std::string_view fraction = point == std::string_view::npos ? std::string_view() : text.substr(point + 1);
    for (const char c : integer) {
        if (c >= '0' && c <= '9') {
            o.i = o.i * 10 + (c - '0');
        }
    }
    o.v = static_cast<std::int64_t>(fraction.size());
    std::string_view trimmed = fraction;
    while (!trimmed.empty() && trimmed.back() == '0') {
        trimmed.remove_suffix(1);
    }
    o.w = static_cast<std::int64_t>(trimmed.size());
    for (const char c : fraction) {
        o.f = o.f * 10 + (c - '0');
    }
    for (const char c : trimmed) {
        o.t = o.t * 10 + (c - '0');
    }
    o.n = static_cast<double>(o.i) + (fraction.empty() ? 0.0 : static_cast<double>(o.f) / std::pow(10.0, static_cast<double>(o.v)));
    return o;
}

PluralCategory plural_category(std::string_view language, const PluralOperands& operands, bool ordinal) {
    if (ordinal) {
        const LanguageRule* rule = find_language(kOrdinalLanguages, language);
        if (rule == nullptr) {
            rule = find_language(kOrdinalLanguages, "en");
        }
        return kOrdinalFunctions[rule->function](operands);
    }
    const LanguageRule* rule = find_language(kCardinalLanguages, language);
    if (rule == nullptr) {
        rule = find_language(kCardinalLanguages, "en");
    }
    return kCardinalFunctions[rule->function](operands);
}

std::string format_number(std::string_view language, double value, const NumberFormatOptions& options) {
    const NumberSymbols* symbols = find_language(kNumberSymbols, language);
    if (symbols == nullptr) {
        symbols = find_language(kNumberSymbols, "en");
    }
    if (options.percent) {
        value *= 100.0;
    }
    if (std::isnan(value)) {
        return "NaN";
    }
    std::string out;
    const bool negative = std::signbit(value) && value != 0.0;
    if (negative) {
        out += symbols->minus;
    }
    if (options.percent) {
        out += symbols->percent_prefix;
    }
    if (std::isinf(value)) {
        out += "\xE2\x88\x9E";  // ∞
        return out;
    }
    const int max_digits = std::clamp(options.max_fraction_digits, 0, 15);
    const int min_digits = std::clamp(options.min_fraction_digits, 0, max_digits);
    // Rounded (halves away from zero) and written with max_digits digits, then trimmed.
    const double scale = std::pow(10.0, max_digits);
    const double rounded = std::round(std::fabs(value) * scale) / scale;
    char text[400];
    std::snprintf(text, sizeof(text), "%.*f", max_digits, rounded);
    std::string digits(text);
    std::string integer = digits.substr(0, digits.find('.'));
    std::string fraction = digits.find('.') == std::string::npos ? std::string() : digits.substr(digits.find('.') + 1);
    while (static_cast<int>(fraction.size()) > min_digits && !fraction.empty() && fraction.back() == '0') {
        fraction.pop_back();
    }

    // Grouping from the right: the primary group first, then the secondary ones; only when the
    // integer part has at least primary + min_grouping digits.
    std::string grouped;
    const int length = static_cast<int>(integer.size());
    if (options.grouping && symbols->primary_group > 0 && length >= symbols->primary_group + symbols->min_grouping) {
        std::vector<std::string> groups;
        int end = length;
        int size = symbols->primary_group;
        while (end > 0) {
            const int start = std::max(0, end - size);
            groups.push_back(integer.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)));
            end = start;
            size = symbols->secondary_group;
        }
        for (std::size_t g = groups.size(); g-- > 0;) {
            grouped += groups[g];
            if (g > 0) {
                grouped += symbols->group;
            }
        }
    } else {
        grouped = integer;
    }
    out += grouped;
    if (!fraction.empty()) {
        out += symbols->decimal;
        out += fraction;
    }
    if (options.percent) {
        out += symbols->percent_suffix;
    }
    return out;
}

}  // namespace moteur

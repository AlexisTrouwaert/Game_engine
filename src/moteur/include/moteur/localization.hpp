#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "moteur/locale.hpp"

/// Localization (milestone 9 bis, part 5): every text the player reads comes from string files,
/// one set per language, by stable identifier; the code never builds a sentence itself.
///
/// The files (`locales/<language>/<resource>.ftl`) use a syntax modeled on Mozilla's Fluent,
/// read by the engine (FORMATS.md, section 22):
///
/// ```
/// # A comment
/// options-title = Options
/// loot-found = { $count ->
///         [one] Vous avez trouvé un objet.
///        *[other] Vous avez trouvé { $count } objets.
///     }
/// -sword = épée
///     .gender = feminine
/// item-name = { -sword.gender ->
///         [feminine] Une { -sword } rouillée
///        *[masculine] Un { -sword } rouillé
///     }
/// health = Vie { NUMBER($ratio, style: "percent") }
/// play-time = { DURATION($seconds) } de jeu
/// ```
///
/// Variables are given by name (`{"count", 3}`); numbers are written in the language's conventions
/// (format_number()) and select plural variants by the CLDR rules (`[one]`, `[few]`...) or by exact
/// value (`[0]`). Terms (`-name`) hold words other messages agree with (their attributes:
/// `.gender`); they may take arguments (`{ -sword(case: "genitive") }`).
///
/// A missing message falls back to the base language (with a warning in development), then to its
/// identifier between braces, so that a hole shows. The language changes at once (`set_language()`;
/// generation() tells an interface to redo its texts). The language "qps" is a pseudo-localization
/// of the base language: accented, 40 % longer and bracketed, it shows the texts written in the code
/// and the boxes too narrow.
///
/// format() may run on any thread once the files are loaded; loading and set_language() run on the
/// main thread.
namespace moteur {

/// A variable's value: an integer, a real number or a text.
using LocalizedValue = std::variant<std::int64_t, double, std::string>;

/// One variable of a message: `{"count", 3}`, `{"name", std::string("Gauvain")}`.
struct LocalizedArg {
    std::string_view name;  ///< as written in the file, without the `$`
    LocalizedValue value;   ///< a number is written in the language's conventions

    /// A variable from an integer, a real number or a text.
    ///@{
    LocalizedArg(std::string_view n, int v) : name(n), value(static_cast<std::int64_t>(v)) {}
    LocalizedArg(std::string_view n, std::int64_t v) : name(n), value(v) {}
    LocalizedArg(std::string_view n, double v) : name(n), value(v) {}
    LocalizedArg(std::string_view n, float v) : name(n), value(static_cast<double>(v)) {}
    LocalizedArg(std::string_view n, std::string v) : name(n), value(std::move(v)) {}
    LocalizedArg(std::string_view n, const char* v) : name(n), value(std::string(v)) {}
    ///@}
};

/// The variables of a message.
using LocalizedArgs = std::initializer_list<LocalizedArg>;

/// A problem found in the string files (Localization::check() and the parser).
struct LocalizationIssue {
    /// What is wrong.
    enum class Kind {
        Syntax,          ///< a file that does not read (the entry is skipped)
        Missing,         ///< a message of the base language absent from a language
        Extra,           ///< a message no base message has (a typo, or a removed text)
        Variables,       ///< not the same variables as in the base language
        Untranslated,    ///< the same text as in the base language (perhaps on purpose: "OK")
        BadReference,    ///< a reference to a message or term that does not exist
    };
    Kind kind = Kind::Syntax;  ///< what is wrong
    std::string language;      ///< the language of the file or message
    std::string file;     ///< for a syntax error
    int line = 0;         ///< for a syntax error (1-based)
    std::string id;       ///< the message
    std::string message;  ///< in English, for the developer
    /// An error stops the preparation; the other kinds are warnings.
    bool is_error() const { return kind == Kind::Syntax || kind == Kind::BadReference; }
};

/// A language the game offers (`locales/languages.json`).
struct LanguageInfo {
    std::string code;  ///< BCP 47 ("fr", "en")
    std::string name;  ///< in itself ("Français", "English"): the options list it so
};

/// The strings of the program in every language it offers.
class Localization {
public:
    /// A function callable in a placeable (`{ NAME($x, opt: "v") }`): positional and named
    /// arguments in, a value out. Built in: NUMBER, DURATION.
    using Function = std::function<LocalizedValue(const Localization& localization,
                                                  const std::vector<LocalizedValue>& positional,
                                                  const std::map<std::string, LocalizedValue, std::less<>>& named)>;

    Localization();
    ~Localization();
    Localization(const Localization&) = delete;
    Localization& operator=(const Localization&) = delete;

    /// Reads `<folder>/languages.json` (`{"base": "fr", "languages": [{"code", "name"}],
    /// "resources": ["tranche"]}`) and, for every language, the files `<folder>/<code>/<resource>.ftl`
    /// of `engine_resources` (the engine's own: "moteur") then of the list. A file that does not
    /// read is reported (issues()) and its readable entries kept. Returns false without a
    /// languages.json. Files are read with read_file() (archives first). Main thread.
    bool load(const std::string& folder, const std::vector<std::string>& engine_resources = {"moteur"});
    /// The same over several folders (the preparation: the engine's sources, then the game's): the
    /// last languages.json found wins, and each resource is read from every folder that has it, the
    /// later ones replacing the earlier ones' entries.
    bool load(const std::vector<std::string>& folders, const std::vector<std::string>& engine_resources = {"moteur"});

    /// The issues of check() as text for a report: one line per syntax error, bad reference,
    /// missing, extra or mismatched message; the untranslated ones listed per language on one line.
    static std::vector<std::string> describe(const std::vector<LocalizationIssue>& issues, bool errors);

    /// Adds the text of a string file to a language (tests, tools; load() calls it). Entries
    /// already there are replaced. Returns false if the text had syntax errors (reported).
    bool add_resource(std::string_view language, std::string_view text, std::string_view file = "<memory>");
    /// Forgets every string (before a reload).
    void clear();

    /// The language the texts are in, among languages() or "qps"; an unknown one is refused
    /// (false). Bumps generation().
    bool set_language(std::string_view language);
    /// The current language.
    const std::string& language() const { return language_; }
    /// The language of the files the others are translated from (the fallback).
    const std::string& base_language() const { return base_; }
    /// Sets the base language (languages.json does it for load(); tests, tools).
    void set_base_language(std::string_view language) { base_ = std::string(language); }
    /// The languages of languages.json, in its order.
    const std::vector<LanguageInfo>& languages() const { return languages_; }
    /// Of the player's system preferences (SDL_GetPreferredLocales()), the first one offered; else
    /// the base language.
    std::string preferred_language() const;
    /// Changes when the texts change (language, reload): an interface compares it to redo its texts.
    std::uint64_t generation() const { return generation_; }

    /// The message `id` (or its attribute: "id.attr") in the current language, its variables
    /// replaced. Never fails: a missing message gives the base language's, then "{id}".
    ///@{
    std::string format(std::string_view id, LocalizedArgs args = {}) const;
    std::string format(std::string_view id, const std::vector<LocalizedArg>& args) const;
    ///@}
    /// Whether the current language (or the base language) has the message.
    bool has(std::string_view id) const;

    /// A number in the current language's conventions.
    std::string format_number(double value, const NumberFormatOptions& options = {}) const;
    /// A duration in the current language (`moteur-duration-*` messages): "1 h 05 min", "3 min 20 s", "45 s".
    std::string format_duration(double seconds) const;

    /// Adds or replaces a function of the placeables (names in capitals, as Fluent's).
    void add_function(std::string name, Function function);

    /// Every message of the base language checked in every language: missing, extra, different
    /// variables, untranslated; references to unknown messages and terms. Syntax errors of the
    /// loading are included.
    std::vector<LocalizationIssue> check() const;
    /// The syntax errors of the files read so far.
    const std::vector<LocalizationIssue>& issues() const { return issues_; }

    /// The files read by load() (for hot reload).
    const std::vector<std::string>& files() const { return files_; }

    /// Every message identifier of a language (sorted; terms not included).
    std::vector<std::string> message_ids(std::string_view language) const;

    struct Bundle;  ///< the parsed messages of one language (localization.cpp)

private:
    const Bundle* bundle(std::string_view language) const;
    Bundle& bundle_for_writing(std::string_view language);
    void make_pseudo();

    /// In development, warns once about a message shown in the base language or missing.
    void warn_missing(std::string_view id, bool everywhere) const;

    std::map<std::string, std::unique_ptr<Bundle>, std::less<>> bundles_;
    mutable std::mutex warned_mutex_;
    mutable std::set<std::string, std::less<>> warned_;
    std::map<std::string, Function, std::less<>> functions_;
    std::vector<LanguageInfo> languages_;
    std::vector<LocalizationIssue> issues_;
    std::vector<std::string> files_;
    std::string language_;
    std::string base_ = "fr";
    std::uint64_t generation_ = 1;
};

/// The language code of the pseudo-localization.
inline constexpr std::string_view kPseudoLanguage = "qps";

}  // namespace moteur

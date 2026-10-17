#include "moteur/console.hpp"

#include <algorithm>
#include <sstream>

namespace moteur {

Console::Console() {
    add_builtins();
}

void Console::add(std::string name, std::string usage, std::string help, Kind kind, Handler handler, Completer completer) {
    remove(name);
    commands_.push_back({std::move(name), std::move(usage), std::move(help), kind, std::move(handler), std::move(completer)});
    std::sort(commands_.begin(), commands_.end(), [](const Command& a, const Command& b) { return a.name < b.name; });
}

void Console::remove(std::string_view name) {
    std::erase_if(commands_, [name](const Command& c) { return c.name == name; });
}

bool Console::has(std::string_view name) const {
    return find(name) != nullptr;
}

const Console::Command* Console::find(std::string_view name) const {
    for (const Command& command : commands_) {
        if (command.name == name) {
            return &command;
        }
    }
    return nullptr;
}

bool Console::split(std::string_view line, std::vector<std::string>& words, std::string& error) {
    words.clear();
    std::string word;
    bool in_word = false, quoted = false;
    for (const char c : line) {
        if (quoted) {
            if (c == '"') {
                quoted = false;
            } else {
                word += c;
            }
        } else if (c == '"') {
            quoted = true;
            in_word = true;
        } else if (c == ' ' || c == '\t') {
            if (in_word) {
                words.push_back(std::move(word));
                word.clear();
                in_word = false;
            }
        } else {
            word += c;
            in_word = true;
        }
    }
    if (quoted) {
        error = "unclosed quote";
        return false;
    }
    if (in_word) {
        words.push_back(std::move(word));
    }
    return true;
}

void Console::add_line(Level level, std::string text) {
    lines_.push_back({level, std::move(text)});
    ++line_count_;
    error_count_ += level == Level::Error ? 1 : 0;
    while (lines_.size() > kMaxLines) {
        lines_.pop_front();
    }
}

void Console::log_line(Level level, std::string text) {
    std::lock_guard lock(log_mutex_);
    log_queue_.push_back({level, std::move(text)});
}

void Console::flush_log() {
    std::vector<Line> lines;
    {
        std::lock_guard lock(log_mutex_);
        lines.swap(log_queue_);
    }
    for (Line& line : lines) {
        add_line(line.level, std::move(line.text));
    }
}

void Console::submit(const std::string& line) {
    if (line.find_first_not_of(" \t") == std::string::npos) {
        return;
    }
    if (history_.empty() || history_.back() != line) {
        history_.push_back(line);
    }
    add_line(Level::Input, "> " + line);
    std::vector<std::string> words;
    std::string problem;
    if (!split(line, words, problem)) {
        error(problem);
        return;
    }
    const Command* command = find(words[0]);
    if (command == nullptr) {
        error("unknown command: " + words[0] + " (type help for the list)");
        return;
    }
    bool game = command->kind == Kind::Game;
    // Changing a Logic variable changes the game: queued like a game command, so that it runs on a
    // tick and the input recording keeps it.
    if ((words[0] == "set" || words[0] == "reset" || words[0] == "toggle") && words.size() >= 2) {
        const Variable* variable = variables_.find(words[1]);
        game = variable != nullptr && (variable->flags() & Variable::Logic) != 0;
    }
    if (game) {
        queued_.push_back(line);  // at the start of the next tick
        return;
    }
    execute(words, false);
}

bool Console::run_script(std::string_view text) {
    std::istringstream lines{std::string(text)};
    std::string line;
    bool ok = true;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        const std::uint64_t errors_before = error_count_;
        submit(line.substr(first));
        ok = ok && error_count_ == errors_before;
    }
    return ok;
}

std::vector<std::string> Console::take_game_commands() {
    std::vector<std::string> commands;
    commands.swap(queued_);
    return commands;
}

void Console::run_game_command(const std::string& line) {
    std::vector<std::string> words;
    std::string problem;
    if (!split(line, words, problem) || words.empty()) {
        error("cannot parse the game command: " + line);
        return;
    }
    execute(words, true);
}

void Console::execute(const std::vector<std::string>& words, bool from_queue) {
    const Command* command = find(words[0]);
    if (command == nullptr) {
        error("unknown command: " + words[0]);
        return;
    }
    (void)from_queue;
    const Args args(words.begin() + 1, words.end());
    try {
        command->handler(args, *this);
    } catch (const std::exception& e) {
        error(words[0] + ": " + e.what());
    }
}

std::vector<std::string> Console::complete(const std::string& line) const {
    std::vector<std::string> words;
    std::string problem;
    split(line, words, problem);
    const bool new_word = line.empty() || line.back() == ' ';
    if (new_word) {
        words.emplace_back();
    }
    std::vector<std::string> candidates;
    const std::string& prefix = words.empty() ? std::string() : words.back();
    const auto keep = [&prefix, &candidates](const std::string& name) {
        if (name.compare(0, prefix.size(), prefix) == 0) {
            candidates.push_back(name);
        }
    };
    if (words.size() <= 1) {
        for (const Command& command : commands_) {
            keep(command.name);
        }
    } else if (const Command* command = find(words[0])) {
        const std::size_t index = words.size() - 2;
        if ((command->name == "set" || command->name == "get" || command->name == "reset" || command->name == "toggle") &&
            index == 0) {
            for (const Variable* variable : variables_.list()) {
                keep(variable->name());
            }
        } else if (command->name == "help" && index == 0) {
            for (const Command& c : commands_) {
                keep(c.name);
            }
        } else if (command->completer) {
            for (const std::string& candidate : command->completer(index, prefix)) {
                keep(candidate);
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

std::string Console::complete_line(const std::string& line) const {
    const std::vector<std::string> candidates = complete(line);
    if (candidates.empty()) {
        return line;
    }
    std::string common = candidates.front();
    for (const std::string& candidate : candidates) {
        std::size_t n = 0;
        while (n < common.size() && n < candidate.size() && common[n] == candidate[n]) {
            ++n;
        }
        common.resize(n);
    }
    std::size_t word_start = line.size();  // a new word after a space
    if (!line.empty() && line.back() != ' ') {
        const std::size_t space = line.find_last_of(' ');
        word_start = space == std::string::npos ? 0 : space + 1;
    }
    std::string result = line.substr(0, word_start) + common;
    if (candidates.size() == 1) {
        result += ' ';
    }
    return result;
}

void Console::add_builtins() {
    add("help", "[command]", "lists the commands, or shows the help of one command", Kind::Tool, [](const Args& args, Console& c) {
        if (!args.empty()) {
            const Command* command = c.find(args[0]);
            if (command == nullptr) {
                c.error("unknown command: " + args[0]);
                return;
            }
            c.print(command->name + " " + command->usage + ": " + command->help +
                    (command->kind == Kind::Game ? " (game command: runs on the next tick)" : ""));
            return;
        }
        for (const Command& command : c.commands_) {
            c.print(command.name + (command.usage.empty() ? "" : " " + command.usage) + ": " + command.help);
        }
    });
    add("set", "<variable> <value>", "sets a variable", Kind::Tool, [](const Args& args, Console& c) {
        if (args.size() != 2) {
            c.error("usage: set <variable> <value>");
            return;
        }
        Variable* variable = c.variables_.find(args[0]);
        if (variable == nullptr) {
            c.error("unknown variable: " + args[0] + " (type vars for the list)");
            return;
        }
        std::string problem;
        if (!variable->set_text(args[1], problem)) {
            c.error(args[0] + ": " + problem);
            return;
        }
        c.print(variable->name() + " = " + variable->value_text());
    });
    add("get", "<variable>", "shows a variable", Kind::Tool, [](const Args& args, Console& c) {
        if (args.size() != 1) {
            c.error("usage: get <variable>");
            return;
        }
        const Variable* variable = c.variables_.find(args[0]);
        if (variable == nullptr) {
            c.error("unknown variable: " + args[0]);
            return;
        }
        c.print(variable->name() + " = " + variable->value_text() + " (default " + variable->default_text() + "): " + variable->help());
    });
    add("reset", "<variable>", "resets a variable to its default value", Kind::Tool, [](const Args& args, Console& c) {
        Variable* variable = args.size() == 1 ? c.variables_.find(args[0]) : nullptr;
        if (variable == nullptr) {
            c.error("usage: reset <variable>");
            return;
        }
        variable->reset();
        c.print(variable->name() + " = " + variable->value_text());
    });
    add("toggle", "<variable>", "flips a boolean variable", Kind::Tool, [](const Args& args, Console& c) {
        Variable* variable = args.size() == 1 ? c.variables_.find(args[0]) : nullptr;
        if (variable == nullptr || variable->type() != Variable::Type::Bool) {
            c.error("usage: toggle <boolean variable>");
            return;
        }
        variable->set_bool(!variable->as_bool());
        c.print(variable->name() + " = " + variable->value_text());
    });
    add("vars", "[prefix]", "lists the variables", Kind::Tool, [](const Args& args, Console& c) {
        for (const Variable* variable : c.variables_.list(args.empty() ? std::string_view() : std::string_view(args[0]))) {
            c.print(variable->name() + " = " + variable->value_text() + (variable->is_default() ? "" : " *") + ": " + variable->help());
        }
    });
    add("echo", "<text>", "prints a text", Kind::Tool, [](const Args& args, Console& c) {
        std::string text;
        for (const std::string& word : args) {
            text += (text.empty() ? "" : " ") + word;
        }
        c.print(text);
    });
    add("clear", "", "clears the console", Kind::Tool, [](const Args&, Console& c) { c.clear(); });
}

}  // namespace moteur

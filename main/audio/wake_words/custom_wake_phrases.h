#pragma once

#include <string>
#include <utility>
#include <vector>

namespace custom_wake_phrases {

struct Phrase {
    std::string command;
    std::string text;
};

inline std::string Normalize(const std::string& value) {
    std::string result;
    bool space = false;
    for (char ch : value) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v') {
            space = !result.empty();
        } else {
            if (space)
                result += ' ';
            result += ch;
            space = false;
        }
    }
    return result;
}

inline std::vector<std::string> Split(const std::string& value) {
    std::vector<std::string> result;
    size_t start = 0;
    while (true) {
        const auto end = value.find(';', start);
        result.push_back(
            Normalize(value.substr(start, end == std::string::npos ? end : end - start)));
        if (end == std::string::npos)
            return result;
        start = end + 1;
    }
}

// Shared semantics with build_default_assets.py, including legacy alias deduplication.
inline bool Parse(const std::string& primary, const std::string& display,
                  const std::string& aliases, const std::string& alias_displays,
                  std::vector<Phrase>& output) {
    const auto command = Normalize(primary);
    const auto text = Normalize(display);
    if (command.empty() || text.empty())
        return false;
    const auto commands = Split(aliases);
    const auto texts = Split(alias_displays);
    const bool labeled = !alias_displays.empty();
    if (labeled && texts.size() != commands.size())
        return false;
    std::vector<Phrase> result{{command, text}};
    for (size_t i = 0; i < commands.size(); ++i) {
        if (labeled && (commands[i].empty() || texts[i].empty()))
            return false;
        if (commands[i].empty())
            continue;
        bool duplicate = false;
        for (const auto& phrase : result) {
            if (phrase.command == commands[i])
                duplicate = true;
        }
        if (duplicate) {
            if (labeled)
                return false;
            continue;
        }
        result.push_back({commands[i], labeled ? texts[i] : text});
    }
    output = std::move(result);
    return true;
}

}  // namespace custom_wake_phrases

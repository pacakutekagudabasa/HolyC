#pragma once

#include "../lexer/Token.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace holyc {

/// Preprocessor macro definition (object या function-like)।
struct MacroDef {
    std::string name;
    std::vector<std::string> params;  // object macros के लिए empty; "..." exclude होता है
    std::vector<Token> body;
    bool is_function_macro = false;
    bool is_variadic = false;
};

/// Macro definitions का storage और lookup table।
class MacroTable {
public:
    void define(const std::string& name, MacroDef def) {
        macros_.insert_or_assign(name, std::move(def));
    }

    void undef(std::string_view name) {
        macros_.erase(std::string(name));
    }

    MacroDef* lookup(std::string_view name) {
        auto it = macros_.find(std::string(name));
        return it != macros_.end() ? &it->second : nullptr;
    }

    bool isDefined(std::string_view name) const {
        return macros_.count(std::string(name)) != 0;
    }

private:
    std::unordered_map<std::string, MacroDef> macros_;
};

} // namespace holyc

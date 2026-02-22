#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace holyc {

struct Decl;
struct Type;

/**
 * @brief O(1) shadow-map lookup के साथ lexical scope environment।
 *
 * Symbol और type resolution के लिए scopes का stack maintain करता है। एक parallel
 * shadow map किसी भी name के innermost binding का O(1) lookup enable करता है,
 * हर identifier reference पर linear scope-stack walk से बचाता है।
 */
class Env {
public:
    Env() { pushScope(); }

    /**
     * @brief Scope stack पर नया lexical scope push करो।
     */
    void pushScope() { scopes_.emplace_back(); }

    /**
     * @brief Innermost scope pop करो और shadow map से उसके bindings हटाओ।
     *
     * सिर्फ global scope बचा हो तो कोई effect नहीं।
     */
    void popScope() {
        if (scopes_.size() <= 1) return;
        for (auto& [name, decl] : scopes_.back().symbols) {
            auto sit = shadow_.find(name);
            if (sit != shadow_.end() && sit->second.back() == decl) {
                sit->second.pop_back();
                if (sit->second.empty()) shadow_.erase(sit);
            }
        }
        for (auto& [name, type] : scopes_.back().types) {
            auto sit = shadow_types_.find(name);
            if (sit != shadow_types_.end() && sit->second.back() == type) {
                sit->second.pop_back();
                if (sit->second.empty()) shadow_types_.erase(sit);
            }
        }
        scopes_.pop_back();
    }

    /**
     * @brief Current (innermost) scope में symbol define करो।
     *
     * @param name Bind करने वाला symbol name।
     * @param decl Name से associate करने वाला declaration।
     */
    void define(const std::string& name, Decl* decl) {
        scopes_.back().symbols[name] = decl;
        shadow_[name].push_back(decl);
    }

    /**
     * @brief Symbol name का innermost binding lookup करो।
     *
     * @param name Lookup करने वाला symbol name।
     * @return name का innermost Decl*, या नहीं मिला तो nullptr।
     */
    Decl* lookup(const std::string& name) const {
        auto it = shadow_.find(name);
        if (it != shadow_.end() && !it->second.empty())
            return it->second.back();
        return nullptr;
    }

    /**
     * @brief Current (innermost) scope में type alias define करो।
     *
     * @param name Bind करने वाला type name।
     * @param type Name से associate करने वाला Type*।
     */
    void defineType(const std::string& name, Type* type) {
        scopes_.back().types[name] = type;
        shadow_types_[name].push_back(type);
    }

    /**
     * @brief Name का innermost type binding lookup करो।
     *
     * @param name Lookup करने वाला type name।
     * @return name का innermost Type*, या नहीं मिला तो nullptr।
     */
    Type* lookupType(const std::string& name) const {
        auto it = shadow_types_.find(name);
        if (it != shadow_types_.end() && !it->second.empty())
            return it->second.back();
        return nullptr;
    }

    /**
     * @brief maxDist के अंदर edit distance वाला nearest symbol लौटाओ, या empty string।
     *
     * Undeclared-identifier errors पर "did you mean" suggestions के लिए use होता है।
     *
     * @param name Search करने वाला misspelled name।
     * @param maxDist Match consider करने के लिए maximum edit distance।
     * @return Closest matching symbol name, या कोई maxDist के अंदर नहीं तो ""।
     */
    std::string suggest(const std::string& name, unsigned maxDist = 3) const {
        std::string best;
        unsigned bestDist = maxDist + 1;
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            for (auto& [sym, _] : it->symbols) {
                unsigned d = editDistance(name, sym);
                if (d < bestDist) { bestDist = d; best = sym; }
            }
        }
        return bestDist <= maxDist ? best : "";
    }

    /**
     * @brief Global (bottom) scope में symbol define करो।
     *
     * Binding shadow stack की शुरुआत में insert होता है ताकि सभी scopes से
     * visible हो लेकिन inner definitions से shadow हो सके।
     *
     * @param name Globally bind करने वाला symbol name।
     * @param decl Name से associate करने वाला declaration।
     */
    void defineGlobal(const std::string& name, Decl* decl) {
        scopes_.front().symbols[name] = decl;
        auto& stack = shadow_[name];
        stack.insert(stack.begin(), decl);
    }

    /**
     * @brief Global (bottom) scope में type alias define करो।
     *
     * @param name Globally bind करने वाला type name।
     * @param type Name से associate करने वाला Type*।
     */
    void defineGlobalType(const std::string& name, Type* type) {
        scopes_.front().types[name] = type;
        auto& stack = shadow_types_[name];
        stack.insert(stack.begin(), type);
    }

private:
    struct Scope {
        std::unordered_map<std::string, Decl*> symbols;
        std::unordered_map<std::string, Type*> types;
    };
    std::vector<Scope> scopes_;

    std::unordered_map<std::string, std::vector<Decl*>> shadow_;
    std::unordered_map<std::string, std::vector<Type*>> shadow_types_;

    /**
     * @brief दो strings के बीच Levenshtein edit distance compute करो।
     *
     * Length difference 4 से ज़्यादा हो तो तुरंत 99 return करता है, clearly
     * dissimilar strings के लिए unnecessary work से बचाता है।
     *
     * @param a पहला string।
     * @param b दूसरा string।
     * @return Edit distance, या lengths 4 से ज़्यादा differ करें तो 99।
     */
    static unsigned editDistance(const std::string& a, const std::string& b) {
        const size_t m = a.size(), n = b.size();
        if (m == 0) return (unsigned)n;
        if (n == 0) return (unsigned)m;
        if (m > n + 4 || n > m + 4) return 99;
        std::vector<unsigned> prev(n + 1), cur(n + 1);
        for (size_t j = 0; j <= n; ++j) prev[j] = (unsigned)j;
        for (size_t i = 1; i <= m; ++i) {
            cur[0] = (unsigned)i;
            for (size_t j = 1; j <= n; ++j) {
                unsigned cost = (a[i-1] == b[j-1]) ? 0 : 1;
                cur[j] = std::min({prev[j] + 1, cur[j-1] + 1, prev[j-1] + cost});
            }
            std::swap(prev, cur);
        }
        return prev[n];
    }
};

} // namespace holyc

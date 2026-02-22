#include "CHeaderImport.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <regex>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

namespace holyc {

// C type → HolyC type mapping — C types को HolyC equivalent types में बदलना
static const std::unordered_map<std::string, std::string> kTypeMap = {
    {"void",               "U0"},
    {"char",               "I8"},
    {"signed char",        "I8"},
    {"unsigned char",      "U8"},
    {"short",              "I16"},
    {"short int",          "I16"},
    {"signed short",       "I16"},
    {"signed short int",   "I16"},
    {"unsigned short",     "U16"},
    {"unsigned short int", "U16"},
    {"int",                "I32"},
    {"signed",             "I32"},
    {"signed int",         "I32"},
    {"unsigned",           "U32"},
    {"unsigned int",       "U32"},
    {"long",               "I64"},
    {"long int",           "I64"},
    {"signed long",        "I64"},
    {"signed long int",    "I64"},
    {"unsigned long",      "U64"},
    {"unsigned long int",  "U64"},
    {"long long",          "I64"},
    {"long long int",      "I64"},
    {"signed long long",   "I64"},
    {"signed long long int","I64"},
    {"unsigned long long", "U64"},
    {"unsigned long long int","U64"},
    {"float",              "F32"},
    {"double",             "F64"},
    {"long double",        "F64"},
    {"size_t",             "U64"},
    {"ssize_t",            "I64"},
    {"ptrdiff_t",          "I64"},
    {"intptr_t",           "I64"},
    {"uintptr_t",          "U64"},
    {"int8_t",             "I8"},
    {"uint8_t",            "U8"},
    {"int16_t",            "I16"},
    {"uint16_t",           "U16"},
    {"int32_t",            "I32"},
    {"uint32_t",           "U32"},
    {"int64_t",            "I64"},
    {"uint64_t",           "U64"},
};

/**
 * @brief String से leading और trailing whitespace strip करो।
 *
 * @param s Trim करने वाला string।
 * @return @p s की copy जिसमें leading और trailing whitespace remove हो।
 */
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

/**
 * @brief Whitespace के runs को single spaces में collapse करो और result trim करो।
 *
 * @param s Normalize करने वाला string।
 * @return @p s की copy जिसमें हर whitespace run single space से replace हो और trimmed हो।
 */
static std::string normalizeSpaces(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_space = false;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) {
            if (!out.empty() && !in_space) { out += ' '; in_space = true; }
        } else {
            out += c;
            in_space = false;
        }
    }
    return trim(out);
}

std::string CHeaderImport::cTypeToHolyC(const std::string& raw) {
    // Leading/trailing qualifiers strip करो: const, volatile, restrict, __restrict
    static const std::regex kQualRe(
        R"(\b(const|volatile|restrict|__restrict__|__restrict|__signed__|__extension__|_Noreturn|__cdecl|__attribute__\s*\(.*?\))\b)",
        std::regex::optimize);
    std::string t = std::regex_replace(raw, kQualRe, "");
    t = normalizeSpaces(t);

    // Pointer strip करो: call site पर handle होता है
    // Pointer suffix ढूंढो
    bool is_ptr = false;
    size_t star = t.rfind('*');
    std::string base = t;
    if (star != std::string::npos) {
        is_ptr = true;
        base = trim(t.substr(0, star));
    }

    std::string holyc;
    auto it = kTypeMap.find(base);
    if (it != kTypeMap.end()) {
        holyc = it->second;
    } else if (!base.empty()) {
        // Unknown type: safe generic pointer placeholder के रूप में U8* use करो
        holyc = "U8";
        is_ptr = true;
    } else {
        holyc = "U8";
    }

    return is_ptr ? (holyc + "*") : holyc;
}

std::string CHeaderImport::convertParamList(const std::string& params, bool& is_vararg) {
    is_vararg = false;
    std::string p = trim(params);
    if (p.empty() || p == "void") return "";

    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (char c : p) {
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') depth--;
        else if (c == ',' && depth == 0) {
            parts.push_back(trim(current));
            current.clear();
            continue;
        }
        current += c;
    }
    if (!trim(current).empty()) parts.push_back(trim(current));

    std::string result;
    int idx = 0;
    for (auto& part : parts) {
        part = trim(part);
        if (part == "...") {
            is_vararg = true;
            continue;
        }
        // हर parameter convert करो: सिर्फ type convert करो (param name ignore करो)
        // Array brackets, function pointer complexity remove करो - उनके लिए U8* use करो
        std::string holyc_type;
        if (part.find('(') != std::string::npos) {
            holyc_type = "U8*"; // function pointer → opaque pointer के रूप में treat करो
        } else {
            holyc_type = cTypeToHolyC(part);
        }
        if (!result.empty()) result += ", ";
        result += holyc_type + " p" + std::to_string(idx++);
    }
    return result;
}

/**
 * @brief True लौटाता है अगर दिया गया path C preprocessor को pass करना safe है।
 *
 * Path traversal components (जैसे "..") और shell-unsafe characters को reject करता है
 * ताकि command injection न हो।
 *
 * @param p Validate करने वाला absolute path string।
 * @return Path safe हो तो true; नहीं तो false।
 */
static bool isSafeHeaderPath(const std::string& p) {
    if (p.empty() || p.size() > 512) return false;
    // Parent-directory traversal components reject करो; absolute paths allowed हैं
    // क्योंकि callers यहाँ call करने से पहले trusted system directories के against resolve करते हैं।
    if (p.find("/../") != std::string::npos) return false;
    if (p.find("../") == 0) return false;
    if (p.size() >= 3 && p.substr(p.size() - 3) == "/..") return false;
    // Shell metacharacter injection prevent करने के लिए सिर्फ safe characters allow करो।
    for (char c : p)
        if (!std::isalnum((unsigned char)c) && c != '/' && c != '.' &&
            c != '_' && c != '-')
            return false;
    return true;
}

std::string CHeaderImport::runCPreprocessor(const std::string& headerPath) {
    if (!isSafeHeaderPath(headerPath)) return "";

    int pipefd[2];
    if (pipe(pipefd) < 0) return "";

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return "";
    }

    if (pid == 0) {
        // child: stdout -> pipe का write end
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        // stderr को /dev/null पर redirect करो
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execlp("cc", "cc", "-E", "-P", "-x", "c",
               headerPath.c_str(), (char*)nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        result.append(buf, (size_t)n);
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return result;
}

std::vector<std::string> CHeaderImport::extractFuncDecls(const std::string& cpp_output) {
    std::vector<std::string> results;

    // Normalize: continuation lines join करो, comments remove करो
    std::string src = cpp_output;

    // Line-continuation remove करो
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '\\' && i + 1 < src.size() && src[i+1] == '\n') {
            src[i] = ' '; src[i+1] = ' ';
        }
    }

    // Single-line comments remove करो
    {
        std::string cleaned;
        size_t i = 0;
        while (i < src.size()) {
            if (i + 1 < src.size() && src[i] == '/' && src[i+1] == '/') {
                while (i < src.size() && src[i] != '\n') i++;
            } else if (i + 1 < src.size() && src[i] == '/' && src[i+1] == '*') {
                i += 2;
                while (i + 1 < src.size() && !(src[i] == '*' && src[i+1] == '/')) i++;
                i += 2;
            } else {
                cleaned += src[i++];
            }
        }
        src = std::move(cleaned);
    }

    // C function declarations match करो:
    // Pattern: identifier ( param-list ) ; — function declaration का expected format
    // हम ऐसे patterns ढूंढते हैं: type name ( params ) ;
    // यह regex ज़्यादातर simple cases handle करता है। Complex macros/attributes skip होते हैं।
    static const std::regex kFuncDeclRe(
        // return type (possibly pointer, multi-word) + function name + ( params ) ; — regex pattern (C function declarations ढूंढने के लिए)
        R"((?:^|;|\})[\s\n]*((?:(?:unsigned|signed|long|short|const|volatile|struct|enum|union)\s+)*\w[\w\s\*]*?)\s+(\*?\w+)\s*\(([^)]*)\)\s*;)",
        std::regex::optimize | std::regex::multiline);

    auto begin = std::sregex_iterator(src.begin(), src.end(), kFuncDeclRe);
    auto end   = std::sregex_iterator();

    std::unordered_map<std::string, bool> seen;
    for (auto it = begin; it != end; ++it) {
        const auto& m = *it;
        std::string ret_type = trim(m[1].str());
        std::string func_name = trim(m[2].str());
        std::string params    = trim(m[3].str());

        // अगर name keyword, type, या macro जैसा लगे तो skip करो
        if (func_name.empty() || func_name[0] == '_') continue;
        if (seen.count(func_name)) continue;
        seen[func_name] = true;

        // struct/typedef declarations जैसी types skip करो
        if (ret_type.find("struct") != std::string::npos ||
            ret_type.find("enum")   != std::string::npos ||
            ret_type.find("union")  != std::string::npos) continue;

        // Function name पर leading * handle करो (pointer return करने वाले function)
        bool ret_ptr = false;
        if (!func_name.empty() && func_name[0] == '*') {
            func_name = func_name.substr(1);
            ret_ptr = true;
        }

        std::string holyc_ret = cTypeToHolyC(ret_type);
        if (ret_ptr && holyc_ret.back() != '*') holyc_ret += "*";

        bool is_vararg = false;
        std::string holyc_params = convertParamList(params, is_vararg);

        std::string decl = "extern " + holyc_ret + " " + func_name + "("
                         + holyc_params
                         + (is_vararg ? (holyc_params.empty() ? ".." : ", ..") : "")
                         + ");";
        results.push_back(std::move(decl));
    }
    return results;
}

std::string CHeaderImport::import(const std::string& headerPath,
                                   const std::string& /*headerName*/) {
    std::string cpp_out = runCPreprocessor(headerPath);
    if (cpp_out.empty()) return "";

    auto decls = extractFuncDecls(cpp_out);
    if (decls.empty()) return "";

    std::string result;
    result.reserve(decls.size() * 50);
    for (auto& d : decls) {
        result += d;
        result += '\n';
    }
    return result;
}

} // namespace holyc

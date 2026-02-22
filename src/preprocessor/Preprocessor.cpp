#include "Preprocessor.h"
#include "CHeaderImport.h"
#include "../parser/Parser.h"
#include "../interpreter/Interpreter.h"

#include <algorithm>
#include <cassert>
#include <filesystem>

namespace holyc {

Preprocessor::Preprocessor(SourceManager& sm, Diagnostics& diag, int fileId)
    : sm_(sm), diag_(diag)
{
    include_stack_.push_back({
        std::make_unique<Lexer>(sm, fileId, diag), fileId
    });
}

Token Preprocessor::rawNext() {
    while (!include_stack_.empty()) {
        Token tok = include_stack_.back().lexer->next();
        if (tok.kind != TokenKind::Eof) return tok;
        if (include_stack_.size() > 1) {
            include_stack_.pop_back();
            continue;
        }
        return tok;
    }
    return Token(TokenKind::Eof, {}, {});
}

std::vector<Token> Preprocessor::collectLineTokens(uint32_t line) {
    std::vector<Token> result;
    Lexer& lex = *include_stack_.back().lexer;

    if (line == 0) {
        Token first = lex.peek();
        if (first.kind == TokenKind::Eof) return result;
        line = first.loc.line;
    }

    uint32_t current_line = line;
    while (true) {
        Token t = lex.peek();
        if (t.kind == TokenKind::Eof) break;
        if (t.loc.line != current_line) break;
        result.push_back(lex.next());
    }

    while (!result.empty() && result.back().kind == TokenKind::Error &&
           !result.back().text.empty() && result.back().text[0] == '\\') {
        result.pop_back();
        Token t = lex.peek();
        if (t.kind == TokenKind::Eof) break;
        current_line = t.loc.line;
        while (true) {
            Token t2 = lex.peek();
            if (t2.kind == TokenKind::Eof || t2.loc.line != current_line) break;
            result.push_back(lex.next());
        }
    }

    return result;
}

void Preprocessor::skipConditionalBlock() {
    int depth = 1;
    while (depth > 0) {
        Token tok = rawNext();
        if (tok.kind == TokenKind::Eof) {
            diag_.error(tok.loc, "unterminated conditional directive");
            return;
        }
        switch (tok.kind) {
        case TokenKind::PP_Ifdef:
        case TokenKind::PP_Ifndef:
        case TokenKind::PP_If:
        case TokenKind::PP_Ifaot:
        case TokenKind::PP_Ifjit:
            depth++;
            break;
        case TokenKind::PP_Endif:
            depth--;
            if (depth == 0) return;
            break;
        case TokenKind::PP_Else:
            if (depth == 1) {
                condSetBack(CondState::Active);
                return;
            }
            break;
        case TokenKind::PP_Elif:
            if (depth == 1) {
                auto tokens = collectLineTokens();
                int64_t val = evalConstExpr(tokens);
                if (val) {
                    condSetBack(CondState::Active);
                    return;
                }
            }
            break;
        default:
            break;
        }
    }
}

void Preprocessor::handleDefine() {
    Lexer& lex = *include_stack_.back().lexer;
    Token name = lex.next();
    if (name.kind == TokenKind::Eof || name.text.empty()) {
        diag_.error(name.loc, "expected identifier after #define");
        return;
    }

    MacroDef def;
    def.name = std::string(name.text);

    // Function-like macros के लिए '(' directly name के बाद आना चाहिए (कोई whitespace नहीं)।
    Token peeked = lex.peek();
    if (peeked.kind == TokenKind::LParen &&
        peeked.loc.line == name.loc.line &&
        peeked.loc.col == name.loc.col + name.text.size()) {
        def.is_function_macro = true;
        lex.next(); // '(' consume करो
        while (true) {
            Token p = lex.next();
            if (p.kind == TokenKind::RParen) break;
            if (p.kind == TokenKind::Identifier) {
                def.params.push_back(std::string(p.text));
                Token sep = lex.peek();
                if (sep.kind == TokenKind::Comma) lex.next();
            } else if (p.kind == TokenKind::DotDot) {
                def.is_variadic = true;
                Token rp = lex.next();
                if (rp.kind != TokenKind::RParen) {
                    diag_.error(rp.loc, "'...' must be the last macro parameter");
                }
                break;
            } else if (p.kind == TokenKind::Eof) {
                diag_.error(p.loc, "unterminated macro parameter list");
                return;
            } else {
                diag_.error(p.loc, "expected parameter name in macro definition");
                return;
            }
        }
    }

    def.body = collectLineTokens(name.loc.line);
    std::string macroName = def.name;
    macros_.define(macroName, std::move(def));
}

void Preprocessor::handleUndef() {
    Lexer& lex = *include_stack_.back().lexer;
    Token name = lex.next();
    if (name.kind == TokenKind::Eof || name.text.empty()) {
        diag_.error(name.loc, "expected identifier after #undef");
        return;
    }
    macros_.undef(name.text);
}

void Preprocessor::handleInclude(const Token& directive) {
    if (include_stack_.size() >= static_cast<size_t>(kMaxIncludeDepth)) {
        diag_.error(directive.loc, "include depth exceeded (max 32)");
        return;
    }

    Lexer& lex = *include_stack_.back().lexer;
    Token path = lex.next();

    if (path.kind == TokenKind::Less) {
        std::string headerName;
        while (true) {
            Token t = lex.next();
            if (t.kind == TokenKind::Greater || t.kind == TokenKind::Eof) break;
            headerName += std::string(t.text);
        }
        if (headerName.empty()) {
            diag_.error(directive.loc, "empty angle-bracket include");
            return;
        }
        if (headerName.size() >= 2 && headerName.rfind(".h") == headerName.size() - 2) {
            std::string resolvedPath;
            const char* sysPaths[] = {
                "/usr/include", "/usr/local/include",
                "/usr/include/x86_64-linux-gnu", "/usr/include/aarch64-linux-gnu",
                nullptr
            };
            for (const char** sp = sysPaths; *sp; ++sp) {
                std::filesystem::path candidate = std::filesystem::path(*sp) / headerName;
                if (std::filesystem::exists(candidate)) {
                    resolvedPath = candidate.string();
                    break;
                }
            }
            if (!resolvedPath.empty()) {
                std::string decls = CHeaderImport::import(resolvedPath, headerName);
                if (!decls.empty()) {
                    std::string bufName = "<c-import:" + headerName + ">";
                    int syntheticId = sm_.loadString(bufName, decls);
                    include_stack_.push_back({
                        std::make_unique<Lexer>(sm_, syntheticId, diag_), syntheticId
                    });
                }
            }
        }
        return;
    }

    if (path.kind != TokenKind::StringLiteral) {
        diag_.error(path.loc, "expected string literal after #include");
        return;
    }

    std::string_view raw = path.text;
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
    }
    std::string includePath(raw);

    int currentFileId = include_stack_.back().fileId;
    std::string currentFile = sm_.getFileName(currentFileId);
    std::filesystem::path base = std::filesystem::path(currentFile).parent_path();
    std::filesystem::path resolved = base / includePath;
    std::string resolvedStr = resolved.string();

    auto checkTraversal = [&](const std::string& checkPath) -> bool {
        if (checkPath.find("/../") != std::string::npos ||
            checkPath.find("../") == 0 ||
            (checkPath.size() >= 3 && checkPath.substr(checkPath.size()-3) == "/..")) {
            return true;
        }
        size_t pos = 0;
        while ((pos = checkPath.find("..", pos)) != std::string::npos) {
            bool leftOk  = (pos == 0 || checkPath[pos-1] == '/');
            bool rightOk = (pos+2 >= checkPath.size() || checkPath[pos+2] == '/');
            if (leftOk && rightOk) return true;
            pos += 2;
        }
        return false;
    };

    if (checkTraversal(resolvedStr)) {
        diag_.error(directive.loc, "include path traversal rejected: " + resolvedStr);
        return;
    }

    int newFileId = sm_.loadFile(resolvedStr);
    if (newFileId < 0) {
        for (auto& ipath : extra_include_paths_) {
            std::filesystem::path alt = std::filesystem::path(ipath) / includePath;
            std::string altStr = alt.string();
            if (checkTraversal(altStr)) continue;
            int altId = sm_.loadFile(altStr);
            if (altId >= 0) {
                resolvedStr = altStr;
                newFileId = altId;
                break;
            }
        }
        if (newFileId < 0) {
            std::filesystem::path cwdPath = std::filesystem::current_path() / includePath;
            std::string cwdStr = cwdPath.string();
            if (!checkTraversal(cwdStr)) {
                int cwdId = sm_.loadFile(cwdStr);
                if (cwdId >= 0) {
                    resolvedStr = cwdStr;
                    newFileId = cwdId;
                }
            }
        }
    }

    for (auto& frame : include_stack_) {
        if (sm_.getFileName(frame.fileId) == resolvedStr) {
            diag_.error(directive.loc, "circular include detected: " + resolvedStr);
            return;
        }
    }

    if (newFileId < 0) {
        diag_.error(directive.loc, "cannot open include file: " + resolvedStr);
        return;
    }

    include_stack_.push_back({
        std::make_unique<Lexer>(sm_, newFileId, diag_), newFileId
    });
}

void Preprocessor::handleIfdef(bool sense) {
    Lexer& lex = *include_stack_.back().lexer;
    Token name = lex.next();
    if (name.kind == TokenKind::Eof || name.text.empty()) {
        diag_.error(name.loc, "expected identifier after #ifdef/#ifndef");
        return;
    }
    bool defined = macros_.isDefined(name.text);
    bool active = sense ? defined : !defined;
    if (active) {
        condPush(CondState::Active);
    } else {
        condPush(CondState::SkipToElse);
        skipConditionalBlock();
        // skipConditionalBlock ने #else के बिना #endif ढूंढा तो unused frame pop करो।
        if (!cond_stack_.empty() && cond_stack_.back() == CondState::SkipToElse)
            condPop(); // #endif skipConditionalBlock के अंदर consume हुआ; अब-dead frame pop करो
    }
}

void Preprocessor::handleIf(const Token& /*directive*/) {
    auto tokens = collectLineTokens();
    int64_t val = evalConstExpr(tokens);
    if (val) {
        condPush(CondState::Active);
    } else {
        condPush(CondState::SkipToElse);
        skipConditionalBlock();
        if (!cond_stack_.empty() && cond_stack_.back() == CondState::SkipToElse)
            condPop();
    }
}

void Preprocessor::handleElif() {
    if (cond_stack_.empty()) {
        diag_.error({}, "#elif without #if");
        return;
    }
    condSetBack(CondState::SkipToEnd);
    collectLineTokens();
    int depth = 1;
    while (depth > 0) {
        Token tok = rawNext();
        if (tok.kind == TokenKind::Eof) {
            diag_.error(tok.loc, "unterminated conditional directive");
            return;
        }
        switch (tok.kind) {
        case TokenKind::PP_Ifdef:
        case TokenKind::PP_Ifndef:
        case TokenKind::PP_If:
        case TokenKind::PP_Ifaot:
        case TokenKind::PP_Ifjit:
            depth++;
            break;
        case TokenKind::PP_Endif:
            depth--;
            break;
        default:
            break;
        }
    }
    condPop();
}

void Preprocessor::handleElse() {
    if (cond_stack_.empty()) {
        diag_.error({}, "#else without #if");
        return;
    }
    condSetBack(CondState::SkipToEnd);
    int depth = 1;
    while (depth > 0) {
        Token tok = rawNext();
        if (tok.kind == TokenKind::Eof) {
            diag_.error(tok.loc, "unterminated conditional directive");
            return;
        }
        switch (tok.kind) {
        case TokenKind::PP_Ifdef:
        case TokenKind::PP_Ifndef:
        case TokenKind::PP_If:
        case TokenKind::PP_Ifaot:
        case TokenKind::PP_Ifjit:
            depth++;
            break;
        case TokenKind::PP_Endif:
            depth--;
            break;
        default:
            break;
        }
    }
    condPop();
}

void Preprocessor::handleEndif() {
    if (cond_stack_.empty()) {
        diag_.error({}, "#endif without #if");
        return;
    }
    condPop();
}

void Preprocessor::handleIfaot() {
    if (aot_mode_) {
        condPush(CondState::Active);
    } else {
        condPush(CondState::SkipToElse);
        skipConditionalBlock();
        if (!cond_stack_.empty() && cond_stack_.back() == CondState::SkipToElse)
            condPop();
    }
}

void Preprocessor::handleIfjit() {
    if (!aot_mode_) {
        condPush(CondState::Active);
    } else {
        condPush(CondState::SkipToElse);
        skipConditionalBlock();
        if (!cond_stack_.empty() && cond_stack_.back() == CondState::SkipToElse)
            condPop();
    }
}

void Preprocessor::handleAssert(const Token& directive) {
    auto tokens = collectLineTokens();
    int64_t val = evalConstExpr(tokens);
    if (val == 0) {
        diag_.error(directive.loc, "#assert failed");
    }
}

int64_t Preprocessor::evalConstExpr(const std::vector<Token>& tokens) {
    if (tokens.empty()) return 0;
    size_t pos = 0;
    return evalOr(tokens, pos);
}

int64_t Preprocessor::evalOr(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalAnd(tokens, pos);
    while (pos < tokens.size() && tokens[pos].kind == TokenKind::PipePipe) {
        pos++;
        int64_t right = evalAnd(tokens, pos);
        left = (left || right) ? 1 : 0;
    }
    return left;
}

int64_t Preprocessor::evalAnd(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalBitwiseOr(tokens, pos);
    while (pos < tokens.size() && tokens[pos].kind == TokenKind::AmpAmp) {
        pos++;
        int64_t right = evalBitwiseOr(tokens, pos);
        left = (left && right) ? 1 : 0;
    }
    return left;
}

int64_t Preprocessor::evalBitwiseOr(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalBitwiseXor(tokens, pos);
    while (pos < tokens.size() && tokens[pos].kind == TokenKind::Pipe) {
        pos++;
        left |= evalBitwiseXor(tokens, pos);
    }
    return left;
}

int64_t Preprocessor::evalBitwiseXor(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalBitwiseAnd(tokens, pos);
    while (pos < tokens.size() && tokens[pos].kind == TokenKind::Caret) {
        pos++;
        left ^= evalBitwiseAnd(tokens, pos);
    }
    return left;
}

int64_t Preprocessor::evalBitwiseAnd(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalEquality(tokens, pos);
    while (pos < tokens.size() && tokens[pos].kind == TokenKind::Ampersand) {
        pos++;
        left &= evalEquality(tokens, pos);
    }
    return left;
}

int64_t Preprocessor::evalEquality(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalRelational(tokens, pos);
    while (pos < tokens.size()) {
        TokenKind k = tokens[pos].kind;
        if (k == TokenKind::EqEq) { pos++; left = (left == evalRelational(tokens, pos)) ? 1 : 0; }
        else if (k == TokenKind::BangEq) { pos++; left = (left != evalRelational(tokens, pos)) ? 1 : 0; }
        else break;
    }
    return left;
}

int64_t Preprocessor::evalRelational(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalShift(tokens, pos);
    while (pos < tokens.size()) {
        TokenKind k = tokens[pos].kind;
        if (k == TokenKind::Less) { pos++; left = (left < evalShift(tokens, pos)) ? 1 : 0; }
        else if (k == TokenKind::Greater) { pos++; left = (left > evalShift(tokens, pos)) ? 1 : 0; }
        else if (k == TokenKind::LessEq) { pos++; left = (left <= evalShift(tokens, pos)) ? 1 : 0; }
        else if (k == TokenKind::GreaterEq) { pos++; left = (left >= evalShift(tokens, pos)) ? 1 : 0; }
        else break;
    }
    return left;
}

int64_t Preprocessor::evalShift(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalAdditive(tokens, pos);
    while (pos < tokens.size()) {
        TokenKind k = tokens[pos].kind;
        if (k == TokenKind::Shl) {
            SourceLocation shiftLoc = tokens[pos].loc;
            pos++;
            int64_t rhs = evalAdditive(tokens, pos);
            if (rhs < 0 || rhs >= 64) {
                diag_.warning(shiftLoc, "shift amount out of [0,63] range; result set to 0");
                left = 0;
            } else {
                left <<= rhs;
            }
        } else if (k == TokenKind::Shr) {
            SourceLocation shiftLoc = tokens[pos].loc;
            pos++;
            int64_t rhs = evalAdditive(tokens, pos);
            if (rhs < 0 || rhs >= 64) {
                diag_.warning(shiftLoc, "shift amount out of [0,63] range; result set to 0");
                left = 0;
            } else {
                left >>= rhs;
            }
        } else break;
    }
    return left;
}

int64_t Preprocessor::evalAdditive(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalMultiplicative(tokens, pos);
    while (pos < tokens.size()) {
        TokenKind k = tokens[pos].kind;
        if (k == TokenKind::Plus) {
            pos++;
            int64_t rhs = evalMultiplicative(tokens, pos);
            if (rhs > 0 && left > INT64_MAX - rhs) left = INT64_MAX;
            else if (rhs < 0 && left < INT64_MIN - rhs) left = INT64_MIN;
            else left += rhs;
        } else if (k == TokenKind::Minus) {
            pos++;
            int64_t rhs = evalMultiplicative(tokens, pos);
            if (rhs < 0 && left > INT64_MAX + rhs) left = INT64_MAX;
            else if (rhs > 0 && left < INT64_MIN + rhs) left = INT64_MIN;
            else left -= rhs;
        } else break;
    }
    return left;
}

int64_t Preprocessor::evalMultiplicative(const std::vector<Token>& tokens, size_t& pos) {
    int64_t left = evalUnary(tokens, pos);
    while (pos < tokens.size()) {
        TokenKind k = tokens[pos].kind;
        if (k == TokenKind::Star) {
            pos++;
            int64_t rhs = evalUnary(tokens, pos);
            if (left == 0 || rhs == 0) {
                left = 0;
            } else {
                int64_t absL = (left == INT64_MIN) ? INT64_MAX : (left < 0 ? -left : left);
                int64_t absR = (rhs  == INT64_MIN) ? INT64_MAX : (rhs  < 0 ? -rhs  : rhs);
                if (absL > INT64_MAX / absR) {
                    left = ((left > 0) == (rhs > 0)) ? INT64_MAX : INT64_MIN;
                } else {
                    left *= rhs;
                }
            }
        } else if (k == TokenKind::Slash) {
            pos++;
            int64_t right = evalUnary(tokens, pos);
            left = right != 0 ? left / right : 0;
        }
        else if (k == TokenKind::Percent) {
            pos++;
            int64_t right = evalUnary(tokens, pos);
            left = right != 0 ? left % right : 0;
        }
        else break;
    }
    return left;
}

int64_t Preprocessor::evalUnary(const std::vector<Token>& tokens, size_t& pos) {
    if (pos < tokens.size()) {
        if (tokens[pos].kind == TokenKind::Bang) {
            pos++;
            return evalUnary(tokens, pos) ? 0 : 1;
        }
        if (tokens[pos].kind == TokenKind::Tilde) {
            pos++;
            return ~evalUnary(tokens, pos);
        }
        if (tokens[pos].kind == TokenKind::Minus) {
            pos++;
            return -evalUnary(tokens, pos);
        }
        if (tokens[pos].kind == TokenKind::Plus) {
            pos++;
            return evalUnary(tokens, pos);
        }
    }
    return evalPrimary(tokens, pos);
}

int64_t Preprocessor::evalPrimary(const std::vector<Token>& tokens, size_t& pos) {
    if (pos >= tokens.size()) return 0;

    Token tok = tokens[pos];

    if (tok.kind == TokenKind::IntLiteral) {
        pos++;
        // #if arithmetic defined रहे इसलिए INT64_MAX पर clamp करो।
        if (tok.uintVal > static_cast<uint64_t>(INT64_MAX))
            return INT64_MAX;
        return static_cast<int64_t>(tok.uintVal);
    }

    if (tok.kind == TokenKind::CharLiteral) {
        pos++;
        if (tok.uintVal > static_cast<uint64_t>(INT64_MAX))
            return INT64_MAX;
        return static_cast<int64_t>(tok.uintVal);
    }

    if (tok.kind == TokenKind::True) { pos++; return 1; }
    if (tok.kind == TokenKind::False) { pos++; return 0; }

    if (tok.kind == TokenKind::Identifier && tok.text == "defined") {
        pos++;
        bool hasParen = false;
        if (pos < tokens.size() && tokens[pos].kind == TokenKind::LParen) {
            hasParen = true;
            pos++;
        }
        if (pos >= tokens.size() || tokens[pos].kind != TokenKind::Identifier) {
            return 0;
        }
        std::string name(tokens[pos].text);
        pos++;
        if (hasParen) {
            if (pos < tokens.size() && tokens[pos].kind == TokenKind::RParen)
                pos++;
        }
        return macros_.isDefined(name) ? 1 : 0;
    }

    if (tok.kind == TokenKind::Identifier) {
        pos++;
        std::string name(tok.text);
        MacroDef* def = macros_.lookup(name);
        if (def && !def->is_function_macro && !def->body.empty())
            return evalConstExpr(def->body);
        return 0;
    }

    if (tok.kind == TokenKind::LParen) {
        pos++;
        int64_t val = evalOr(tokens, pos);
        if (pos < tokens.size() && tokens[pos].kind == TokenKind::RParen)
            pos++;
        return val;
    }

    pos++;
    return 0;
}

/**
 * @brief Token का display text लौटाओ, या text empty हो तो kind name।
 *
 * @param tok Display text चाहिए वाला token।
 * @return Token का text, या text field empty हो तो kind name string।
 */
static std::string tokenText(const Token& tok) {
    if (!tok.text.empty()) return std::string(tok.text);
    return std::string(tokenKindToString(tok.kind));
}

/**
 * @brief Token texts को spaces से join करो, stringification के लिए use होता है।
 *
 * @param toks वह token sequence जिनके text values join करने हैं।
 * @return सभी token texts का single space-separated string।
 */
static std::string tokensToString(const std::vector<Token>& toks) {
    std::string result;
    for (size_t i = 0; i < toks.size(); i++) {
        if (i > 0) result += ' ';
        result += tokenText(toks[i]);
    }
    return result;
}

/**
 * @brief String में double-quotes और backslashes escape करो ताकि string literal के अंदर use हो।
 *
 * @param s Escape करने वाला raw string।
 * @return @p s की copy जिसमें '"' '\\"' से और '\\' '\\\\' से replace हो।
 */
static std::string escapeForString(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else { out += c; }
    }
    return out;
}

bool Preprocessor::tryExpandMacro(const Token& ident, std::deque<Token>& output) {
    if (expand_depth_ >= kMaxExpandDepth) {
        diag_.error(ident.loc, "macro expansion too deep (possible recursive macro)");
        return false;
    }
    expand_depth_++;
    struct ExpandDepthGuard {
        int& d;
        ExpandDepthGuard(int& d) : d(d) {}
        ~ExpandDepthGuard() { d--; }
    } depthGuard(expand_depth_);

    std::string name(ident.text);

    if (name == "__FILE__") {
        Token tok = ident;
        tok.kind = TokenKind::StringLiteral;
        int fileId = include_stack_.back().fileId;
        std::string fname_copy = "\"" + std::string(sm_.getFileName(fileId)) + "\"";
        tok.text = synthText(std::move(fname_copy));
        output.push_back(tok);
        return true;
    }
    if (name == "__LINE__") {
        Token tok = ident;
        tok.kind = TokenKind::IntLiteral;
        tok.intVal = static_cast<int64_t>(ident.loc.line);
        output.push_back(tok);
        return true;
    }

    if (expanding_.count(name)) return false;

    MacroDef* def = macros_.lookup(name);
    if (!def) return false;

    if (!def->is_function_macro) {
        expanding_.insert(name);
        for (auto& bodyTok : def->body) {
            if (bodyTok.kind == TokenKind::Identifier) {
                std::deque<Token> expanded;
                if (tryExpandMacro(bodyTok, expanded)) {
                    for (auto& e : expanded) output.push_back(e);
                    continue;
                }
            }
            output.push_back(bodyTok);
        }
        expanding_.erase(name);
        return true;
    }

    Lexer& lex = *include_stack_.back().lexer;
    Token peeked = lex.peek();
    if (peeked.kind != TokenKind::LParen) return false;

    lex.next(); // '(' consume करो

    std::vector<std::vector<Token>> args;
    args.emplace_back();
    int parenDepth = 1;
    while (true) {
        Token t = rawNext();
        if (t.kind == TokenKind::Eof) {
            diag_.error(ident.loc, "unterminated macro argument list");
            return true;
        }
        if (t.kind == TokenKind::LParen) {
            parenDepth++;
            args.back().push_back(t);
        } else if (t.kind == TokenKind::RParen) {
            parenDepth--;
            if (parenDepth == 0) break;
            args.back().push_back(t);
        } else if (t.kind == TokenKind::Comma && parenDepth == 1) {
            args.emplace_back();
        } else {
            args.back().push_back(t);
        }
    }

    // MACRO() बिना params के: एक empty arg → zero args treat करो।
    if (def->params.empty() && !def->is_variadic && args.size() == 1 && args[0].empty()) {
        args.clear();
    }

    static constexpr size_t kMaxMacroArgs = 256;
    if (args.size() > kMaxMacroArgs) {
        diag_.error(ident.loc, "too many macro arguments (max 256)");
        return false;
    }

    if (def->is_variadic) {
        if (args.size() < def->params.size()) {
            diag_.error(ident.loc, "macro '" + name + "' expects at least " +
                        std::to_string(def->params.size()) + " arguments, got " +
                        std::to_string(args.size()));
            return true;
        }
    } else {
        if (args.size() != def->params.size()) {
            diag_.error(ident.loc, "macro '" + name + "' expects " +
                        std::to_string(def->params.size()) + " arguments, got " +
                        std::to_string(args.size()));
            return true;
        }
    }

    std::vector<Token> va_tokens;
    bool va_empty = true;
    if (def->is_variadic) {
        for (size_t i = def->params.size(); i < args.size(); i++) {
            if (i > def->params.size()) {
                Token comma = ident;
                comma.kind = TokenKind::Comma;
                comma.text = synthText(",");
                va_tokens.push_back(comma);
            }
            for (auto& t : args[i]) va_tokens.push_back(t);
        }
        va_empty = va_tokens.empty();
    }

    auto getParamArg = [&](const Token& tok, std::vector<Token>* out) -> bool {
        if (tok.kind != TokenKind::Identifier) return false;
        for (size_t i = 0; i < def->params.size(); i++) {
            if (tok.text == def->params[i]) {
                if (out) *out = args[i];
                return true;
            }
        }
        if (def->is_variadic && tok.text == "__VA_ARGS__") {
            if (out) *out = va_tokens;
            return true;
        }
        return false;
    };

    expanding_.insert(name);

    const auto& body = def->body;
    size_t bi = 0;

    while (bi < body.size()) {
        const Token& bodyTok = body[bi];

        if (bodyTok.kind == TokenKind::PP_Hash) { // # param stringification
            bi++;

            std::string_view hashText = bodyTok.text;
            std::string paramName;
            if (hashText.size() > 1) {
                size_t start = 1;
                while (start < hashText.size() && hashText[start] == ' ') start++;
                paramName = std::string(hashText.substr(start));
            }

            std::vector<Token> argToks;
            bool isParam = false;

            if (!paramName.empty()) {
                for (size_t i = 0; i < def->params.size(); i++) {
                    if (paramName == def->params[i]) {
                        argToks = args[i];
                        isParam = true;
                        break;
                    }
                }
                if (!isParam && def->is_variadic && paramName == "__VA_ARGS__") {
                    argToks = va_tokens;
                    isParam = true;
                }
            } else {
                if (bi >= body.size()) {
                    diag_.error(bodyTok.loc, "'#' not followed by macro parameter");
                    break;
                }
                const Token& paramTok = body[bi];
                bi++;
                isParam = getParamArg(paramTok, &argToks);
                if (!isParam) {
                    diag_.error(paramTok.loc, "'#' not followed by macro parameter");
                    output.push_back(paramTok);
                    continue;
                }
            }

            if (!isParam) {
                diag_.error(bodyTok.loc, "'#' not followed by macro parameter");
                continue;
            }

            std::string argText = tokensToString(argToks);
            static constexpr size_t kMaxStringify = 65536;
            if (argText.size() > kMaxStringify) {
                diag_.warning(bodyTok.loc, "stringified macro argument truncated");
                argText.resize(kMaxStringify);
            }
            std::string quoted = "\"" + escapeForString(argText) + "\"";

            Token strTok = bodyTok;
            strTok.kind = TokenKind::StringLiteral;
            strTok.text = synthText(quoted);
            output.push_back(strTok);
            continue;
        }

        if (bodyTok.kind == TokenKind::PP_HashHash) { // ## body के start पर: skip करो
            bi++;
            continue;
        }

        if (bi + 1 < body.size() && body[bi + 1].kind == TokenKind::PP_HashHash) { // lhs ## rhs
            std::string lhsText;
            std::vector<Token> lhsArgToks;
            if (getParamArg(bodyTok, &lhsArgToks)) {
                lhsText = tokensToString(lhsArgToks);
            } else {
                lhsText = tokenText(bodyTok);
            }

            bi += 2; // lhs और ## skip करो

            // GNU extension: ,##__VA_ARGS__ जब __VA_ARGS__ empty हो तो comma elide करो।
            if (bi < body.size() &&
                body[bi].kind == TokenKind::Identifier &&
                body[bi].text == "__VA_ARGS__" &&
                def->is_variadic && va_empty) {
                while (!lhsText.empty() && (lhsText.back() == ',' || lhsText.back() == ' '))
                    lhsText.pop_back();
                bi++;
                if (!lhsText.empty()) {
                    int fid = sm_.loadString("<pp-paste>", lhsText);
                    Lexer relexer(sm_, fid, diag_);
                    while (true) {
                        Token t = relexer.next();
                        if (t.kind == TokenKind::Eof) break;
                        output.push_back(t);
                    }
                }
                continue;
            }

            std::string rhsText;
            if (bi < body.size()) {
                std::vector<Token> rhsArgToks;
                if (getParamArg(body[bi], &rhsArgToks)) {
                    rhsText = tokensToString(rhsArgToks);
                } else {
                    rhsText = tokenText(body[bi]);
                }
                bi++;
            }

            std::string pasted = lhsText + rhsText;
            static constexpr size_t kMaxPasteLen = 4096;
            if (pasted.size() > kMaxPasteLen) {
                diag_.error(bodyTok.loc, "token paste result too long");
                bi++;
                continue;
            }
            if (!pasted.empty()) {
                int fid = sm_.loadString("<pp-paste>", pasted);
                Lexer relexer(sm_, fid, diag_);
                std::vector<Token> relex;
                while (true) {
                    Token t = relexer.next();
                    if (t.kind == TokenKind::Eof) break;
                    relex.push_back(t);
                }
                if (relex.empty() || relex[0].kind == TokenKind::Error) {
                    diag_.error(bodyTok.loc, "## produced invalid token '" + pasted + "'");
                    bi++;
                    continue;
                }
                for (auto& t : relex) output.push_back(t);
            }
            continue;
        }

        if (bodyTok.kind == TokenKind::Identifier) {
            std::vector<Token> argToks;
            if (getParamArg(bodyTok, &argToks)) {
                for (auto& argTok : argToks) {
                    if (argTok.kind == TokenKind::Identifier) {
                        std::deque<Token> expanded;
                        if (tryExpandMacro(argTok, expanded)) {
                            for (auto& e : expanded) output.push_back(e);
                        } else {
                            output.push_back(argTok);
                        }
                    } else {
                        output.push_back(argTok);
                    }
                }
                bi++;
                continue;
            }

            std::deque<Token> expanded;
            if (tryExpandMacro(bodyTok, expanded)) {
                for (auto& e : expanded) output.push_back(e);
                bi++;
                continue;
            }
        }

        if (bodyTok.kind != TokenKind::PP_Hash && bodyTok.kind != TokenKind::PP_HashHash) {
            output.push_back(bodyTok);
        }
        bi++;
    }

    expanding_.erase(name);
    return true;
}

bool Preprocessor::produce(Token& out) {
    Token tok = rawNext();

    switch (tok.kind) {
    case TokenKind::PP_Define:
        if (isActive()) handleDefine();
        else collectLineTokens();
        return false;

    case TokenKind::PP_Undef:
        if (isActive()) handleUndef();
        else { rawNext(); }
        return false;

    case TokenKind::PP_Include:
        if (isActive()) handleInclude(tok);
        else collectLineTokens();
        return false;

    case TokenKind::PP_Ifdef:
        if (isActive()) {
            handleIfdef(true);
        } else {
            condPush(CondState::SkipToEnd);
            rawNext();
            skipConditionalBlock();
        }
        return false;

    case TokenKind::PP_Ifndef:
        if (isActive()) {
            handleIfdef(false);
        } else {
            condPush(CondState::SkipToEnd);
            rawNext();
            skipConditionalBlock();
        }
        return false;

    case TokenKind::PP_If:
        if (isActive()) {
            handleIf(tok);
        } else {
            condPush(CondState::SkipToEnd);
            collectLineTokens();
            skipConditionalBlock();
        }
        return false;

    case TokenKind::PP_Elif:
        if (isActive()) {
            handleElif();
        } else {
            collectLineTokens();
        }
        return false;

    case TokenKind::PP_Else:
        if (isActive()) {
            handleElse();
        }
        return false;

    case TokenKind::PP_Endif:
        handleEndif();
        return false;

    case TokenKind::PP_Ifaot:
        if (isActive()) {
            handleIfaot();
        } else {
            condPush(CondState::SkipToEnd);
            skipConditionalBlock();
        }
        return false;

    case TokenKind::PP_Ifjit:
        if (isActive()) {
            handleIfjit();
        } else {
            condPush(CondState::SkipToEnd);
            skipConditionalBlock();
        }
        return false;

    case TokenKind::PP_Assert:
        if (isActive()) handleAssert(tok);
        else collectLineTokens();
        return false;

    case TokenKind::PP_Exe:
        if (isActive()) handleExe();
        return false;

    case TokenKind::PP_Hash:
    case TokenKind::PP_HashHash:
        return false;

    default:
        break;
    }

    if (!isActive()) return false;

    if (tok.kind == TokenKind::Identifier) {
        std::deque<Token> expanded;
        if (tryExpandMacro(tok, expanded)) {
            for (auto it = expanded.rbegin(); it != expanded.rend(); ++it) {
                buffer_.push_front(*it);
            }
            return false;
        }
    }

    out = tok;
    return true;
}

void Preprocessor::fillBuffer() {
    while (buffer_.empty()) {
        Token out;
        if (produce(out)) {
            buffer_.push_back(out);
            return;
        }
        if (include_stack_.size() == 1) {
            Token p = include_stack_.back().lexer->peek();
            if (p.kind == TokenKind::Eof) {
                buffer_.push_back(p);
                return;
            }
        }
    }
}

Token Preprocessor::next() {
    if (!buffer_.empty()) {
        Token tok = buffer_.front();
        buffer_.pop_front();
        return tok;
    }
    fillBuffer();
    if (!buffer_.empty()) {
        Token tok = buffer_.front();
        buffer_.pop_front();
        return tok;
    }
    return Token(TokenKind::Eof, {}, {});
}

Token Preprocessor::peek() {
    if (buffer_.empty()) {
        fillBuffer();
    }
    if (!buffer_.empty()) {
        return buffer_.front();
    }
    return Token(TokenKind::Eof, {}, {});
}

void Preprocessor::handleExe() {
    Token lbrace = rawNext();
    if (lbrace.kind != TokenKind::LBrace) {
        diag_.error(lbrace.loc, "expected '{' after #exe");
        return;
    }

    std::string code;
    int depth = 1;
    while (depth > 0) {
        Token t = rawNext();
        if (t.kind == TokenKind::Eof) {
            diag_.error(t.loc, "unterminated #exe block");
            return;
        }
        if (t.kind == TokenKind::LBrace) {
            depth++;
            code += "{ ";
        } else if (t.kind == TokenKind::RBrace) {
            depth--;
            if (depth > 0) code += "} ";
        } else {
            code += std::string(t.text) + " ";
        }
    }

    std::string wrappedCode = "{ " + code + " }";

    int fid = sm_.loadString("<pp-exe>", wrappedCode);
    Preprocessor exePP(sm_, diag_, fid);
    Arena arena;
    Parser parser(exePP, diag_, arena);
    TranslationUnit* tu = parser.parse();

    CompoundStmt* body = nullptr;
    if (!tu->decls.empty()) {
        if (auto* cs = dynamic_cast<CompoundStmt*>(tu->decls[0])) {
            body = cs;
        }
    }

    if (!body) return;

    Interpreter interp(diag_);
    std::string emitted = interp.runExeBlock(body);

    if (emitted.empty()) return;

    int emitFid = sm_.loadString("<exe-emit>", emitted);
    Lexer lexer(sm_, emitFid, diag_);
    std::deque<Token> tokens;
    while (true) {
        Token tok = lexer.next();
        if (tok.kind == TokenKind::Eof) break;
        tokens.push_back(tok);
    }

    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        buffer_.push_front(*it);
    }
}

void Preprocessor::injectTokens(const std::deque<Token>& tokens) {
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        buffer_.push_front(*it);
    }
}

} // namespace holyc

#include "LSPServer.h"
#include "../lexer/Lexer.h"
#include "../lexer/Token.h"
#include "../preprocessor/Preprocessor.h"
#include "../parser/Parser.h"
#include "../sema/Sema.h"
#include "../driver/Formatter.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace holyc {
namespace lsp {

// ---------------------------------------------------------------------------
// JSON-RPC error codes (LSP spec के अनुसार)
// ---------------------------------------------------------------------------
static const int kParseError        = -32700;
static const int kInvalidRequest    = -32600;
static const int kMethodNotFound    = -32601;
static const int kServerNotInit     = -32002;

// ---------------------------------------------------------------------------
// Constructor (निर्माणकर्ता)
// ---------------------------------------------------------------------------
LSPServer::LSPServer() {}

// ---------------------------------------------------------------------------
// Main loop (मुख्य लूप)
// ---------------------------------------------------------------------------
/**
 * @brief JSON-RPC message loop चलाता है; 150 ms में debounced parses भी flush करता है।
 */
void LSPServer::run() {
    while (!shutdown_requested_) {
        // देखो कि pending parse flush करना है या नहीं (debounce: 150 ms)
        if (pending_parse_) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_change_time_).count();
            if (elapsed >= 150) {
                auto* doc = getDocument(pending_uri_);
                if (doc) {
                    parseDocument(*doc);
                    publishDiagnostics(*doc);
                }
                pending_parse_ = false;
            }
        }
        try {
            json msg = readMessage();
            if (msg.is_null()) break;
            handleMessage(msg);
        } catch (const std::exception& e) {
            // stderr पर log करो (stdout नहीं, वो LSP channel है)
            std::fprintf(stderr, "[hcc-lsp] exception: %s\n", e.what());
        }
    }
}

// ---------------------------------------------------------------------------
// I/O helpers — LSP framing: Content-Length: N\r\n\r\n{json} — Input/Output operations (संदेश पढ़ना/लिखना)
// ---------------------------------------------------------------------------
/**
 * @brief stdin से Content-Length-framed JSON-RPC message पढ़ता है।
 *
 * @return Parsed JSON object, या error या EOF पर null।
 */
json LSPServer::readMessage() {
    std::string header;
    int contentLength = -1;
    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) return nullptr;
        // Trailing \r हटाओ
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;  // headers खत्म
        if (line.rfind("Content-Length: ", 0) == 0) {
            try {
                long long val = std::stoll(line.substr(16));
                constexpr long long MAX_MSG = 10LL * 1024 * 1024;  // 10 MB — maximum message size limit (अधिकतम संदेश आकार)
                if (val > 0 && val <= MAX_MSG)
                    contentLength = static_cast<int>(val);
            } catch (...) {
                // malformed Content-Length — ignore करो, <= 0 check पर fail होगा
            }
        }
    }
    if (contentLength <= 0) return nullptr;
    std::string body;
    try {
        body.resize(static_cast<size_t>(contentLength), '\0');
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    if (!std::cin.read(&body[0], contentLength)) return nullptr;
    if (body.size() > 10'000'000ULL) return nullptr;
    auto parsed = json::parse(body, nullptr, false);
    if (parsed.is_discarded()) return nullptr;
    return parsed;
}

/**
 * @brief msg को JSON serialize करके LSP Content-Length framing के साथ stdout पर लिखता है।
 *
 * @param msg Send करने वाला JSON object।
 */
void LSPServer::sendMessage(const json& msg) {
    std::string body = msg.dump(-1, ' ', false, json::error_handler_t::replace);
    std::string out = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    // stdout पर atomically लिखना जरूरी है
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
}

void LSPServer::sendResponse(const json& id, const json& result) {
    sendMessage({{"jsonrpc","2.0"}, {"id", id}, {"result", result}});
}

void LSPServer::sendError(const json& id, int code, const std::string& message) {
    sendMessage({{"jsonrpc","2.0"}, {"id", id},
                 {"error", {{"code", code}, {"message", message}}}});
}

void LSPServer::sendNotification(const std::string& method, const json& params) {
    sendMessage({{"jsonrpc","2.0"}, {"method", method}, {"params", params}});
}

// ---------------------------------------------------------------------------
// Dispatch (संदेश routing)
// ---------------------------------------------------------------------------
/**
 * @brief Method field parse करके appropriate lifecycle या feature handler पर route करता है।
 *
 * @param msg Parsed JSON-RPC message object।
 */
void LSPServer::handleMessage(const json& msg) {
    if (!msg.is_object()) return;

    std::string method;
    if (msg.contains("method") && msg["method"].is_string())
        method = msg["method"].get<std::string>();

    json id = nullptr;
    if (msg.contains("id")) id = msg["id"];

    bool isRequest = !id.is_null();
    json params = msg.value("params", json(nullptr));

    // Initialization से पहले, सिर्फ initialize allow करो
    if (!initialized_ && method != "initialize") {
        if (isRequest)
            sendError(id, kServerNotInit, "Server not initialized");
        return;
    }

    try {
        if (method == "initialize") {
            sendResponse(id, handleInitialize(params));
        } else if (method == "initialized") {
            handleInitialized(params);
        } else if (method == "shutdown") {
            shutdown_requested_ = true;
            sendResponse(id, nullptr);
        } else if (method == "exit") {
            shutdown_requested_ = true;
        } else if (method == "textDocument/didOpen") {
            handleDidOpen(params);
        } else if (method == "textDocument/didChange") {
            handleDidChange(params);
        } else if (method == "textDocument/didClose") {
            handleDidClose(params);
        } else if (method == "textDocument/hover") {
            sendResponse(id, handleHover(params));
        } else if (method == "textDocument/definition") {
            sendResponse(id, handleDefinition(params));
        } else if (method == "textDocument/completion") {
            sendResponse(id, handleCompletion(params));
        } else if (method == "textDocument/signatureHelp") {
            sendResponse(id, handleSignatureHelp(params));
        } else if (method == "textDocument/formatting") {
            sendResponse(id, handleFormatting(params));
        } else if (method == "textDocument/documentSymbol") {
            sendResponse(id, handleDocumentSymbol(params));
        } else if (method == "textDocument/semanticTokens/full") {
            sendResponse(id, handleSemanticTokensFull(params));
        } else {
            if (isRequest)
                sendError(id, kMethodNotFound, "Method not found: " + method);
        }
    } catch (const std::exception& e) {
        if (isRequest)
            sendError(id, -32603, std::string("Internal error: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Lifecycle (server जीवनचक्र)
// ---------------------------------------------------------------------------
/**
 * @brief LSP initialize request का server की capability set के साथ जवाब देता है।
 *
 * @return JSON capabilities और serverInfo object।
 */
json LSPServer::handleInitialize(const json& /*params*/) {
    initialized_ = true;
    return {
        {"capabilities", {
            {"textDocumentSync", {
                {"openClose", true},
                {"change", 1}  // Full sync (पूरा document भेजते हैं)
            }},
            {"hoverProvider", true},
            {"definitionProvider", true},
            {"completionProvider", {
                {"triggerCharacters", json::array({"."})}
            }},
            {"signatureHelpProvider", {
                {"triggerCharacters", json::array({"(", ","})}
            }},
            {"documentFormattingProvider", true},
            {"documentSymbolProvider", true},
            {"semanticTokensProvider", {
                {"legend", {
                    {"tokenTypes", json::array({
                        "keyword", "type", "function", "variable",
                        "string", "number", "comment", "macro", "operator"
                    })},
                    {"tokenModifiers", json::array({"declaration", "definition"})}
                }},
                {"full", true}
            }},
            {"diagnosticProvider", {
                {"identifier", "hcc"},
                {"interFileDependencies", false},
                {"workspaceDiagnostics", false}
            }}
        }},
        {"serverInfo", {
            {"name", "hcc"},
            {"version", "0.1"}
        }}
    };
}

void LSPServer::handleInitialized(const json& /*params*/) {
    // अभी के लिए कुछ नहीं करना
}

// ---------------------------------------------------------------------------
// Document sync (दस्तावेज़ synchronization)
// ---------------------------------------------------------------------------
/**
 * @brief doc के लिए parse pipeline (SourceManager, Diagnostics, Parser, Sema) rebuild करता है।
 *
 * @param doc In-place re-parse होने वाला Document; failure पर tu को nullptr set किया जाता है।
 */
void LSPServer::parseDocument(ParsedDocument& doc) {
    doc.sm    = std::make_unique<SourceManager>();
    doc.diag  = std::make_unique<Diagnostics>(doc.sm.get());
    doc.arena = std::make_unique<Arena>();
    doc.tu    = nullptr;

    doc.diag->setSuppressWarnings(false);
    doc.diag->setStoreEntries(true);

    int fid = doc.sm->loadString(doc.uri, doc.text);
    Preprocessor pp(*doc.sm, *doc.diag, fid);
    Parser parser(pp, *doc.diag, *doc.arena);
    doc.tu = parser.parse();

    if (doc.tu && !doc.diag->hasErrors()) {
        Sema sema(*doc.diag, *doc.arena);
        sema.analyze(doc.tu);
    }
}

ParsedDocument* LSPServer::getDocument(const std::string& uri) {
    auto it = docs_.find(uri);
    if (it == docs_.end()) return nullptr;
    return &it->second;
}

/**
 * @brief doc के सभी errors और warnings के लिए textDocument/publishDiagnostics भेजता है।
 *
 * @param doc जिस Document के stored diagnostics publish होने चाहिए।
 */
void LSPServer::publishDiagnostics(const ParsedDocument& doc) {
    json diags = json::array();

    if (doc.diag) {
        for (const auto& e : doc.diag->entries()) {
            int severity = (e.level == DiagLevel::Error) ? 1 : 2;
            json d = {
                {"range", {
                    {"start", {{"line", e.loc.line > 0 ? e.loc.line - 1 : 0},
                                {"character", e.loc.col > 0 ? e.loc.col - 1 : 0}}},
                    {"end",   {{"line", e.loc.line > 0 ? e.loc.line - 1 : 0},
                                {"character", e.loc.col > 0 ? (int)e.loc.col + (int)e.message.size() : 0}}}
                }},
                {"severity", severity},
                {"source", "hcc"},
                {"message", e.message}
            };
            diags.push_back(d);
        }
    }

    sendNotification("textDocument/publishDiagnostics", {
        {"uri", doc.uri},
        {"diagnostics", diags}
    });
}

void LSPServer::handleDidOpen(const json& params) {
    auto& tdoc = params["textDocument"];
    std::string uri  = tdoc["uri"].get<std::string>();
    std::string text = tdoc["text"].get<std::string>();
    int version      = tdoc.value("version", 0);

    auto& doc = docs_[uri];
    doc.uri     = uri;
    doc.text    = text;
    doc.version = version;
    parseDocument(doc);
    publishDiagnostics(doc);
}

void LSPServer::handleDidChange(const json& params) {
    auto& tdoc = params["textDocument"];
    std::string uri = tdoc["uri"].get<std::string>();
    int version     = tdoc.value("version", 0);

    auto* doc = getDocument(uri);
    if (!doc) {
        // नई entry create करो
        docs_[uri].uri = uri;
        doc = &docs_[uri];
    }

    // Full sync: आखिरी content change लो
    auto& changes = params["contentChanges"];
    if (!changes.empty()) {
        doc->text    = changes.back()["text"].get<std::string>();
        doc->version = version;
    }

    // Debounce: time record करो और parsing 150 ms की inactivity तक defer करो
    last_change_time_ = std::chrono::steady_clock::now();
    pending_uri_  = uri;
    pending_text_ = doc->text;
    pending_parse_ = true;
}

void LSPServer::handleDidClose(const json& params) {
    std::string uri = params["textDocument"]["uri"].get<std::string>();
    docs_.erase(uri);
    // बंद document के diagnostics clear करो
    sendNotification("textDocument/publishDiagnostics", {
        {"uri", uri},
        {"diagnostics", json::array()}
    });
}

// ---------------------------------------------------------------------------
// AST helpers (AST सहायक फ़ंक्शन)
// ---------------------------------------------------------------------------

/**
 * @brief TU से सभी declarations एक flat list में collect करता है (definition/completion के लिए)।
 *
 * @param doc जिस Document का TU walk होगा।
 * @return TU में मिले सभी Decl pointers का flat vector।
 */
std::vector<holyc::Decl*> LSPServer::collectDecls(const ParsedDocument& doc) {
    std::vector<holyc::Decl*> result;
    if (!doc.tu) return result;

    std::function<void(holyc::Stmt*)> walkStmt = [&](holyc::Stmt* s) {
        if (!s) return;
        if (auto* ds = dynamic_cast<holyc::DeclStmt*>(s)) {
            if (ds->decl) result.push_back(ds->decl);
        } else if (auto* cs = dynamic_cast<holyc::CompoundStmt*>(s)) {
            for (auto* st : cs->stmts) walkStmt(st);
        } else if (auto* ifs = dynamic_cast<holyc::IfStmt*>(s)) {
            walkStmt(ifs->then_body);
            walkStmt(ifs->else_body);
        } else if (auto* ws = dynamic_cast<holyc::WhileStmt*>(s)) {
            walkStmt(ws->body);
        } else if (auto* fs = dynamic_cast<holyc::ForStmt*>(s)) {
            walkStmt(fs->body);
        }
    };

    for (auto* n : doc.tu->decls) {
        if (auto* d = dynamic_cast<holyc::Decl*>(n)) {
            result.push_back(d);
            if (auto* fd = dynamic_cast<holyc::FuncDecl*>(d)) {
                if (fd->body) walkStmt(fd->body);
            }
        }
    }
    return result;
}

/**
 * @brief दिए गए 0-based line/col पर innermost node (Expr या Decl) ढूंढता है।
 *
 * यह best-effort walk है; पहला node लौटाता है जिसका source location match करे।
 *
 * @param doc  Search करने वाला Document।
 * @param line 0-based line number।
 * @param col  0-based column number।
 * @return Best-matching node, या न मिले तो nullptr।
 */
holyc::Node* LSPServer::findNodeAt(const ParsedDocument& doc, int line, int col) {
    if (!doc.tu) return nullptr;
    // 1-based में convert करो
    unsigned wantLine = (unsigned)(line + 1);
    unsigned wantCol  = (unsigned)(col + 1);

    holyc::Node* best = nullptr;

    std::function<void(holyc::Node*)> visit = [&](holyc::Node* n) {
        if (!n) return;
        holyc::SourceLocation loc = {};
        if (auto* e = dynamic_cast<holyc::Expr*>(n))       loc = e->loc;
        else if (auto* d = dynamic_cast<holyc::Decl*>(n))  loc = d->loc;
        else if (auto* s = dynamic_cast<holyc::Stmt*>(n))  loc = s->loc;

        if (loc.line == wantLine && loc.col <= wantCol)
            best = n;

        // Children में recurse करो
        if (auto* fd = dynamic_cast<holyc::FuncDecl*>(n)) {
            if (fd->body) visit(fd->body);
        } else if (auto* cs = dynamic_cast<holyc::CompoundStmt*>(n)) {
            for (auto* s : cs->stmts) visit(s);
        } else if (auto* es = dynamic_cast<holyc::ExprStmt*>(n)) {
            visit(es->expr);
        } else if (auto* ds = dynamic_cast<holyc::DeclStmt*>(n)) {
            if (ds->decl) visit(ds->decl);
        } else if (auto* vd = dynamic_cast<holyc::VarDecl*>(n)) {
            if (vd->init) visit(vd->init);
        } else if (auto* ce = dynamic_cast<holyc::CallExpr*>(n)) {
            visit(ce->callee);
            for (auto* a : ce->args) visit(a);
        } else if (auto* be = dynamic_cast<holyc::BinaryExpr*>(n)) {
            visit(be->lhs); visit(be->rhs);
        } else if (auto* ue = dynamic_cast<holyc::UnaryExpr*>(n)) {
            visit(ue->operand);
        } else if (auto* ie = dynamic_cast<holyc::IfStmt*>(n)) {
            visit(ie->cond); visit(ie->then_body); visit(ie->else_body);
        } else if (auto* we = dynamic_cast<holyc::WhileStmt*>(n)) {
            visit(we->cond); visit(we->body);
        } else if (auto* fe = dynamic_cast<holyc::ForStmt*>(n)) {
            visit(fe->init); visit(fe->cond); visit(fe->post); visit(fe->body);
        } else if (auto* re = dynamic_cast<holyc::ReturnStmt*>(n)) {
            if (re->value) visit(re->value);
        }
    };

    for (auto* n : doc.tu->decls) visit(n);
    return best;
}

// ---------------------------------------------------------------------------
// Hover (cursor पर type info)
// ---------------------------------------------------------------------------
json LSPServer::handleHover(const json& params) {
    std::string uri = params["textDocument"]["uri"].get<std::string>();
    int line = params["position"]["line"].get<int>();
    int col  = params["position"]["character"].get<int>();

    auto* doc = getDocument(uri);
    if (!doc || !doc->tu) return nullptr;

    holyc::Node* node = findNodeAt(*doc, line, col);
    if (!node) return nullptr;

    std::string typeStr;
    // typedef lookup से पहले raw type string
    std::string rawTypeStr;
    if (auto* e = dynamic_cast<holyc::Expr*>(node)) {
        if (e->resolved_type) rawTypeStr = e->resolved_type->toString();
        typeStr = rawTypeStr;
    } else if (auto* fd = dynamic_cast<holyc::FuncDecl*>(node)) {
        rawTypeStr = fd->return_type ? fd->return_type->toString() : "U0";
        typeStr = fd->name + ": " + rawTypeStr;
    } else if (auto* vd = dynamic_cast<holyc::VarDecl*>(node)) {
        rawTypeStr = vd->type ? vd->type->toString() : "?";
        typeStr = vd->name + ": " + rawTypeStr;
    }

    if (typeStr.empty()) return nullptr;

    // देखो कि raw type string किसी typedef alias से match करती है
    if (!rawTypeStr.empty() && doc->tu) {
        for (auto* n : doc->tu->decls) {
            if (auto* td = dynamic_cast<holyc::TypedefDecl*>(n)) {
                if (td->type && td->type->toString() == rawTypeStr) {
                    typeStr += " (alias: " + td->name + ")";
                    break;
                }
            }
        }
    }

    return {
        {"contents", {
            {"kind", "markdown"},
            {"value", "```holyc\n" + typeStr + "\n```"}
        }}
    };
}

// ---------------------------------------------------------------------------
// Definition (definition पर जाएं)
// ---------------------------------------------------------------------------
json LSPServer::handleDefinition(const json& params) {
    std::string uri = params["textDocument"]["uri"].get<std::string>();
    int line = params["position"]["line"].get<int>();
    int col  = params["position"]["character"].get<int>();

    auto* doc = getDocument(uri);
    if (!doc || !doc->tu) return nullptr;

    holyc::Node* node = findNodeAt(*doc, line, col);
    if (!node) return nullptr;

    // Cursor पर identifier ढूंढने की कोशिश करो
    std::string name;
    if (auto* id = dynamic_cast<holyc::IdentifierExpr*>(node)) name = id->name;
    else if (auto* ce = dynamic_cast<holyc::CallExpr*>(node)) {
        if (auto* id2 = dynamic_cast<holyc::IdentifierExpr*>(ce->callee)) name = id2->name;
    }
    if (name.empty()) return nullptr;

    // Matching name के लिए सभी decls search करो
    auto decls = collectDecls(*doc);
    for (auto* d : decls) {
        std::string dname;
        if (auto* fd = dynamic_cast<holyc::FuncDecl*>(d))   dname = fd->name;
        else if (auto* vd = dynamic_cast<holyc::VarDecl*>(d)) dname = vd->name;
        if (dname == name && d->loc.line > 0) {
            int defLine = (int)d->loc.line - 1;
            int defCol  = (int)d->loc.col - 1;
            return {
                {"uri", uri},
                {"range", {
                    {"start", {{"line", defLine}, {"character", defCol}}},
                    {"end",   {{"line", defLine}, {"character", defCol + (int)dname.size()}}}
                }}
            };
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Class declaration lookup helper (class declaration खोजने का helper)
// ---------------------------------------------------------------------------
holyc::ClassDecl* LSPServer::findClassDecl(const ParsedDocument& doc, const std::string& name) {
    if (!doc.tu || name.empty()) return nullptr;
    for (auto* n : doc.tu->decls) {
        if (auto* cd = dynamic_cast<holyc::ClassDecl*>(n)) {
            if (cd->name == name) return cd;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Completion (auto-complete) — code completion suggestions (स्वतः पूर्णता)
// ---------------------------------------------------------------------------
json LSPServer::handleCompletion(const json& params) {
    std::string uri = params["textDocument"]["uri"].get<std::string>();

    auto* doc = getDocument(uri);
    json items = json::array();

    // Keywords (आरक्षित शब्द)
    static const char* kKeywords[] = {
        "U0", "U8", "U16", "U32", "U64", "I8", "I16", "I32", "I64", "F64", "F32",
        "if", "else", "while", "for", "do", "return", "break", "continue",
        "class", "extern", "public", "asm", "goto", "try", "catch", "throw",
        "sizeof", "static", nullptr
    };
    for (const char** kw = kKeywords; *kw; ++kw) {
        items.push_back({{"label", *kw}, {"kind", 14}});  // kind 14 = keyword (LSP spec के अनुसार)
    }
    // Builtin functions (built-in फ़ंक्शन)
    static const char* kBuiltins[] = {
        "Print", "printf", "sprintf", "Malloc", "MAlloc", "Free", "Strlen",
        "Strcmp", "Strcpy", "Strcat", "MemSet", "MemCpy", "Abs", "Sqrt",
        "Sin", "Cos", "Tan", "Pow", "Log", "Rand", "Now", "GetTickCount",
        "Bts", "Btr", "Btc", nullptr
    };
    for (const char** b = kBuiltins; *b; ++b) {
        items.push_back({{"label", *b}, {"kind", 3}});  // kind 3 = function (फ़ंक्शन)
    }

    // Current document से declarations
    if (doc && doc->tu) {
        auto decls = collectDecls(*doc);
        for (auto* d : decls) {
            std::string name;
            int kind = 6;  // variable (चर)
            if (auto* fd = dynamic_cast<holyc::FuncDecl*>(d)) {
                name = fd->name; kind = 3;  // function (फ़ंक्शन)
            } else if (auto* vd = dynamic_cast<holyc::VarDecl*>(d)) {
                name = vd->name; kind = 6;
            }
            if (!name.empty())
                items.push_back({{"label", name}, {"kind", kind}});
        }

        // Class members, inherited ones सहित
        for (auto* n : doc->tu->decls) {
            if (auto* classDecl = dynamic_cast<holyc::ClassDecl*>(n)) {
                // इस class के direct members
                for (auto* member : classDecl->members) {
                    std::string mname;
                    int mkind = 8;  // kind 8 = field (सदस्य चर)
                    if (auto* fd = dynamic_cast<holyc::FuncDecl*>(member)) {
                        mname = fd->name; mkind = 3;
                    } else if (auto* fld = dynamic_cast<holyc::FieldDecl*>(member)) {
                        mname = fld->name; mkind = 8;
                    } else if (auto* vd = dynamic_cast<holyc::VarDecl*>(member)) {
                        mname = vd->name; mkind = 6;
                    }
                    if (!mname.empty())
                        items.push_back({{"label", mname}, {"kind", mkind},
                                         {"detail", classDecl->name + "." + mname}});
                }

                // Inherited members: base_name chain पर चढ़ो
                std::string base = classDecl->base_name;
                while (!base.empty()) {
                    holyc::ClassDecl* baseDecl = findClassDecl(*doc, base);
                    if (!baseDecl) break;
                    for (auto* member : baseDecl->members) {
                        std::string mname;
                        int mkind = 8;
                        if (auto* fd = dynamic_cast<holyc::FuncDecl*>(member)) {
                            mname = fd->name; mkind = 3;
                        } else if (auto* fld = dynamic_cast<holyc::FieldDecl*>(member)) {
                            mname = fld->name; mkind = 8;
                        } else if (auto* vd = dynamic_cast<holyc::VarDecl*>(member)) {
                            mname = vd->name; mkind = 6;
                        }
                        if (!mname.empty())
                            items.push_back({{"label", mname}, {"kind", mkind},
                                             {"detail", "(inherited from " + base + ")"}});
                    }
                    base = baseDecl->base_name;
                }
            }
        }
    }

    return {{"isIncomplete", false}, {"items", items}};
}

// ---------------------------------------------------------------------------
// Signature help (फ़ंक्शन signature सहायता)
// ---------------------------------------------------------------------------
json LSPServer::handleSignatureHelp(const json& params) {
    std::string uri = params["textDocument"]["uri"].get<std::string>();

    auto* doc = getDocument(uri);
    if (!doc || !doc->tu) return nullptr;

    // सभी functions ढूंढो और उनके signatures लौटाओ
    json sigs = json::array();
    if (doc->tu) {
        for (auto* n : doc->tu->decls) {
            if (auto* fd = dynamic_cast<holyc::FuncDecl*>(n)) {
                std::string label = fd->name + "(";
                json params_json = json::array();
                bool first = true;
                for (auto* p : fd->params) {
                    if (!first) label += ", ";
                    first = false;
                    std::string pt = p->type ? p->type->toString() : "?";
                    label += pt + " " + p->name;
                    params_json.push_back(json{{"label", pt + " " + p->name}});
                }
                if (fd->is_vararg) {
                    if (!first) label += ", ";
                    label += "..";
                }
                label += ")";
                sigs.push_back(json{
                    {"label", label},
                    {"parameters", params_json}
                });
            }
        }
    }

    if (sigs.empty()) return nullptr;
    return {{"signatures", sigs}, {"activeSignature", 0}, {"activeParameter", 0}};
}

// ---------------------------------------------------------------------------
// Formatting (कोड formatting)
// ---------------------------------------------------------------------------
json LSPServer::handleFormatting(const json& params) {
    std::string uri = params["textDocument"]["uri"].get<std::string>();

    auto* doc = getDocument(uri);
    if (!doc) return nullptr;

    // Document lex करो और formatter चलाओ
    SourceManager sm;
    Diagnostics diag(&sm);
    int fid = sm.loadString(doc->uri, doc->text);
    Lexer lexer(sm, fid, diag);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.next();
        tokens.push_back(tok);
        if (tok.kind == TokenKind::Eof) break;
    }

    Formatter fmt(tokens);
    std::string formatted = fmt.format();

    // Original document में lines गिनो
    int numLines = 0;
    for (char c : doc->text) if (c == '\n') ++numLines;

    // एक single edit लौटाओ जो पूरा document replace करे
    return json::array({
        {
            {"range", {
                {"start", {{"line", 0}, {"character", 0}}},
                {"end",   {{"line", numLines + 1}, {"character", 0}}}
            }},
            {"newText", formatted}
        }
    });
}

// ---------------------------------------------------------------------------
// Document Symbols (दस्तावेज़ के symbols)
// ---------------------------------------------------------------------------
json LSPServer::handleDocumentSymbol(const json& params) {
    std::string uri = params["textDocument"]["uri"].get<std::string>();

    auto* doc = getDocument(uri);
    if (!doc || !doc->tu) return json::array();

    json symbols = json::array();

    // LSP SymbolKind constants — LSP spec में defined symbol type numbers
    // 5 = Class, 12 = Function, 13 = Variable, 8 = Field, 10 = Enum (LSP spec के अनुसार)
    static const int kClass    = 5;
    static const int kFunction = 12;
    static const int kVariable = 13;
    static const int kField    = 8;
    static const int kEnum     = 10;

    auto makeRange = [](const holyc::SourceLocation& loc) -> json {
        int line = loc.line > 0 ? (int)loc.line - 1 : 0;
        int col  = loc.col  > 0 ? (int)loc.col  - 1 : 0;
        return {
            {"start", {{"line", line}, {"character", col}}},
            {"end",   {{"line", line}, {"character", col + 1}}}
        };
    };

    for (auto* n : doc->tu->decls) {
        if (auto* fd = dynamic_cast<holyc::FuncDecl*>(n)) {
            if (fd->name.empty()) continue;
            json sym = {
                {"name", fd->name},
                {"kind", kFunction},
                {"location", {
                    {"uri", uri},
                    {"range", makeRange(fd->loc)}
                }}
            };
            // Parameter detail जोड़ो
            std::string detail;
            for (auto* p : fd->params) {
                if (!detail.empty()) detail += ", ";
                if (p->type) detail += p->type->toString() + " ";
                detail += p->name;
            }
            if (fd->is_vararg) {
                if (!detail.empty()) detail += ", ";
                detail += "..";
            }
            sym["detail"] = "(" + detail + ") -> " +
                            (fd->return_type ? fd->return_type->toString() : "U0");
            symbols.push_back(sym);
        } else if (auto* vd = dynamic_cast<holyc::VarDecl*>(n)) {
            if (vd->name.empty()) continue;
            symbols.push_back({
                {"name", vd->name},
                {"kind", kVariable},
                {"location", {
                    {"uri", uri},
                    {"range", makeRange(vd->loc)}
                }},
                {"detail", vd->type ? vd->type->toString() : "?"}
            });
        } else if (auto* cd = dynamic_cast<holyc::ClassDecl*>(n)) {
            if (cd->name.empty()) continue;
            json children = json::array();
            for (auto* m : cd->members) {
                if (auto* mfd = dynamic_cast<holyc::FuncDecl*>(m)) {
                    if (!mfd->name.empty())
                        children.push_back({
                            {"name", mfd->name},
                            {"kind", kFunction},
                            {"location", {
                                {"uri", uri},
                                {"range", makeRange(mfd->loc)}
                            }}
                        });
                } else if (auto* fld = dynamic_cast<holyc::FieldDecl*>(m)) {
                    if (!fld->name.empty())
                        children.push_back({
                            {"name", fld->name},
                            {"kind", kField},
                            {"location", {
                                {"uri", uri},
                                {"range", makeRange(fld->loc)}
                            }},
                            {"detail", fld->type ? fld->type->toString() : "?"}
                        });
                }
            }
            json sym = {
                {"name", cd->name},
                {"kind", kClass},
                {"location", {
                    {"uri", uri},
                    {"range", makeRange(cd->loc)}
                }},
                {"children", children}
            };
            symbols.push_back(sym);
        } else if (auto* ed = dynamic_cast<holyc::EnumDecl*>(n)) {
            if (ed->name.empty()) continue;
            symbols.push_back({
                {"name", ed->name},
                {"kind", kEnum},
                {"location", {
                    {"uri", uri},
                    {"range", makeRange(ed->loc)}
                }}
            });
        }
    }

    return symbols;
}

// Token type indices — handleInitialize() के legend से match होने चाहिए
static const int kTokKeyword  = 0;
static const int kTokType     = 1;
static const int kTokFunction = 2;
static const int kTokVariable = 3;
static const int kTokString   = 4;
static const int kTokNumber   = 5;
static const int kTokComment  = 6;
static const int kTokMacro    = 7;
static const int kTokOperator = 8;

json LSPServer::handleSemanticTokensFull(const json& params) {
    std::string uri;
    if (params.contains("textDocument") && params["textDocument"].contains("uri"))
        uri = params["textDocument"]["uri"].get<std::string>();

    auto* doc = getDocument(uri);
    if (!doc || doc->text.empty())
        return {{"data", json::array()}};

    // Source text lex करो ताकि precise locations वाले tokens मिलें।
    SourceManager sm;
    Diagnostics diag(&sm);
    int fid = sm.loadString(uri, doc->text);
    Lexer lexer(sm, fid, diag);
    std::vector<Token> tokens;
    while (true) {
        Token t = lexer.next();
        if (t.kind == TokenKind::Eof) break;
        tokens.push_back(t);
    }

    // हर token को classify करो → (tokenType, tokenModifier)
    // -1 लौटाएं तो token skip होता है (उसके लिए कुछ emit नहीं)।
    auto classify = [](TokenKind k) -> int {
        switch (k) {
        // Control-flow / structural keywords (नियंत्रण-प्रवाह के keywords)
        case TokenKind::If:       case TokenKind::Else:
        case TokenKind::For:      case TokenKind::While:
        case TokenKind::Do:       case TokenKind::Return:
        case TokenKind::Break:    case TokenKind::Continue:
        case TokenKind::Switch:   case TokenKind::Case:
        case TokenKind::Default:  case TokenKind::Class:
        case TokenKind::Goto:     case TokenKind::Sizeof:
        case TokenKind::Typeof:   case TokenKind::Extern:
        case TokenKind::Static:   case TokenKind::Reg:
        case TokenKind::Lock:     case TokenKind::Public:
        case TokenKind::Try:      case TokenKind::Catch:
        case TokenKind::Throw:    case TokenKind::Import:
        case TokenKind::Enum:     case TokenKind::Union:
        case TokenKind::NoReg:
            return kTokKeyword;
        // Built-in type keywords (अंतर्निहित type keywords)
        case TokenKind::U0:  case TokenKind::I0:
        case TokenKind::U8:  case TokenKind::I8:
        case TokenKind::U16: case TokenKind::I16:
        case TokenKind::U32: case TokenKind::I32:
        case TokenKind::U64: case TokenKind::I64:
        case TokenKind::F64: case TokenKind::F32:
        case TokenKind::Bool:
            return kTokType;
        // Literals (literal मान)
        case TokenKind::StringLiteral:
        case TokenKind::CharLiteral:
            return kTokString;
        case TokenKind::IntLiteral:
        case TokenKind::FloatLiteral:
            return kTokNumber;
        // Preprocessor operators (प्रीप्रोसेसर operators)
        case TokenKind::PP_Hash:
        case TokenKind::PP_HashHash:
            return kTokMacro;
        // Operators (संक्रियक)
        case TokenKind::Plus:       case TokenKind::Minus:
        case TokenKind::Star:       case TokenKind::Slash:
        case TokenKind::Percent:    case TokenKind::Ampersand:
        case TokenKind::Pipe:       case TokenKind::Caret:
        case TokenKind::Tilde:      case TokenKind::Bang:
        case TokenKind::Assign:     case TokenKind::PlusAssign:
        case TokenKind::MinusAssign: case TokenKind::StarAssign:
        case TokenKind::SlashAssign: case TokenKind::AmpAssign:
        case TokenKind::PipeAssign:  case TokenKind::ShrAssign:
        case TokenKind::EqEq:       case TokenKind::BangEq:
        case TokenKind::Less:       case TokenKind::LessEq:
        case TokenKind::Greater:    case TokenKind::GreaterEq:
        case TokenKind::Arrow:      case TokenKind::Dot:
        case TokenKind::DoubleColon:
        case TokenKind::PlusPlus:   case TokenKind::MinusMinus:
        case TokenKind::Shl:        case TokenKind::Shr:
            return kTokOperator;
        default:
            return -1; // skip: punctuation और identifiers अलग handle होते हैं
        }
    };

    // (line0, col0, length, type, modifiers) collect करो — सब 0-based।
    struct SemTok { int line, col, len, type, mod; };
    std::vector<SemTok> raw;

    for (const auto& t : tokens) {
        if (t.loc.line == 0) continue;
        int line0 = (int)t.loc.line - 1;
        int col0  = (int)t.loc.col  - 1;
        if (col0 < 0) col0 = 0;
        int len   = (int)t.text.size();
        if (len <= 0) continue;

        int type = classify(t.kind);
        if (type < 0) continue; // बेरंग token — skip

        raw.push_back({line0, col0, len, type, 0});
    }

    // Position से sort करो (पहले से order में होने चाहिए, पर safe रहो)।
    std::sort(raw.begin(), raw.end(), [](const SemTok& a, const SemTok& b) {
        if (a.line != b.line) return a.line < b.line;
        return a.col < b.col;
    });

    // LSP delta-encoded integers के रूप में encode करो: [dLine, dChar, length, type, mod, ...]
    std::vector<int> data;
    data.reserve(raw.size() * 5);
    int prev_line = 0, prev_col = 0;
    for (const auto& st : raw) {
        int dLine = st.line - prev_line;
        int dCol  = (dLine == 0) ? (st.col - prev_col) : st.col;
        data.push_back(dLine);
        data.push_back(dCol);
        data.push_back(st.len);
        data.push_back(st.type);
        data.push_back(st.mod);
        prev_line = st.line;
        prev_col  = st.col;
    }

    return {{"data", data}};
}

} // namespace lsp
} // namespace holyc

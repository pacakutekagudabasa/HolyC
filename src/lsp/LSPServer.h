#pragma once

#include "json.hpp"
#include "../support/SourceManager.h"
#include "../support/Diagnostics.h"
#include "../support/Arena.h"
#include "../ast/AST.h"
#include "../ast/TranslationUnit.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace holyc {
namespace lsp {

using json = nlohmann::json;

/**
 * @brief Document store में parsed document represent करता है।
 *
 * One open file के लिए pipeline results (SourceManager, Diagnostics, Arena, TU) own करता है।
 * Parse न हुआ हो या completely fail हो तो सभी fields null/empty होते हैं।
 */
struct ParsedDocument {
    std::string uri;
    std::string text;
    int version = 0;

    // Pipeline results (parse fail हो तो null)
    std::unique_ptr<SourceManager>    sm;
    std::unique_ptr<Diagnostics>      diag;
    std::unique_ptr<Arena>            arena;
    holyc::TranslationUnit*           tu = nullptr;
};

/**
 * @brief stdin/stdout पर run होने वाला JSON-RPC 2.0 LSP server।
 *
 * Full LSP lifecycle handle करता है (initialize/shutdown), document sync
 * (didOpen/didChange/didClose with 150 ms debounce), और language features
 * (hover, definition, completion, signatureHelp, formatting, documentSymbol,
 * semanticTokens — ये सभी language features support होते हैं)।
 */
class LSPServer {
public:
    LSPServer();

    /**
     * @brief Shutdown request होने या stdin close होने तक main message loop run करो।
     */
    void run();

private:
    // ----- I/O helpers — message read/write के helper methods -----

    /**
     * @brief stdin से एक JSON-RPC message read करो (Content-Length framing)।
     *
     * @return Parsed JSON object, या EOF या framing error पर null।
     */
    json readMessage();

    /**
     * @brief Required Content-Length header के साथ stdout पर JSON-RPC message write करो।
     *
     * @param msg Serialize और send करने वाला JSON object।
     */
    void sendMessage(const json& msg);

    /**
     * @brief Successful JSON-RPC response send करो।
     *
     * @param id     Echo back करने वाला request id।
     * @param result Result payload।
     */
    void sendResponse(const json& id, const json& result);

    /**
     * @brief JSON-RPC error response send करो।
     *
     * @param id      Echo back करने वाला request id।
     * @param code    JSON-RPC error code।
     * @param message Human-readable error description।
     */
    void sendError(const json& id, int code, const std::string& message);

    /**
     * @brief Server-initiated JSON-RPC notification send करो (id नहीं)।
     *
     * @param method LSP notification method name।
     * @param params Notification parameters।
     */
    void sendNotification(const std::string& method, const json& params);

    // ----- Dispatch — incoming messages को handlers पर route करना -----

    /**
     * @brief Incoming JSON-RPC message को appropriate handler पर route करो।
     *
     * @param msg Parsed JSON-RPC message object।
     */
    void handleMessage(const json& msg);

    // ----- Lifecycle — server initialize/shutdown handlers (जीवनचक्र) -----
    json handleInitialize(const json& params);
    void handleInitialized(const json& params);

    // ----- Document sync — document open/change/close handlers (दस्तावेज़ synchronization) -----
    void handleDidOpen(const json& params);
    void handleDidChange(const json& params);
    void handleDidClose(const json& params);

    // ----- Language features — hover/completion/definition आदि handlers -----
    json handleHover(const json& params);
    json handleDefinition(const json& params);
    json handleCompletion(const json& params);
    json handleSignatureHelp(const json& params);
    json handleFormatting(const json& params);
    json handleDocumentSymbol(const json& params);
    json handleSemanticTokensFull(const json& params);

    // ----- Document management — document parse/lookup/publish helpers (दस्तावेज़ प्रबंधन) -----

    /**
     * @brief Document re-parse और type-check करो, उसके sm/diag/arena/tu fields refresh करो।
     *
     * @param doc In-place update करने वाला document।
     */
    void parseDocument(ParsedDocument& doc);

    /**
     * @brief URI से stored document lookup करो।
     *
     * @param uri Search करने वाला document URI।
     * @return Document का pointer, या open नहीं है तो nullptr।
     */
    ParsedDocument* getDocument(const std::string& uri);

    /**
     * @brief Document के textDocument/publishDiagnostics client को push करो।
     *
     * @param doc वह document जिसके diagnostics publish करने हैं।
     */
    void publishDiagnostics(const ParsedDocument& doc);

    // ----- AST helpers — AST traversal और node lookup helpers -----

    /**
     * @brief दिए गए line/col (0-based) पर innermost AST node ढूंढो।
     *
     * @param doc  Search करने वाला document।
     * @param line 0-based line number।
     * @param col  0-based column number।
     * @return Best-matching node, या न मिले तो nullptr।
     */
    holyc::Node* findNodeAt(const ParsedDocument& doc, int line, int col);

    /**
     * @brief सभी declarations collect करने के लिए AST walk करो, flat list में।
     *
     * @param doc जिसका TU walk करना है वह document।
     * @return मिले सभी Decl pointers का flat vector।
     */
    std::vector<holyc::Decl*> collectDecls(const ParsedDocument& doc);

    // ----- State — server की internal state fields -----
    std::map<std::string, ParsedDocument> docs_;
    bool initialized_ = false;
    bool shutdown_requested_ = false;
    int  request_id_ = 0;  // server-initiated requests के लिए

    // ----- didChange के लिए debounce state -----
    bool pending_parse_ = false;
    std::chrono::steady_clock::time_point last_change_time_;
    std::string pending_uri_;
    std::string pending_text_;

    // ----- Helpers — अतिरिक्त utility methods -----

    /**
     * @brief Document के TU में दिए गए name वाला ClassDecl search करो।
     *
     * @param doc  Search करने वाला document।
     * @param name ढूंढने वाला class name।
     * @return Matching ClassDecl का pointer, या न मिले तो nullptr।
     */
    holyc::ClassDecl* findClassDecl(const ParsedDocument& doc, const std::string& name);
};

} // namespace lsp
} // namespace holyc

#pragma once

#include "MacroTable.h"
#include "../lexer/Lexer.h"
#include "../support/SourceManager.h"
#include "../support/Diagnostics.h"
#include "../support/Arena.h"

#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace holyc {

/**
 * @brief C-preprocessor layer: directives, macro expansion, और include stacking handle करता है।
 */
class Preprocessor {
public:
    Preprocessor(SourceManager& sm, Diagnostics& diag, int fileId);

    /**
     * @brief Next preprocessed token consume करके लौटाओ।
     *
     * @return सभी directive और macro processing के बाद next token।
     */
    Token next();

    /**
     * @brief Next preprocessed token बिना consume किए लौटाओ।
     *
     * @return सभी directive और macro processing के बाद next token, stream unchanged।
     */
    Token peek();
    MacroTable& macroTable() { return macros_; }

    void setAotMode(bool aot) { aot_mode_ = aot; }
    void addIncludePath(const std::string& path) { extra_include_paths_.push_back(path); }

    /**
     * @brief Output buffer के front में tokens inject करो (exe/Emit के लिए)।
     *
     * @param tokens Prepend करने वाला token sequence; tokens order में insert होते हैं।
     */
    void injectTokens(const std::deque<Token>& tokens);

    /**
     * @brief SourceManager तक access करो (Emit output lexing के लिए)।
     *
     * @return इस preprocessor के owned SourceManager का reference।
     */
    SourceManager& sourceManager() { return sm_; }
    Diagnostics& diagnostics() { return diag_; }

private:
    /**
     * @brief Active lexer frame से next raw token लौटाओ, exhausted includes pop करते हुए।
     *
     * @return Raw token, या सभी include frames exhausted हो जाएं तो Eof token।
     */
    Token rawNext();

    /**
     * @brief एक directive process करो या next output token produce करो।
     *
     * @param out Output token यहाँ receive होता है अगर produce हुआ।
     * @return True अगर token @p out में रखा गया; false अगर directive consume हुआ।
     */
    bool produce(Token& out);

    /**
     * @brief #define directive process करो, macro को macro table में register करो।
     */
    void handleDefine();

    /**
     * @brief #undef directive process करो।
     */
    void handleUndef();

    /**
     * @brief #include directive process करो; angle-bracket (C import) और quoted forms दोनों handle करो।
     *
     * @param directive #include directive token (error location reporting के लिए)।
     */
    void handleInclude(const Token& directive);

    /**
     * @brief #ifdef (sense=true) या #ifndef (sense=false) process करो।
     *
     * @param sense #ifdef के लिए True, #ifndef के लिए false।
     */
    void handleIfdef(bool sense);

    /**
     * @brief #if directive process करो, उसका constant expression evaluate करो।
     *
     * @param directive #if directive token (error location reporting के लिए)।
     */
    void handleIf(const Token& directive);

    /**
     * @brief #elif directive process करो जब preceding branch active था।
     */
    void handleElif();

    /**
     * @brief #else directive process करो जब preceding branch active था।
     */
    void handleElse();
    void handleEndif();

    /**
     * @brief #ifaot process करो — केवल AOT compilation mode में active।
     */
    void handleIfaot();

    /**
     * @brief #ifjit process करो — केवल JIT (interpreter) mode में active।
     */
    void handleIfjit();

    /**
     * @brief #assert directive process करो; expression zero हो तो error emit करो।
     *
     * @param directive #assert directive token (error location reporting के लिए)।
     */
    void handleAssert(const Token& directive);

    /**
     * @brief #exe block process करो: preprocess time पर execute करो और Emit output inject करो।
     */
    void handleExe();

    /**
     * @brief Matching #else, #elif, या #endif तक tokens skip करो, nesting respect करो।
     */
    void skipConditionalBlock();

    /**
     * @brief Current logical line के remaining tokens collect करो।
     *
     * @param line Non-zero हो तो इस specific source line के tokens collect करो;
     *             नहीं तो first peeked token की line use करो।
     * @return Logical line के tokens का sequence, line-continuation spliced के साथ।
     */
    std::vector<Token> collectLineTokens(uint32_t line = 0);

    /**
     * @brief Token list से preprocessor constant expression evaluate करो।
     *
     * @param tokens Expression represent करने वाला token sequence।
     * @return Expression का integer result, या error पर 0।
     */
    int64_t evalConstExpr(const std::vector<Token>& tokens);

    /**
     * @brief Recursive-descent constant-expression evaluator — logical OR level।
     *
     * @param tokens Full token sequence।
     * @param pos    @p tokens में current index; consumed tokens के past advance होता है।
     * @return Sub-expression का integer result।
     */
    int64_t evalOr(const std::vector<Token>& tokens, size_t& pos);

    /**
     * @brief Recursive-descent constant-expression evaluator — logical AND level।
     *
     * @param tokens Full token sequence।
     * @param pos    @p tokens में current index; consumed tokens के past advance होता है।
     * @return Sub-expression का integer result।
     */
    int64_t evalAnd(const std::vector<Token>& tokens, size_t& pos);
    int64_t evalBitwiseOr(const std::vector<Token>& tokens, size_t& pos);
    int64_t evalBitwiseXor(const std::vector<Token>& tokens, size_t& pos);
    int64_t evalBitwiseAnd(const std::vector<Token>& tokens, size_t& pos);
    int64_t evalEquality(const std::vector<Token>& tokens, size_t& pos);
    int64_t evalRelational(const std::vector<Token>& tokens, size_t& pos);

    /**
     * @brief Shift operators evaluate करो, out-of-range shift amounts से guard करो।
     *
     * @param tokens Full token sequence।
     * @param pos    @p tokens में current index; consumed tokens के past advance होता है।
     * @return Shift sub-expression का integer result।
     */
    int64_t evalShift(const std::vector<Token>& tokens, size_t& pos);

    /**
     * @brief Addition और subtraction evaluate करो, overflow INT64 bounds तक clamp करो।
     *
     * @param tokens Full token sequence।
     * @param pos    @p tokens में current index; consumed tokens के past advance होता है।
     * @return Additive sub-expression का integer result।
     */
    int64_t evalAdditive(const std::vector<Token>& tokens, size_t& pos);
    int64_t evalMultiplicative(const std::vector<Token>& tokens, size_t& pos);
    int64_t evalUnary(const std::vector<Token>& tokens, size_t& pos);

    /**
     * @brief Primary evaluate करो: integer/char literal, defined(), identifier, या parenthesised expression।
     *
     * @param tokens Full token sequence।
     * @param pos    @p tokens में current index; consumed tokens के past advance होता है।
     * @return Primary की integer value।
     */
    int64_t evalPrimary(const std::vector<Token>& tokens, size_t& pos);

    /**
     * @brief दिए गए identifier token को macro के रूप में expand करने की कोशिश करो।
     *
     * @param ident  Expand करने वाला identifier token।
     * @param output Destination deque जो expanded tokens receive करता है।
     * @return True अगर expansion हुई; false अगर कोई matching macro न मिला।
     */
    bool tryExpandMacro(const Token& ident, std::deque<Token>& output);

    bool isActive() const { return inactive_depth_ == 0; }

    std::deque<Token> buffer_;

    /**
     * @brief produce() call करके output buffer populate करो जब तक कम से कम एक token ready न हो।
     */
    void fillBuffer();

    struct LexerFrame {
        std::unique_ptr<Lexer> lexer;
        int fileId;
    };
    std::vector<LexerFrame> include_stack_;

    SourceManager& sm_;
    Diagnostics& diag_;
    MacroTable macros_;

    bool aot_mode_ = false;
    std::vector<std::string> extra_include_paths_;

    enum class CondState { Active, SkipToElse, SkipToEnd, Done };
    std::vector<CondState> cond_stack_;

    void condPush(CondState s) {
        cond_stack_.push_back(s);
        if (s != CondState::Active) ++inactive_depth_;
    }
    void condPop() {
        if (!cond_stack_.empty()) {
            if (cond_stack_.back() != CondState::Active) --inactive_depth_;
            cond_stack_.pop_back();
        }
    }
    void condSetBack(CondState s) {
        if (!cond_stack_.empty()) {
            if (cond_stack_.back() != CondState::Active) --inactive_depth_;
            cond_stack_.back() = s;
            if (s != CondState::Active) ++inactive_depth_;
        }
    }

    std::set<std::string> expanding_;

    int expand_depth_ = 0;
    static constexpr int kMaxExpandDepth = 200;
    int inactive_depth_ = 0;

    std::deque<std::string> synth_strings_;

    /**
     * @brief Synthesized token string के लिए stable storage allocate करो।
     *
     * @param s Store करने वाली string; internal deque में move होती है।
     * @return Stored copy का string_view, इस Preprocessor के lifetime तक valid।
     */
    std::string_view synthText(std::string s) {
        synth_strings_.push_back(std::move(s));
        return std::string_view(synth_strings_.back());
    }

    static constexpr int kMaxIncludeDepth = 32;
};

} // namespace holyc

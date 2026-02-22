#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ast/AST.h"
#include "ast/TranslationUnit.h"
#include "interpreter/Heap.h"
#include "interpreter/Value.h"
#include "support/Diagnostics.h"

namespace holyc {

/**
 * @brief Interpreter में HolyC throw/catch implement करने के लिए C++ exception।
 */
struct HolyCException {
    int64_t code;
};

/**
 * @brief HolyC के लिए tree-walk interpreter।
 */
class Interpreter {
public:
    explicit Interpreter(Diagnostics& diag);

    /**
     * @brief Translation unit execute करो।
     *
     * सभी top-level declarations register करता है, top-level statements execute करता है,
     * और फिर अगर Main() defined है तो उसे call करता है।
     *
     * @param tu Execute करने वाला translation unit।
     * @return Main() का return किया exit code, या Main() defined न हो तो 0।
     */
    int run(TranslationUnit* tu);

    /**
     * @brief Single expression evaluate करो (public REPL entry point)।
     *
     * @param expr Evaluate करने वाला expression।
     * @return Resulting value।
     */
    Value eval(Expr* expr);

    /**
     * @brief Exe-block context में compound statement execute करो और Emit() output collect करो।
     *
     * @param body Execute करने वाला compound statement।
     * @return Execution के दौरान Emit() से लिखा गया सारा text।
     */
    std::string runExeBlock(CompoundStmt* body);

    /** @brief Emit() output buffer का reference लौटाओ। */
    std::string& emitBuffer() { return emit_buffer_; }

    /**
     * @brief User-defined functions, classes, और globals reset करो; builtins preserve रहते हैं।
     */
    void reset();

    /**
     * @brief Managed interpreter heap के लिए heap statistics लौटाओ।
     *
     * @return {total_bytes_allocated, number_of_live_blocks} का pair।
     */
    std::pair<size_t, size_t> heapStats() const;

    /** @brief Global variable table का mutable reference लौटाओ। */
    std::unordered_map<std::string, Value>& globals() { return globals_; }
    /** @brief Global variable table का read-only reference लौटाओ। */
    const std::unordered_map<std::string, Value>& globals() const { return globals_; }
    /** @brief User-defined function table का mutable reference लौटाओ। */
    std::unordered_map<std::string, FuncDecl*>& functions() { return functions_; }
    /** @brief User-defined function table का read-only reference लौटाओ। */
    const std::unordered_map<std::string, FuncDecl*>& functions() const { return functions_; }
    /** @brief Registered class declaration table का read-only reference लौटाओ। */
    const std::unordered_map<std::string, ClassDecl*>& classDecls() const { return class_decls_; }

private:
    /**
     * @brief Node kind के हिसाब से expression evaluation dispatch करो; interpreter का hot path।
     *
     * @param e Evaluate करने वाला expression node।
     * @return Expression से produce हुई value।
     */
    [[gnu::hot]] Value evalExpr(Expr* e);

    /**
     * @brief Binary expression evaluate करो, assignments, short-circuit logic, और arithmetic handle करो।
     *
     * Assignment operators LHS को assignToExpr के through in-place mutate करते हैं। Logical AND/OR short-circuit करते हैं।
     * Integer, unsigned, और float operand types अलग-अलग dispatch paths follow करते हैं।
     *
     * @param e Binary expression node।
     * @return Result value; operand types के हिसाब से integer, unsigned, या float।
     */
    [[gnu::hot]] Value evalBinaryOp(BinaryExpr* e);

    /**
     * @brief Unary expression evaluate करो जिसमें pre/post increment, negate, dereference, और address-of शामिल हैं।
     *
     * @param e Unary expression node।
     * @return Resulting value; pre/post increment operand को assignToExpr के through mutate करते हैं।
     */
    Value evalUnaryOp(UnaryExpr* e);

    /**
     * @brief Function name से resolve और invoke करो, builtins, extern symbols, और user functions handle करो।
     *
     * नया call frame set up करता है, parameters bind करता है, body execute करता है, फिर interpreter state restore करता है।
     *
     * @param e Call expression node।
     * @return Called function का returned value।
     */
    Value evalCall(CallExpr* e);

    /**
     * @brief Struct या union से computed byte offset पर named field read करो।
     *
     * Bit-fields, pointer-typed fields, और IntrinsicUnion pseudo-type handle करता है।
     *
     * @param e Field-access expression node।
     * @return Raw memory से extracted field value।
     */
    Value evalFieldAccess(FieldAccessExpr* e);

    /**
     * @brief Heap-allocated array में index करो, resolved type info से element size respect करो।
     *
     * @param e Array-index expression node।
     * @return Element value; chained indexing support के लिए nested array types पर pointer लौटाता है।
     */
    Value evalArrayIndex(ArrayIndexExpr* e);

    /**
     * @brief Ternary conditional expression evaluate करो, न चुनी गई branch short-circuit हो।
     *
     * @param e Ternary expression node।
     * @return Taken branch की value।
     */
    Value evalTernary(TernaryExpr* e);

    /**
     * @brief Chained comparison (जैसे a < b < c) को sequential pairwise tests की तरह evaluate करो।
     *
     * हर pair left-to-right evaluate होती है; कोई भी pair fail हो तो result false होता है।
     *
     * @param e Chained comparison expression node।
     * @return Boolean true अगर सभी pairwise comparisons hold करें, नहीं तो false।
     */
    Value evalChainedCmp(ChainedCmpExpr* e);

    /**
     * @brief Power expression (base ** exp) evaluate करो।
     *
     * @param e Power expression node।
     * @return दोनों operands integers हों तो integer result; नहीं तो float।
     */
    Value evalPower(PowerExpr* e);

    /**
     * @brief Postfix type cast (जैसे expr(I64)) apply करो, pointers के लिए value का bit pattern reinterpret करो।
     *
     * @param e Postfix cast expression node।
     * @return Target type में reinterpreted value।
     */
    Value evalPostfixCast(PostfixCastExpr* e);

    /**
     * @brief Type या expression का byte size लौटाओ; class types के लिए struct_layouts_ consult करो।
     *
     * @param e Sizeof expression node।
     * @return Byte size represent करने वाला integer value।
     */
    Value evalSizeof(SizeofExpr* e);

    /**
     * @brief Class layout में named member का byte offset लौटाओ।
     *
     * Most recently defined class refer करने के लिए `lastclass` pseudo-name support करता है।
     *
     * @param e Offset expression node।
     * @return Named member का integer byte offset, या न मिले तो 0।
     */
    Value evalOffset(OffsetExpr* e);

    /**
     * @brief Single statement dispatch करो; signal pending हो तो execution skip करो।
     *
     * @param s Execute करने वाला statement node।
     */
    [[gnu::hot]] void execStmt(Stmt* s);

    /**
     * @brief Statements का block sequentially execute करो; goto-as-loop implement करने के लिए backward gotos पर re-enter करो।
     *
     * @param s Execute करने वाला compound statement node।
     */
    [[gnu::hot]] void execCompound(CompoundStmt* s);

    /**
     * @brief Condition evaluate करो और taken branch execute करो; न चुनी गई branch पूरी skip हो।
     *
     * @param s If statement node।
     */
    void execIf(IfStmt* s);

    /**
     * @brief For loop execute करो; init एक बार run करो, हर iteration से पहले cond test करो, हर body के बाद post run करो।
     *
     * @param s For statement node।
     */
    void execFor(ForStmt* s);

    /**
     * @brief While loop execute करो; हर body execution से पहले cond test करो।
     *
     * @param s While statement node।
     */
    void execWhile(WhileStmt* s);

    /**
     * @brief Do-while loop execute करो; cond test से पहले body कम से कम एक बार हमेशा run करो।
     *
     * @param s Do-while statement node।
     */
    void execDoWhile(DoWhileStmt* s);

    /**
     * @brief Switch statement execute करो; value ranges (case N..M:) और fall-through support करो।
     *
     * @param s Switch statement node।
     */
    void execSwitch(SwitchStmt* s);

    /**
     * @brief Current frame में return value store करो और Signal::Return raise करो।
     *
     * @param s Return statement node।
     */
    void execReturn(ReturnStmt* s);

    /**
     * @brief "..."(args) output statement के format string और arguments evaluate करो और print करो।
     *
     * @param s String-output statement node।
     */
    void execStringOutput(StringOutputStmt* s);

    /**
     * @brief Current scope में declaration register करो।
     *
     * Variables (local और global), functions, classes (layout computation trigger), enums,
     * और dlsym से resolve किए extern C bindings handle करता है।
     *
     * @param d Register करने वाला declaration node।
     */
    void execDecl(Decl* d);

    /** @brief True लौटाओ अगर name built-in function के रूप में registered है। */
    bool isBuiltin(const std::string& name) const;

    /**
     * @brief builtins_ को सभी built-in HolyC functions (math, string, I/O, memory, आदि) से populate करो।
     */
    void registerBuiltins();

    /**
     * @brief HolyC extensions के साथ printf-style string format करो।
     *
     * Standard C specifiers के अलावा %h (repeat modifier), %n (engineering notation),
     * %z (indexed null-separated list), %D (date), और %T (time) support करता है।
     *
     * @param fmt Format string।
     * @param args Argument vector। Indexing arg_start से शुरू होती है।
     * @param arg_start args में वह index जहाँ से format arguments शुरू होते हैं; args[0] को format string ही रहने देता है।
     * @return Fully formatted output string।
     */
    std::string formatPrint(const std::string& fmt, const std::vector<Value>& args, size_t arg_start = 0);

    /**
     * @brief दो values को दिए गए relational operator से compare करो; ज़रूरत पर float, string, या uint promote करो।
     *
     * @param op Relational operator kind (Eq, Ne, Lt, Le, Gt, Ge)।
     * @param lhs Left-hand side value।
     * @param rhs Right-hand side value।
     * @return Comparison result represent करने वाला boolean value।
     */
    Value evalCmpOp(BinOpKind op, const Value& lhs, const Value& rhs);

    /**
     * @brief val को target से denoted location पर write करो।
     *
     * Target identifier, dereference, array index, या field access हो सकता है।
     * Struct bit-fields और method bodies के अंदर implicit this-field assignment handle करता है।
     *
     * @param target Assignment receive करने वाला lvalue expression।
     * @param val Store करने वाली value।
     */
    void assignToExpr(Expr* target, const Value& val);

    enum class Signal { None, Break, Continue, Return };
    Signal signal_ = Signal::None;

    struct Frame {
        std::unordered_map<std::string, Value> vars;
        std::unordered_map<std::string, std::string> static_keys; // varName → "funcName.varName"
        std::string func_name;
        Value return_val;
        bool has_returned = false;
        std::vector<Value> varargs;
    };
    std::vector<Frame> call_stack_;
    std::unordered_map<std::string, Value> globals_;
    std::unordered_map<std::string, FuncDecl*> functions_;

    std::unordered_map<std::string, Value> static_vars_;
    std::unordered_set<std::string> static_initialized_;
    std::string current_func_name_;

    // "ClassName$methodName" → inheritance walk के बाद resolved function name
    std::unordered_map<std::string, std::string> method_cache_;

    std::string current_method_class_;

    struct FieldLayout {
        std::string name;
        size_t offset;
        int bit_start = 0;
        int bit_width = -1; // -1 for non-bitfield
    };
    struct StructLayout {
        std::vector<FieldLayout> field_offsets;
        size_t total_size = 0;
        std::unordered_map<std::string, size_t> field_index;
    };
    std::unordered_map<std::string, StructLayout> struct_layouts_;

    /**
     * @brief Class के लिए field byte offsets compute करो, base-class layout inherit करो और bit-fields pack करो।
     *
     * Bit-fields contiguous 8-byte words में pack होते हैं। Same group के union members
     * same starting offset share करते हैं। Results struct_layouts_ में store होते हैं।
     *
     * @param cd वह class declaration जिसका layout compute करना है।
     */
    void computeStructLayout(ClassDecl* cd);
    std::unordered_map<std::string, ClassDecl*> class_decls_;
    ClassDecl* last_class_decl_ = nullptr;

    Heap heap_;
    Diagnostics& diag_;

    std::string emit_buffer_;
    bool in_exe_block_ = false;

    /**
     * @brief Call stack पर या globals में existing variable binding को val से write करो।
     *
     * Existing binding के लिए call stack top-down search करता है। Name न मिले तो
     * current frame (या globals) में नई binding create करता है।
     *
     * @param name Variable name।
     * @param val Store करने वाली value।
     */
    void setVar(const std::string& name, Value val);

    /**
     * @brief Call stack top-down, फिर globals search करके variable read करो।
     *
     * @param name Variable name।
     * @return Variable की value, या न मिले तो integer 0।
     */
    Value getVar(const std::string& name);

    /**
     * @brief In-place mutation के लिए variable के storage का pointer लौटाओ।
     *
     * @param name Variable name।
     * @return Owning container में Value का pointer, या न मिले तो nullptr।
     */
    Value* getVarPtr(const std::string& name);

    using BuiltinFn = std::function<Value(const std::vector<Value>&)>;
    std::unordered_map<std::string, BuiltinFn> builtins_;

    struct ExternBinding {
        void* sym;
        std::vector<std::string> param_types; // "i64", "f64", "ptr"
        std::string return_type;              // "i64", "f64", "ptr", "void"
    };
    std::unordered_map<std::string, ExternBinding> extern_syms_;

    struct LabelInfo {
        CompoundStmt* parent;
        size_t index;
    };
    std::unordered_map<std::string, LabelInfo> labels_;
    std::string pending_goto_;

    /**
     * @brief Goto resolution के लिए labels_ build करने को compound statement recursively scan करो।
     *
     * हर label का parent CompoundStmt और उस compound में index record करता है ताकि
     * execCompound सही position से execution resume कर सके।
     *
     * @param body Scan करने वाला compound statement।
     */
    void scanLabels(CompoundStmt* body);

    /**
     * @brief Containing compound के अंदर label के बाद वाले statement से execution resume करो।
     *
     * @param label Jump करने वाला label name।
     */
    void execFromLabel(const std::string& label);
};

} // namespace holyc

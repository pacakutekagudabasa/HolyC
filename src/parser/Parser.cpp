#include "parser/Parser.h"
#include "lexer/Lexer.h"
#include "support/SourceManager.h"

#include <cassert>
#include <deque>
#include <string>

namespace holyc {

/**
 * @brief RAII guard जो recursive parse depth को limit करता है।
 */
struct ParseDepthGuard {
    int& depth;
    bool ok;
    ParseDepthGuard(int& d, int max) : depth(d), ok(d < max) { if (ok) ++depth; }
    ~ParseDepthGuard() { if (ok) --depth; }
};

/**
 * @brief Raw string literal body में HolyC/C escape sequences expand करो (quotes के बिना)।
 *
 * @param raw Raw string literal content, surrounding quote characters के बिना।
 * @return Decoded string जिसमें सभी escape sequences उनके byte values से replace हुए हों।
 */
static std::string decodeStringEscapes(std::string_view raw) {
    std::string result;
    result.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            i++;
            switch (raw[i]) {
            case '0': result += '\0'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case '\\': result += '\\'; break;
            case '\'': result += '\''; break;
            case '"': result += '"'; break;
            case 'd': result += '$'; break;
            case 'x':
                if (i + 2 < raw.size()) {
                    char hex[3] = {raw[i+1], raw[i+2], 0};
                    result += (char)strtol(hex, nullptr, 16);
                    i += 2;
                }
                break;
            default: result += raw[i]; break;
            }
        } else {
            result += raw[i];
        }
    }
    return result;
}

Parser::Parser(Preprocessor& pp, Diagnostics& diag, Arena& arena)
    : pp_(pp), diag_(diag), arena_(arena), exe_interp_(diag)
{
    current_ = pp_.next();
}

Token Parser::peek(int ahead) {
    if (ahead < 0 || ahead >= static_cast<int>(std::size(lookahead_))) {
        return Token{TokenKind::Eof, {}, {}};
    }
    if (ahead == 0) return current_;
    while (la_count_ < ahead) {
        int idx = (la_head_ + la_count_) & 3;
        lookahead_[idx] = pp_.next();
        ++la_count_;
    }
    return lookahead_[(la_head_ + ahead - 1) & 3];
}

Token Parser::consume() {
    Token t = current_;
    if (la_count_ > 0) {
        current_ = lookahead_[la_head_];
        la_head_ = (la_head_ + 1) & 3;
        --la_count_;
    } else {
        current_ = pp_.next();
    }
    return t;
}

Token Parser::expect(TokenKind kind) {
    if (current_.kind == kind)
        return consume();
    diag_.error(current_.loc,
                std::string("expected '") + tokenKindToString(kind) +
                "' but got '" + tokenKindToString(current_.kind) + "'");
    return current_;
}

bool Parser::match(TokenKind kind) {
    if (current_.kind == kind) {
        consume();
        return true;
    }
    return false;
}

bool Parser::check(TokenKind kind) {
    return current_.kind == kind;
}

void Parser::synchronize() {
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Semicolon)) {
            consume();
            return;
        }
        if (check(TokenKind::RBrace))    return;
        if (check(TokenKind::LBrace))    return;
        if (check(TokenKind::If)    || check(TokenKind::For)   ||
            check(TokenKind::While) || check(TokenKind::Return)||
            check(TokenKind::Class) || check(TokenKind::Goto))
            return;
        consume();
    }
}

bool Parser::isTypeToken(TokenKind k) {
    return (k >= TokenKind::U0 && k <= TokenKind::Bool) ||
           (k >= TokenKind::U8i && k <= TokenKind::F64i);
}

bool Parser::isTypeStart() {
    return isTypeToken(current_.kind) ||
           current_.kind == TokenKind::Class ||
           current_.kind == TokenKind::Union ||
           current_.kind == TokenKind::Typeof ||
           (current_.kind == TokenKind::Identifier &&
            known_class_names_.count(current_.text));
}

bool Parser::isComparisonOp(TokenKind k) {
    return k == TokenKind::Less || k == TokenKind::Greater ||
           k == TokenKind::LessEq || k == TokenKind::GreaterEq ||
           k == TokenKind::EqEq || k == TokenKind::BangEq;
}

bool Parser::isAssignmentOp(TokenKind k) {
    switch (k) {
    case TokenKind::Assign:
    case TokenKind::PlusAssign: case TokenKind::MinusAssign:
    case TokenKind::StarAssign: case TokenKind::SlashAssign:
    case TokenKind::PercentAssign:
    case TokenKind::AmpAssign: case TokenKind::PipeAssign:
    case TokenKind::CaretAssign:
    case TokenKind::ShlAssign: case TokenKind::ShrAssign:
    case TokenKind::PPAssign: case TokenKind::MMAssign:
        return true;
    default:
        return false;
    }
}

BinOpKind Parser::tokenToAssignOp(TokenKind k) {
    switch (k) {
    case TokenKind::Assign:        return BinOpKind::Assign;
    case TokenKind::PlusAssign:    return BinOpKind::AddAssign;
    case TokenKind::MinusAssign:   return BinOpKind::SubAssign;
    case TokenKind::StarAssign:    return BinOpKind::MulAssign;
    case TokenKind::SlashAssign:   return BinOpKind::DivAssign;
    case TokenKind::PercentAssign: return BinOpKind::ModAssign;
    case TokenKind::AmpAssign:     return BinOpKind::BitAndAssign;
    case TokenKind::PipeAssign:    return BinOpKind::BitOrAssign;
    case TokenKind::CaretAssign:   return BinOpKind::BitXorAssign;
    case TokenKind::ShlAssign:     return BinOpKind::ShlAssign;
    case TokenKind::ShrAssign:     return BinOpKind::ShrAssign;
    case TokenKind::PPAssign:      return BinOpKind::PPAssign;
    case TokenKind::MMAssign:      return BinOpKind::MMAssign;
    default:                       return BinOpKind::Assign;
    }
}

BinOpKind Parser::tokenToCmpOp(TokenKind k) {
    switch (k) {
    case TokenKind::Less:      return BinOpKind::Lt;
    case TokenKind::Greater:   return BinOpKind::Gt;
    case TokenKind::LessEq:    return BinOpKind::Le;
    case TokenKind::GreaterEq: return BinOpKind::Ge;
    case TokenKind::EqEq:      return BinOpKind::Eq;
    case TokenKind::BangEq:    return BinOpKind::Ne;
    default:                   return BinOpKind::Eq;
    }
}

bool Parser::isDeclaration() {
    TokenKind k = current_.kind;
    if (k == TokenKind::Public || k == TokenKind::Static ||
        k == TokenKind::Extern || k == TokenKind::_Extern ||
        k == TokenKind::_Intern || k == TokenKind::Import ||
        k == TokenKind::_Import)
        return true;
    if (k == TokenKind::Interrupt || k == TokenKind::HasErrCode ||
        k == TokenKind::ArgPop || k == TokenKind::NoArgPop)
        return true;
    if (k == TokenKind::Reg || k == TokenKind::NoReg || k == TokenKind::NoWarn)
        return true;
    if (isTypeToken(k))
        return true;
    if (k == TokenKind::Class || k == TokenKind::Union || k == TokenKind::Typedef ||
        k == TokenKind::Enum || k == TokenKind::Typeof)
        return true;
    // "ClassName::Method(...)" qualified call है, declaration नहीं।
    if (k == TokenKind::Identifier &&
        known_class_names_.count(current_.text)) {
        if (peek(1).kind == TokenKind::DoubleColon)
            return false;
        return true;
    }
    if (k == TokenKind::Auto)
        return true;
    return false;
}

Type* Parser::parseType() {
    ParseDepthGuard dg(parse_depth_, kMaxParseDepth);
    if (!dg.ok) {
        diag_.error(current_.loc, "type too deeply nested");
        Type* t = arena_.alloc<Type>();
        t->kind = Type::Prim;
        t->data = PrimitiveType{PrimKind::I64};
        return t;
    }

    Type* base = nullptr;

    if (check(TokenKind::Typeof)) {
        consume(); // typeof keyword consume करो
        expect(TokenKind::LParen);
        Expr* e = parseExpr();
        expect(TokenKind::RParen);
        base = arena_.alloc<Type>();
        base->kind = Type::Typeof;
        base->data = TypeofData{e};
        // नीचे star/array suffix parsing तक fall through करो
    } else if (current_.kind == TokenKind::Identifier &&
        known_class_names_.count(current_.text)) {
        Token t = consume();
        base = arena_.alloc<Type>();
        base->kind = Type::Class;
        // Placeholder decl; sema असली ClassDecl fill करेगा।
        ClassDecl* placeholder = arena_.alloc<ClassDecl>();
        placeholder->name = std::string(t.text);
        base->data = ClassType{placeholder};
    } else if (isTypeToken(current_.kind)) {
        Token t = consume();
        base = arena_.alloc<Type>();
        if (t.kind >= TokenKind::U8i && t.kind <= TokenKind::F64i) {
            base->kind = Type::IntrinsicUnion;
            IntrinsicUnionKind ik;
            switch (t.kind) {
            case TokenKind::U8i:  ik = IntrinsicUnionKind::U8i;  break;
            case TokenKind::I8i:  ik = IntrinsicUnionKind::I8i;  break;
            case TokenKind::U16i: ik = IntrinsicUnionKind::U16i; break;
            case TokenKind::I16i: ik = IntrinsicUnionKind::I16i; break;
            case TokenKind::U32i: ik = IntrinsicUnionKind::U32i; break;
            case TokenKind::I32i: ik = IntrinsicUnionKind::I32i; break;
            case TokenKind::U64i: ik = IntrinsicUnionKind::U64i; break;
            case TokenKind::I64i: ik = IntrinsicUnionKind::I64i; break;
            case TokenKind::F64i: ik = IntrinsicUnionKind::F64i; break;
            default: ik = IntrinsicUnionKind::U8i; break;
            }
            base->data = IntrinsicUnionType{ik};
        } else {
            base->kind = Type::Prim;
            PrimKind pk;
            switch (t.kind) {
            case TokenKind::U0:   pk = PrimKind::U0;   break;
            case TokenKind::I0:   pk = PrimKind::I0;   break;
            case TokenKind::U8:   pk = PrimKind::U8;   break;
            case TokenKind::I8:   pk = PrimKind::I8;   break;
            case TokenKind::U16:  pk = PrimKind::U16;  break;
            case TokenKind::I16:  pk = PrimKind::I16;  break;
            case TokenKind::U32:  pk = PrimKind::U32;  break;
            case TokenKind::I32:  pk = PrimKind::I32;  break;
            case TokenKind::U64:  pk = PrimKind::U64;  break;
            case TokenKind::I64:  pk = PrimKind::I64;  break;
            case TokenKind::F64:  pk = PrimKind::F64;  break;
            case TokenKind::F32:  pk = PrimKind::F32;  break;
            case TokenKind::Bool: pk = PrimKind::Bool;  break;
            default: pk = PrimKind::I64; break;
            }
            base->data = PrimitiveType{pk};
        }
    } else {
        diag_.error(current_.loc, "expected type");
        base = arena_.alloc<Type>();
        base->kind = Type::Prim;
        base->data = PrimitiveType{PrimKind::I64};
    }

    int stars = 0;
    while (check(TokenKind::Star)) {
        consume();
        ++stars;
    }
    if (stars > 0) {
        Type* ptr = arena_.alloc<Type>();
        ptr->kind = Type::Pointer;
        ptr->data = PointerType{base, stars};
        base = ptr;
    }

    if (check(TokenKind::LBracket)) {
        consume();
        int size = -1;
        if (check(TokenKind::IntLiteral)) {
            if (current_.intVal > static_cast<uint64_t>(INT32_MAX)) {
                diag_.error(current_.loc, "array dimension too large");
                size = 0;
            } else {
                size = static_cast<int>(current_.intVal);
            }
            consume();
        }
        expect(TokenKind::RBracket);
        Type* arr = arena_.alloc<Type>();
        arr->kind = Type::Array;
        arr->data = ArrayType{base, size};
        base = arr;
    }

    return base;
}

Expr* Parser::parseExpr() {
    ParseDepthGuard dg(parse_depth_, kMaxParseDepth);
    if (!dg.ok) {
        diag_.error(current_.loc, "expression or statement too deeply nested");
        return nullptr;
    }
    return parseAssignment();
}

Expr* Parser::parseAssignment() {
    Expr* lhs = parseTernary();
    if (isAssignmentOp(current_.kind)) {
        Token op = consume();
        Expr* rhs = parseAssignment();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = tokenToAssignOp(op.kind);
        node->lhs = lhs;
        node->rhs = rhs;
        return node;
    }
    return lhs;
}

Expr* Parser::parseTernary() {
    Expr* cond = parseLogicalOr();
    if (match(TokenKind::Question)) {
        Expr* then_expr = parseExpr();
        expect(TokenKind::Colon);
        Expr* else_expr = parseTernary();
        auto* node = arena_.alloc<TernaryExpr>();
        node->loc = cond->loc;
        node->cond = cond;
        node->then_expr = then_expr;
        node->else_expr = else_expr;
        return node;
    }
    return cond;
}

Expr* Parser::parseLogicalOr() {
    Expr* lhs = parseLogicalXor();
    while (check(TokenKind::PipePipe)) {
        Token op = consume();
        Expr* rhs = parseLogicalXor();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = BinOpKind::LogOr;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

Expr* Parser::parseLogicalXor() {
    Expr* lhs = parseLogicalAnd();
    while (check(TokenKind::CaretCaret)) {
        Token op = consume();
        Expr* rhs = parseLogicalAnd();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = BinOpKind::LogXor;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

Expr* Parser::parseLogicalAnd() {
    Expr* lhs = parseShift();
    while (check(TokenKind::AmpAmp)) {
        Token op = consume();
        Expr* rhs = parseShift();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = BinOpKind::LogAnd;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

// HolyC में shift की precedence comparison से कम होती है।
Expr* Parser::parseShift() {
    Expr* lhs = parseComparison();
    while (check(TokenKind::Shl) || check(TokenKind::Shr)) {
        Token op = consume();
        Expr* rhs = parseComparison();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = (op.kind == TokenKind::Shl) ? BinOpKind::Shl : BinOpKind::Shr;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

Expr* Parser::parseComparison() {
    Expr* lhs = parseAdditive();
    if (!isComparisonOp(current_.kind))
        return lhs;

    Token op1 = consume();
    Expr* rhs = parseAdditive();

    if (!isComparisonOp(current_.kind)) {
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op1.loc;
        node->op = tokenToCmpOp(op1.kind);
        node->lhs = lhs;
        node->rhs = rhs;
        return node;
    }

    auto* chain = arena_.alloc<ChainedCmpExpr>();
    chain->loc = lhs->loc;
    chain->operands.push_back(lhs);
    chain->operands.push_back(rhs);
    chain->ops.push_back(tokenToCmpOp(op1.kind));

    while (isComparisonOp(current_.kind)) {
        Token opN = consume();
        Expr* next = parseAdditive();
        chain->operands.push_back(next);
        chain->ops.push_back(tokenToCmpOp(opN.kind));
    }
    return chain;
}

Expr* Parser::parseAdditive() {
    Expr* lhs = parseBitOr();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        Token op = consume();
        Expr* rhs = parseBitOr();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = (op.kind == TokenKind::Plus) ? BinOpKind::Add : BinOpKind::Sub;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

// HolyC में bitwise OR की precedence +/- से ज़्यादा है।
Expr* Parser::parseBitOr() {
    Expr* lhs = parseBitXor();
    while (check(TokenKind::Pipe)) {
        Token op = consume();
        Expr* rhs = parseBitXor();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = BinOpKind::BitOr;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

Expr* Parser::parseBitXor() {
    Expr* lhs = parseBitAnd();
    while (check(TokenKind::Caret)) {
        Token op = consume();
        Expr* rhs = parseBitAnd();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = BinOpKind::BitXor;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

Expr* Parser::parseBitAnd() {
    Expr* lhs = parseMultiplicative();
    while (check(TokenKind::Ampersand)) {
        Token op = consume();
        Expr* rhs = parseMultiplicative();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        node->op = BinOpKind::BitAnd;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

Expr* Parser::parseMultiplicative() {
    Expr* lhs = parsePower();
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
        Token op = consume();
        Expr* rhs = parsePower();
        auto* node = arena_.alloc<BinaryExpr>();
        node->loc = op.loc;
        if (op.kind == TokenKind::Star)         node->op = BinOpKind::Mul;
        else if (op.kind == TokenKind::Slash)   node->op = BinOpKind::Div;
        else                                     node->op = BinOpKind::Mod;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }
    return lhs;
}

Expr* Parser::parsePower() {
    Expr* base = parseUnary();
    if (check(TokenKind::Backtick)) {
        Token op = consume();
        Expr* exp = parsePower();
        auto* node = arena_.alloc<PowerExpr>();
        node->loc = op.loc;
        node->base = base;
        node->exp = exp;
        return node;
    }
    return base;
}

Expr* Parser::parseUnary() {
    ParseDepthGuard dg(parse_depth_, kMaxParseDepth);
    if (!dg.ok) {
        diag_.error(current_.loc, "expression or statement too deeply nested");
        return nullptr;
    }
    SourceLocation loc = current_.loc;

    if (check(TokenKind::PlusPlus)) {
        consume();
        Expr* operand = parseUnary();
        auto* node = arena_.alloc<UnaryExpr>();
        node->loc = loc;
        node->op = UnOpKind::PreInc;
        node->operand = operand;
        node->is_postfix = false;
        return node;
    }
    if (check(TokenKind::MinusMinus)) {
        consume();
        Expr* operand = parseUnary();
        auto* node = arena_.alloc<UnaryExpr>();
        node->loc = loc;
        node->op = UnOpKind::PreDec;
        node->operand = operand;
        node->is_postfix = false;
        return node;
    }
    if (check(TokenKind::Ampersand)) {
        consume();
        Expr* operand = parseUnary();
        auto* node = arena_.alloc<AddrOfExpr>();
        node->loc = loc;
        node->operand = operand;
        return node;
    }
    if (check(TokenKind::Star)) {
        consume();
        Expr* operand = parseUnary();
        auto* node = arena_.alloc<DerefExpr>();
        node->loc = loc;
        node->operand = operand;
        return node;
    }
    if (check(TokenKind::Bang)) {
        consume();
        Expr* operand = parseUnary();
        auto* node = arena_.alloc<UnaryExpr>();
        node->loc = loc;
        node->op = UnOpKind::LogNot;
        node->operand = operand;
        node->is_postfix = false;
        return node;
    }
    if (check(TokenKind::Tilde)) {
        consume();
        Expr* operand = parseUnary();
        auto* node = arena_.alloc<UnaryExpr>();
        node->loc = loc;
        node->op = UnOpKind::BitNot;
        node->operand = operand;
        node->is_postfix = false;
        return node;
    }
    if (check(TokenKind::Minus)) {
        consume();
        Expr* operand = parseUnary();
        auto* node = arena_.alloc<UnaryExpr>();
        node->loc = loc;
        node->op = UnOpKind::Negate;
        node->operand = operand;
        node->is_postfix = false;
        return node;
    }

    return parsePostfix();
}

Expr* Parser::parsePostfix() {
    Expr* expr = parsePrimary();

    for (;;) {
        SourceLocation loc = current_.loc;

        if (check(TokenKind::PlusPlus)) {
            consume();
            auto* node = arena_.alloc<UnaryExpr>();
            node->loc = loc;
            node->op = UnOpKind::PostInc;
            node->operand = expr;
            node->is_postfix = true;
            expr = node;
            continue;
        }
        if (check(TokenKind::MinusMinus)) {
            consume();
            auto* node = arena_.alloc<UnaryExpr>();
            node->loc = loc;
            node->op = UnOpKind::PostDec;
            node->operand = expr;
            node->is_postfix = true;
            expr = node;
            continue;
        }
        if (check(TokenKind::LBracket)) {
            consume();
            Expr* index = parseExpr();
            expect(TokenKind::RBracket);
            auto* node = arena_.alloc<ArrayIndexExpr>();
            node->loc = loc;
            node->base = expr;
            node->index = index;
            expr = node;
            continue;
        }
        if (check(TokenKind::DoubleColon)) {
            // ClassName::Method — ClassName$Method के रूप में mangle करो।
            consume(); // ::
            Token method = expect(TokenKind::Identifier);
            auto* id = dynamic_cast<IdentifierExpr*>(expr);
            if (id) {
                std::string mangled = id->name + "$" + std::string(method.text);
                auto* node = arena_.alloc<IdentifierExpr>();
                node->loc = loc;
                node->name = mangled;
                expr = node;
            }
            continue;
        }
        if (check(TokenKind::Dot)) {
            consume();
            Token field = expect(TokenKind::Identifier);
            auto* node = arena_.alloc<FieldAccessExpr>();
            node->loc = loc;
            node->object = expr;
            node->field = std::string(field.text);
            node->is_arrow = false;
            expr = node;
            continue;
        }
        if (check(TokenKind::Arrow)) {
            consume();
            Token field = expect(TokenKind::Identifier);
            auto* node = arena_.alloc<FieldAccessExpr>();
            node->loc = loc;
            node->object = expr;
            node->field = std::string(field.text);
            node->is_arrow = true;
            expr = node;
            continue;
        }
        if (check(TokenKind::LParen)) {
            // postfix cast expr(Type) और function call expr(...) में अंतर करो।
            Token la1 = peek(1);
            bool la1_is_type = isTypeToken(la1.kind) ||
                (la1.kind == TokenKind::Identifier && known_class_names_.count(la1.text));
            if (la1_is_type) {
                bool looks_like_cast = false;
                Token la2 = peek(2);
                if (la2.kind == TokenKind::RParen) {
                    looks_like_cast = true;
                } else if (la2.kind == TokenKind::Star) {
                    looks_like_cast = true;
                }

                if (looks_like_cast) {
                    consume(); // (
                    Type* ty = parseType();
                    expect(TokenKind::RParen);
                    auto* node = arena_.alloc<PostfixCastExpr>();
                    node->loc = loc;
                    node->expr = expr;
                    node->target_type = ty;
                    expr = node;
                    continue;
                }
            }

            consume(); // (
            auto* call = arena_.alloc<CallExpr>();
            call->loc = loc;
            call->callee = expr;
            if (!check(TokenKind::RParen)) {
                call->args.push_back(parseExpr());
                while (match(TokenKind::Comma)) {
                    call->args.push_back(parseExpr());
                }
            }
            expect(TokenKind::RParen);
            expr = call;
            continue;
        }
        break;
    }
    return expr;
}

Expr* Parser::parsePrimary() {
    ParseDepthGuard dg(parse_depth_, kMaxParseDepth);
    if (!dg.ok) {
        diag_.error(current_.loc, "expression or statement too deeply nested");
        auto* dummy = arena_.alloc<IntLiteralExpr>();
        dummy->loc = current_.loc;
        dummy->value = 0;
        return dummy;
    }
    SourceLocation loc = current_.loc;

    if (check(TokenKind::IntLiteral)) {
        Token t = consume();
        auto* node = arena_.alloc<IntLiteralExpr>();
        node->loc = loc;
        node->value = static_cast<uint64_t>(t.intVal);
        node->type_hint = PrimKind::I64;
        return node;
    }

    if (check(TokenKind::FloatLiteral)) {
        Token t = consume();
        auto* node = arena_.alloc<FloatLiteralExpr>();
        node->loc = loc;
        node->value = t.floatVal;
        return node;
    }

    if (check(TokenKind::StringLiteral)) {
        Token t = consume();
        auto* node = arena_.alloc<StringLiteralExpr>();
        node->loc = loc;
        if (t.text.size() >= 2)
            node->value = decodeStringEscapes(t.text.substr(1, t.text.size() - 2));
        else
            node->value = std::string(t.text);
        while (check(TokenKind::StringLiteral)) {
            Token t2 = consume();
            if (t2.text.size() >= 2)
                node->value += decodeStringEscapes(t2.text.substr(1, t2.text.size() - 2));
            else
                node->value += t2.text;
        }
        return node;
    }

    if (check(TokenKind::CharLiteral)) {
        Token t = consume();
        auto* node = arena_.alloc<CharLiteralExpr>();
        node->loc = loc;
        node->value = static_cast<uint64_t>(t.intVal);
        node->byte_count = 1;
        if (t.text.size() > 3)
            node->byte_count = static_cast<int>(t.text.size() - 2);
        return node;
    }

    if (check(TokenKind::True)) {
        consume();
        auto* node = arena_.alloc<BoolLiteralExpr>();
        node->loc = loc;
        node->value = true;
        return node;
    }
    if (check(TokenKind::False)) {
        consume();
        auto* node = arena_.alloc<BoolLiteralExpr>();
        node->loc = loc;
        node->value = false;
        return node;
    }

    if (check(TokenKind::Identifier)) {
        Token t = consume();
        auto* node = arena_.alloc<IdentifierExpr>();
        node->loc = loc;
        node->name = std::string(t.text);
        return node;
    }

    if (check(TokenKind::LParen)) {
        Token la1 = peek(1);
        bool la1_is_type2 = isTypeToken(la1.kind) ||
            (la1.kind == TokenKind::Identifier && known_class_names_.count(la1.text));
        if (la1_is_type2) {
            Token la2 = peek(2);
            bool looks_like_cast = la2.kind == TokenKind::RParen ||
                                   la2.kind == TokenKind::Star;
            if (looks_like_cast) {
                consume(); // (
                Type* ty = parseType();
                expect(TokenKind::RParen);
                Expr* operand = parseUnary();
                auto* node = arena_.alloc<PostfixCastExpr>();
                node->loc = loc;
                node->expr = operand;
                node->target_type = ty;
                return node;
            }
        }

        consume(); // (
        Expr* inner = parseExpr();
        expect(TokenKind::RParen);
        return inner;
    }

    if (check(TokenKind::Sizeof)) {
        consume();
        auto* node = arena_.alloc<SizeofExpr>();
        node->loc = loc;
        expect(TokenKind::LParen);
        if (isTypeStart()) {
            node->target_type = parseType();
        } else {
            node->target_expr = parseExpr();
        }
        expect(TokenKind::RParen);
        return node;
    }

    if (check(TokenKind::Offset)) {
        consume();
        auto* node = arena_.alloc<OffsetExpr>();
        node->loc = loc;
        expect(TokenKind::LParen);
        if (check(TokenKind::Lastclass)) {
            consume();
            node->class_name = "lastclass";
        } else {
            Token cls = expect(TokenKind::Identifier);
            node->class_name = std::string(cls.text);
        }
        expect(TokenKind::Comma);
        Token mem = expect(TokenKind::Identifier);
        node->member_name = std::string(mem.text);
        expect(TokenKind::RParen);
        return node;
    }

    if (check(TokenKind::Lastclass)) {
        consume();
        auto* node = arena_.alloc<IdentifierExpr>();
        node->loc = loc;
        node->name = "lastclass";
        return node;
    }

    if (check(TokenKind::Throw)) {
        consume();
        auto* node = arena_.alloc<ThrowExpr>();
        node->loc = loc;
        if (!check(TokenKind::Semicolon) && !check(TokenKind::RParen)) {
            node->code = parseExpr();
        }
        return node;
    }

    diag_.error(loc, std::string("unexpected token '") +
                tokenKindToString(current_.kind) + "' in expression");
    consume();
    auto* dummy = arena_.alloc<IntLiteralExpr>();
    dummy->loc = loc;
    dummy->value = 0;
    return dummy;
}

Stmt* Parser::parseStmt() {
    ParseDepthGuard dg(parse_depth_, kMaxParseDepth);
    if (!dg.ok) {
        diag_.error(current_.loc, "expression or statement too deeply nested");
        return nullptr;
    }
    SourceLocation loc = current_.loc;

    // "fmt", arg1, arg2; string-output statement।
    if (check(TokenKind::StringLiteral)) {
        Token la1 = peek(1);
        if (la1.kind == TokenKind::Comma || la1.kind == TokenKind::Semicolon ||
            la1.kind == TokenKind::StringLiteral) {
            auto* node = arena_.alloc<StringOutputStmt>();
            node->loc = loc;
            node->format = static_cast<StringLiteralExpr*>(parsePrimary());
            while (match(TokenKind::Comma)) {
                node->args.push_back(parseExpr());
            }
            expect(TokenKind::Semicolon);
            return node;
        }
    }

    if (check(TokenKind::LBrace))
        return parseCompoundStmt();

    if (check(TokenKind::If))
        return parseIfStmt();

    if (check(TokenKind::For))
        return parseForStmt();

    if (check(TokenKind::While))
        return parseWhileStmt();

    if (check(TokenKind::Do))
        return parseDoWhileStmt();

    if (check(TokenKind::Switch))
        return parseSwitchStmt();

    // lock {} body plain compound statement की तरह run होती है।
    if (check(TokenKind::Lock)) {
        consume();
        return parseCompoundStmt();
    }

    if (check(TokenKind::Return))
        return parseReturnStmt();

    if (check(TokenKind::Goto))
        return parseGotoStmt();

    if (check(TokenKind::Break)) {
        consume();
        expect(TokenKind::Semicolon);
        auto* node = arena_.alloc<BreakStmt>();
        node->loc = loc;
        return node;
    }

    if (check(TokenKind::Continue)) {
        consume();
        expect(TokenKind::Semicolon);
        auto* node = arena_.alloc<ContinueStmt>();
        node->loc = loc;
        return node;
    }

    if (check(TokenKind::Try))
        return parseTryCatchStmt();

    if (check(TokenKind::Asm))
        return parseAsmStmt();

    if (check(TokenKind::Exe))
        return parseExeStmt();

    if (check(TokenKind::Identifier)) {
        Token la1 = peek(1);
        if (la1.kind == TokenKind::Colon) {
            Token name = consume();
            consume(); // :
            auto* label = arena_.alloc<LabelStmt>();
            label->loc = loc;
            label->name = std::string(name.text);
            if (check(TokenKind::RBrace) || check(TokenKind::Eof)) {
                label->stmt = nullptr;
            } else {
                label->stmt = parseStmt();
            }
            return label;
        }
        // "ClassName::Method(...)" qualified call है, asm label नहीं।
        if (la1.kind == TokenKind::DoubleColon) {
            Token la2 = peek(2);
            if (la2.kind == TokenKind::Identifier) {
                // expression parsing पर fall through करो
            } else {
                Token name = consume();
                consume(); // ::
                auto* label = arena_.alloc<LabelStmt>();
                label->loc = loc;
                label->name = std::string(name.text);
                if (check(TokenKind::RBrace) || check(TokenKind::Eof)) {
                label->stmt = nullptr;
            } else {
                label->stmt = parseStmt();
            }
                return label;
            }
        }
    }

    if (isDeclaration()) {
        Decl* d = parseDecl();
        if (d && d->nk == NodeKind::CompoundDecl) {
            auto* cd = static_cast<CompoundDecl*>(d);
            auto* block = arena_.alloc<CompoundStmt>();
            block->loc = loc;
            for (auto* vd : cd->decls) {
                auto* ds = arena_.alloc<DeclStmt>();
                ds->loc = vd->loc;
                ds->decl = vd;
                block->stmts.push_back(ds);
            }
            return block;
        }
        auto* ds = arena_.alloc<DeclStmt>();
        ds->loc = loc;
        ds->decl = d;
        return ds;
    }

    Expr* expr = parseExpr();
    expect(TokenKind::Semicolon);
    auto* es = arena_.alloc<ExprStmt>();
    es->loc = loc;
    es->expr = expr;
    return es;
}

CompoundStmt* Parser::parseCompoundStmt() {
    SourceLocation loc = current_.loc;
    expect(TokenKind::LBrace);
    auto* block = arena_.alloc<CompoundStmt>();
    block->loc = loc;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        Stmt* s = parseStmt();
        if (s) block->stmts.push_back(s);
        else synchronize();
    }
    expect(TokenKind::RBrace);
    return block;
}

Stmt* Parser::parseIfStmt() {
    SourceLocation loc = current_.loc;
    consume(); // if keyword consume करो
    expect(TokenKind::LParen);
    Expr* cond = parseExpr();
    expect(TokenKind::RParen);
    Stmt* then_body = parseStmt();
    Stmt* else_body = nullptr;
    if (match(TokenKind::Else)) {
        else_body = parseStmt();
    }
    auto* node = arena_.alloc<IfStmt>();
    node->loc = loc;
    node->cond = cond;
    node->then_body = then_body;
    node->else_body = else_body;
    return node;
}

Stmt* Parser::parseForStmt() {
    SourceLocation loc = current_.loc;
    consume(); // for keyword consume करो
    expect(TokenKind::LParen);

    Stmt* init = nullptr;
    if (!check(TokenKind::Semicolon)) {
        if (isDeclaration()) {
            init = arena_.alloc<DeclStmt>();
            static_cast<DeclStmt*>(init)->decl = parseDecl();
            init->loc = loc;
        } else {
            Expr* e = parseExpr();
            expect(TokenKind::Semicolon);
            auto* es = arena_.alloc<ExprStmt>();
            es->loc = loc;
            es->expr = e;
            init = es;
        }
    } else {
        consume(); // ;
    }

    Expr* cond = nullptr;
    if (!check(TokenKind::Semicolon))
        cond = parseExpr();
    expect(TokenKind::Semicolon);

    Expr* post = nullptr;
    if (!check(TokenKind::RParen))
        post = parseExpr();
    expect(TokenKind::RParen);

    Stmt* body = parseStmt();

    auto* node = arena_.alloc<ForStmt>();
    node->loc = loc;
    node->init = init;
    node->cond = cond;
    node->post = post;
    node->body = body;
    return node;
}

Stmt* Parser::parseWhileStmt() {
    SourceLocation loc = current_.loc;
    consume(); // while keyword consume करो
    expect(TokenKind::LParen);
    Expr* cond = parseExpr();
    expect(TokenKind::RParen);
    Stmt* body = parseStmt();
    auto* node = arena_.alloc<WhileStmt>();
    node->loc = loc;
    node->cond = cond;
    node->body = body;
    return node;
}

Stmt* Parser::parseDoWhileStmt() {
    SourceLocation loc = current_.loc;
    consume(); // do keyword consume करो
    Stmt* body = parseStmt();
    expect(TokenKind::While);
    expect(TokenKind::LParen);
    Expr* cond = parseExpr();
    expect(TokenKind::RParen);
    expect(TokenKind::Semicolon);
    auto* node = arena_.alloc<DoWhileStmt>();
    node->loc = loc;
    node->body = body;
    node->cond = cond;
    return node;
}

Stmt* Parser::parseSwitchStmt() {
    SourceLocation loc = current_.loc;
    consume(); // switch keyword consume करो
    expect(TokenKind::LParen);
    Expr* expr = parseExpr();
    expect(TokenKind::RParen);
    expect(TokenKind::LBrace);

    auto* sw = arena_.alloc<SwitchStmt>();
    sw->loc = loc;
    sw->expr = expr;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        if (check(TokenKind::Case) || check(TokenKind::Default)) {
            auto* cs = arena_.alloc<CaseStmt>();
            cs->loc = current_.loc;
            if (match(TokenKind::Case)) {
                cs->value = parseExpr();
                // case N..M: range syntax — range case statement (range वाला case)
                if (check(TokenKind::DotDot)) {
                    consume();
                    cs->range_end = parseExpr();
                }
            } else {
                consume(); // default keyword consume करो
                cs->value = nullptr;
            }
            expect(TokenKind::Colon);
            while (!check(TokenKind::Case) && !check(TokenKind::Default) &&
                   !check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                cs->stmts.push_back(parseStmt());
            }
            sw->cases.push_back(cs);
        } else {
            diag_.error(current_.loc, "expected 'case' or 'default' in switch");
            synchronize();
        }
    }
    expect(TokenKind::RBrace);
    return sw;
}

Stmt* Parser::parseReturnStmt() {
    SourceLocation loc = current_.loc;
    consume(); // return keyword consume करो
    auto* node = arena_.alloc<ReturnStmt>();
    node->loc = loc;
    if (!check(TokenKind::Semicolon)) {
        node->value = parseExpr();
    }
    expect(TokenKind::Semicolon);
    return node;
}

Stmt* Parser::parseGotoStmt() {
    SourceLocation loc = current_.loc;
    consume(); // goto keyword consume करो
    Token label = expect(TokenKind::Identifier);
    expect(TokenKind::Semicolon);
    auto* node = arena_.alloc<GotoStmt>();
    node->loc = loc;
    node->label = std::string(label.text);
    return node;
}

Stmt* Parser::parseTryCatchStmt() {
    SourceLocation loc = current_.loc;
    consume(); // try keyword consume करो
    CompoundStmt* try_body = parseCompoundStmt();
    expect(TokenKind::Catch);
    CompoundStmt* catch_body = parseCompoundStmt();
    auto* node = arena_.alloc<TryCatchStmt>();
    node->loc = loc;
    node->try_body = try_body;
    node->catch_body = catch_body;
    return node;
}

Stmt* Parser::parseAsmStmt() {
    SourceLocation loc = current_.loc;
    consume(); // asm keyword consume करो
    expect(TokenKind::LBrace);
    auto* node = arena_.alloc<AsmStmt>();
    node->loc = loc;

    // Extended: asm { "template" : outputs : inputs : clobbers } — GCC-style extended asm (विस्तारित assembly)
    // Simple:   asm { raw token text } — सीधा raw assembly text
    if (check(TokenKind::StringLiteral)) {
        std::string asm_text(current_.text);
        if (asm_text.size() >= 2 && asm_text.front() == '"' && asm_text.back() == '"')
            asm_text = asm_text.substr(1, asm_text.size() - 2);
        node->raw_text = asm_text;
        consume();

        auto parseConstraintSection = [&](std::vector<AsmConstraint>& list) {
            while (!check(TokenKind::Colon) && !check(TokenKind::RBrace)
                   && !check(TokenKind::Eof)) {
                if (check(TokenKind::StringLiteral)) {
                    AsmConstraint ac;
                    std::string cstr(current_.text);
                    if (cstr.size() >= 2 && cstr.front() == '"' && cstr.back() == '"')
                        cstr = cstr.substr(1, cstr.size() - 2);
                    ac.constraint = cstr;
                    consume();
                    if (check(TokenKind::LParen)) {
                        consume(); // '('
                        ac.expr = parseExpr();
                        expect(TokenKind::RParen);
                    }
                    list.push_back(std::move(ac));
                    if (check(TokenKind::Comma)) consume();
                } else {
                    consume(); // unexpected tokens skip करो
                }
            }
        };

        if (check(TokenKind::Colon)) {
            consume();
            parseConstraintSection(node->outputs);
        }

        if (check(TokenKind::Colon)) {
            consume();
            parseConstraintSection(node->inputs);
        }

        if (check(TokenKind::Colon)) {
            consume();
            while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                if (check(TokenKind::StringLiteral)) {
                    std::string clob(current_.text);
                    if (clob.size() >= 2 && clob.front() == '"' && clob.back() == '"')
                        clob = clob.substr(1, clob.size() - 2);
                    if (!clob.empty()) node->clobbers.push_back(clob);
                    consume();
                }
                if (check(TokenKind::Comma)) consume();
                else break;
            }
        }

        expect(TokenKind::RBrace);
    } else {
        int depth = 1;
        std::string raw;
        while (depth > 0 && !check(TokenKind::Eof)) {
            if (check(TokenKind::LBrace)) {
                ++depth; raw += "{"; consume();
            } else if (check(TokenKind::RBrace)) {
                --depth;
                if (depth > 0) { raw += "}"; consume(); }
            } else {
                raw += std::string(current_.text);
                raw += " ";
                consume();
            }
        }
        expect(TokenKind::RBrace);
        node->raw_text = raw;
    }

    return node;
}

Stmt* Parser::parseExeStmt() {
    SourceLocation loc = current_.loc;
    consume(); // exe keyword consume करो
    CompoundStmt* body = parseCompoundStmt();

    executeExeBlock(body);
    auto* node = arena_.alloc<ExeBlockStmt>();
    node->loc = loc;
    node->body = body;
    return node;
}

void Parser::executeExeBlock(CompoundStmt* body) {
    std::string emitted = exe_interp_.runExeBlock(body);
    if (emitted.empty()) return;

    SourceManager& sm = pp_.sourceManager();
    Diagnostics& diag = pp_.diagnostics();
    int fid = sm.loadString("<exe-emit>", emitted);

    Lexer lexer(sm, fid, diag);
    std::deque<Token> tokens;
    while (true) {
        Token tok = lexer.next();
        if (tok.kind == TokenKind::Eof) break;
        tokens.push_back(tok);
    }
    if (tokens.empty()) return;

    // Current token और lookaheads को injected stream में वापस push करो।
    std::deque<Token> all_tokens = tokens;
    all_tokens.push_back(current_);
    for (int i = 0; i < la_count_; ++i)
        all_tokens.push_back(lookahead_[i]);
    la_count_ = 0;

    pp_.injectTokens(all_tokens);
    current_ = pp_.next();
}

Decl* Parser::parseDecl() {
    SourceLocation loc = current_.loc;

    Storage storage = Storage::Default;
    Linkage linkage = Linkage::None;
    FuncFlags flags{};
    bool no_warn = false;

    bool parsing_modifiers = true;
    while (parsing_modifiers) {
        switch (current_.kind) {
        case TokenKind::Public:
            consume(); linkage = Linkage::Public; break;
        case TokenKind::Extern:
            consume(); linkage = Linkage::Extern; break;
        case TokenKind::_Extern:
            consume(); linkage = Linkage::ExternC; break;
        case TokenKind::_Intern:
            consume(); linkage = Linkage::Intern; break;
        case TokenKind::Import:
        case TokenKind::_Import:
            consume(); linkage = Linkage::Import; break;
        case TokenKind::Static:
            consume(); storage = Storage::Static; break;
        case TokenKind::Reg:
            consume(); storage = Storage::Reg; break;
        case TokenKind::NoReg:
            consume(); storage = Storage::NoReg; break;
        case TokenKind::NoWarn:
            consume(); no_warn = true; break;
        case TokenKind::Interrupt:
            consume(); flags.interrupt = true; break;
        case TokenKind::HasErrCode:
            consume(); flags.haserrcode = true; break;
        case TokenKind::ArgPop:
            consume(); flags.argpop = true; break;
        case TokenKind::NoArgPop:
            consume(); flags.noargpop = true; break;
        default:
            parsing_modifiers = false; break;
        }
    }

    if (check(TokenKind::Class))
        return parseClassDecl();

    if (check(TokenKind::Union))
        return parseUnionDecl();

    if (check(TokenKind::Enum))
        return parseEnumDecl();

    if (check(TokenKind::Typedef)) {
        consume();
        Type* ty = parseType();
        Token name = expect(TokenKind::Identifier);
        expect(TokenKind::Semicolon);
        auto* td = arena_.alloc<TypedefDecl>();
        td->loc = loc;
        td->type = ty;
        td->name = std::string(name.text);
        return td;
    }

    // 'auto' को I64 माना जाता है।
    Type* type = nullptr;
    if (check(TokenKind::Auto)) {
        consume();
        type = arena_.alloc<Type>();
        type->kind = Type::Prim;
        type->data = PrimitiveType{PrimKind::I64};
    } else {
        type = parseType();
    }

    // Function pointer: RetType (*name)(ParamTypes...) — C-style function pointer declaration (function का pointer)
    if (check(TokenKind::LParen) &&
        peek(1).kind == TokenKind::Star &&
        peek(2).kind == TokenKind::Identifier &&
        peek(3).kind == TokenKind::RParen) {
        consume(); // (
        consume(); // *
        Token fpName = consume(); // variable name — function pointer का नाम
        consume(); // )

        std::vector<Type*> paramTypes;
        bool is_vararg = false;
        if (check(TokenKind::LParen)) {
            consume(); // (
            while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
                if (check(TokenKind::DotDot)) {
                    consume();
                    is_vararg = true;
                    break;
                }
                Type* pt = parseType();
                // Optional parameter name consume करो
                if (check(TokenKind::Identifier))
                    consume();
                paramTypes.push_back(pt);
                if (!match(TokenKind::Comma))
                    break;
            }
            expect(TokenKind::RParen);
        }

        auto* ft = arena_.alloc<Type>();
        ft->kind = Type::Func;
        FuncType ftd;
        ftd.return_type = type;
        ftd.param_types = paramTypes;
        ftd.is_vararg = is_vararg;
        ft->data = std::move(ftd);

        auto* ptrTy = arena_.alloc<Type>();
        ptrTy->kind = Type::Pointer;
        ptrTy->data = PointerType{ft, 1};

        auto* var = arena_.alloc<VarDecl>();
        var->loc = loc;
        var->type = ptrTy;
        var->name = std::string(fpName.text);
        var->storage = storage;
        var->no_warn = no_warn;

        if (match(TokenKind::Assign)) {
            var->init = parseExpr();
        }
        expect(TokenKind::Semicolon);
        return var;
    }

    Token name = expect(TokenKind::Identifier);
    return parseVarOrFuncDecl(type, std::string(name.text), storage, linkage, flags, no_warn);
}

Decl* Parser::parseVarOrFuncDecl(Type* type, const std::string& name, Storage storage,
                                  Linkage linkage, FuncFlags flags, bool no_warn) {
    SourceLocation loc = current_.loc;

    if (check(TokenKind::LParen)) {
        return parseFuncDecl(type, name, linkage, flags);
    }

    auto* var = arena_.alloc<VarDecl>();
    var->loc = loc;
    var->type = type;
    var->name = name;
    var->storage = storage;
    var->no_warn = no_warn;

    if (match(TokenKind::Assign)) {
        if (check(TokenKind::LBrace)) {
            consume(); // {
            auto* il = arena_.alloc<InitListExpr>();
            il->loc = current_.loc;
            if (!check(TokenKind::RBrace)) {
                il->values.push_back(parseExpr());
                while (match(TokenKind::Comma) && !check(TokenKind::RBrace)) {
                    il->values.push_back(parseExpr());
                }
            }
            expect(TokenKind::RBrace);
            var->init = il;
        } else {
            var->init = parseExpr();
        }
    }

    if (check(TokenKind::LBracket)) {
        std::vector<int> dims;
        while (check(TokenKind::LBracket)) {
            consume();
            int size = -1;
            if (check(TokenKind::IntLiteral)) {
                size = static_cast<int>(current_.intVal);
                consume();
            }
            expect(TokenKind::RBracket);
            dims.push_back(size);
        }
        // Nested ArrayType right-to-left बनाओ ताकि m[2][3] → Array{Array{I64,3},2} हो।
        Type* arrType = type;
        for (int i = static_cast<int>(dims.size()) - 1; i >= 0; --i) {
            Type* arr = arena_.alloc<Type>();
            arr->kind = Type::Array;
            arr->data = ArrayType{arrType, dims[i]};
            arrType = arr;
        }
        var->type = arrType;

        if (match(TokenKind::Assign)) {
            if (check(TokenKind::LBrace)) {
                consume(); // {
                auto* il = arena_.alloc<InitListExpr>();
                il->loc = current_.loc;
                if (!check(TokenKind::RBrace)) {
                    il->values.push_back(parseExpr());
                    while (match(TokenKind::Comma) && !check(TokenKind::RBrace)) {
                        il->values.push_back(parseExpr());
                    }
                }
                expect(TokenKind::RBrace);
                var->init = il;
            } else if (check(TokenKind::StringLiteral)) {
                var->init = parsePrimary();
            } else {
                var->init = parseExpr();
            }
        }
    }

    std::vector<VarDecl*> siblings;
    while (check(TokenKind::Comma)) {
        consume(); // ,
        Token n2 = expect(TokenKind::Identifier);
        auto* v2 = arena_.alloc<VarDecl>();
        v2->loc = current_.loc;
        v2->type = type;
        v2->name = std::string(n2.text);
        v2->storage = storage;
        v2->no_warn = no_warn;
        if (match(TokenKind::Assign)) {
            v2->init = parseExpr();
        }
        siblings.push_back(v2);
    }

    expect(TokenKind::Semicolon);

    if (siblings.empty()) return var;

    auto* cd = arena_.alloc<CompoundDecl>();
    cd->loc = var->loc;
    cd->decls.push_back(var);
    for (auto* s : siblings) cd->decls.push_back(s);
    return cd;
}

FuncDecl* Parser::parseFuncDecl(Type* return_type, const std::string& name,
                                 Linkage linkage, FuncFlags flags) {
    SourceLocation loc = current_.loc;
    consume(); // (

    auto* func = arena_.alloc<FuncDecl>();
    func->loc = loc;
    func->return_type = return_type;
    func->name = name;
    func->linkage = linkage;
    func->flags = flags;
    func->is_vararg = false;

    if (!check(TokenKind::RParen)) {
        for (;;) {
            if (check(TokenKind::DotDot)) {
                consume();
                func->is_vararg = true;
                break;
            }
            auto* param = arena_.alloc<ParamDecl>();
            param->loc = current_.loc;
            param->type = parseType();
            if (check(TokenKind::Identifier)) {
                Token pname = consume();
                param->name = std::string(pname.text);
            }
            if (match(TokenKind::Assign)) {
                param->default_value = parseExpr();
            }
            func->params.push_back(param);

            if (!match(TokenKind::Comma))
                break;
        }
    }
    expect(TokenKind::RParen);

    if (check(TokenKind::LBrace)) {
        func->body = parseCompoundStmt();
    } else {
        expect(TokenKind::Semicolon);
        func->body = nullptr;
    }
    return func;
}

ClassDecl* Parser::parseClassDecl() {
    SourceLocation loc = current_.loc;
    consume(); // class keyword consume करो
    Token name = expect(TokenKind::Identifier);

    auto* cls = arena_.alloc<ClassDecl>();
    cls->loc = loc;
    cls->name = std::string(name.text);
    known_class_names_.insert(cls->name);

    if (match(TokenKind::Colon)) {
        Token base = expect(TokenKind::Identifier);
        cls->base_name = std::string(base.text);
    }

    if (check(TokenKind::Semicolon)) {
        consume();
        return cls;
    }

    int anonUnionGroupCounter = 0;

    expect(TokenKind::LBrace);
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        bool is_anon_union = check(TokenKind::Union) && peek(1).kind == TokenKind::LBrace;
        bool is_anon_class = check(TokenKind::Class) && peek(1).kind == TokenKind::LBrace;
        if (is_anon_union || is_anon_class) {
            consume(); // 'union' या 'class'
            expect(TokenKind::LBrace);
            int group_id = is_anon_union ? anonUnionGroupCounter++ : -1;
            while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                if (isTypeStart() || isTypeToken(current_.kind)) {
                    Type* ftype = parseType();
                    Token fname = expect(TokenKind::Identifier);

                    auto* field = arena_.alloc<FieldDecl>();
                    field->loc = current_.loc;
                    field->type = ftype;
                    field->name = std::string(fname.text);
                    field->bit_width = -1;
                    if (is_anon_union) {
                        field->is_union_member = true;
                        field->union_group = group_id;
                    }

                    if (check(TokenKind::LBracket)) {
                        consume();
                        int size = -1;
                        if (check(TokenKind::IntLiteral)) {
                            size = static_cast<int>(current_.intVal);
                            consume();
                        }
                        expect(TokenKind::RBracket);
                        Type* arr = arena_.alloc<Type>();
                        arr->kind = Type::Array;
                        arr->data = ArrayType{ftype, size};
                        field->type = arr;
                    }

                    expect(TokenKind::Semicolon);
                    cls->members.push_back(field);
                } else {
                    diag_.error(current_.loc, "expected field declaration in anonymous union/struct");
                    synchronize();
                }
            }
            expect(TokenKind::RBrace);
            match(TokenKind::Semicolon);
            continue;
        }

        if (check(TokenKind::Public)) { consume(); }

        if (isTypeStart() || isTypeToken(current_.kind)) {
            Type* ftype = parseType();
            Token fname = expect(TokenKind::Identifier);

            if (check(TokenKind::LParen)) {
                cls->members.push_back(parseFuncDecl(ftype, std::string(fname.text),
                                                      Linkage::None, FuncFlags{}));
                continue;
            }

            auto* field = arena_.alloc<FieldDecl>();
            field->loc = current_.loc;
            field->type = ftype;
            field->name = std::string(fname.text);
            field->bit_width = -1;

            if (match(TokenKind::Colon)) {
                Token width = expect(TokenKind::IntLiteral);
                field->bit_width = static_cast<int>(width.intVal);
            }

            if (check(TokenKind::LBracket)) {
                consume();
                int size = -1;
                if (check(TokenKind::IntLiteral)) {
                    size = static_cast<int>(current_.intVal);
                    consume();
                }
                expect(TokenKind::RBracket);
                Type* arr = arena_.alloc<Type>();
                arr->kind = Type::Array;
                arr->data = ArrayType{ftype, size};
                field->type = arr;
            }

            expect(TokenKind::Semicolon);
            cls->members.push_back(field);
        } else {
            diag_.error(current_.loc, "expected field declaration in class");
            synchronize();
        }
    }
    expect(TokenKind::RBrace);
    match(TokenKind::Semicolon); // optional trailing ; — class definition के बाद
    return cls;
}

EnumDecl* Parser::parseEnumDecl() {
    SourceLocation loc = current_.loc;
    consume(); // enum keyword consume करो

    auto* ed = arena_.alloc<EnumDecl>();
    ed->loc = loc;

    if (check(TokenKind::Identifier)) {
        Token name = consume();
        ed->name = std::string(name.text);
    }

    expect(TokenKind::LBrace);
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        Token mname = expect(TokenKind::Identifier);
        EnumMember member;
        member.name = std::string(mname.text);
        member.value = nullptr;
        if (match(TokenKind::Assign)) {
            member.value = parseExpr();
        }
        ed->members.push_back(member);
        if (!match(TokenKind::Comma))
            break;
    }
    expect(TokenKind::RBrace);
    match(TokenKind::Semicolon);
    return ed;
}

UnionDecl* Parser::parseUnionDecl() {
    SourceLocation loc = current_.loc;
    consume(); // union keyword consume करो
    Token name = expect(TokenKind::Identifier);

    auto* u = arena_.alloc<UnionDecl>();
    u->loc = loc;
    u->name = std::string(name.text);

    expect(TokenKind::LBrace);
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        Type* ftype = parseType();
        Token fname = expect(TokenKind::Identifier);

        auto* field = arena_.alloc<FieldDecl>();
        field->loc = current_.loc;
        field->type = ftype;
        field->name = std::string(fname.text);
        field->bit_width = -1;

        // Array suffix — union field पर array size syntax
        if (check(TokenKind::LBracket)) {
            consume();
            int size = -1;
            if (check(TokenKind::IntLiteral)) {
                size = static_cast<int>(current_.intVal);
                consume();
            }
            expect(TokenKind::RBracket);
            Type* arr = arena_.alloc<Type>();
            arr->kind = Type::Array;
            arr->data = ArrayType{ftype, size};
            field->type = arr;
        }

        expect(TokenKind::Semicolon);
        u->members.push_back(field);
    }
    expect(TokenKind::RBrace);
    match(TokenKind::Semicolon);
    return u;
}

TranslationUnit* Parser::parse() {
    auto* tu = arena_.alloc<TranslationUnit>();
    while (!check(TokenKind::Eof)) {
        if (isDeclaration()) {
            Decl* d = parseDecl();
            if (d && d->nk == NodeKind::CompoundDecl) {
                auto* cd = static_cast<CompoundDecl*>(d);
                for (auto* vd : cd->decls)
                    tu->decls.push_back(vd);
            } else {
                tu->decls.push_back(d);
            }
        } else {
            Stmt* s = parseStmt();
            tu->decls.push_back(s);
        }
    }
    return tu;
}

} // namespace holyc

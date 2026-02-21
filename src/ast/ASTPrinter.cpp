#include "ast/ASTPrinter.h"
#include "ast/TranslationUnit.h"
#include <cassert>

namespace holyc {

/**
 * @brief Stream पर `indent * 2` spaces output करो।
 *
 * @param indent Current indentation depth।
 */
void ASTPrinter::printIndent(int indent) {
    for (int i = 0; i < indent; ++i) os_ << "  ";
}

/**
 * @brief Node के NodeKind के हिसाब से printExpr, printStmt, या printDecl पर dispatch करो।
 *
 * @param node   Print करने वाला AST node (nullptr हो तो no-op)।
 * @param indent Current indentation depth।
 */
void ASTPrinter::print(Node* node, int indent) {
    if (!node) return;

    // NodeKind ranges से पता करो कि Expr है, Stmt है, या Decl
    switch (node->nk) {
    // Expressions — सभी expression node kinds
    case NodeKind::IntLiteralExpr: case NodeKind::FloatLiteralExpr:
    case NodeKind::StringLiteralExpr: case NodeKind::CharLiteralExpr:
    case NodeKind::BoolLiteralExpr: case NodeKind::IdentifierExpr:
    case NodeKind::BinaryExpr: case NodeKind::UnaryExpr:
    case NodeKind::TernaryExpr: case NodeKind::ChainedCmpExpr:
    case NodeKind::PowerExpr: case NodeKind::CallExpr:
    case NodeKind::PostfixCastExpr: case NodeKind::SizeofExpr:
    case NodeKind::OffsetExpr: case NodeKind::ArrayIndexExpr:
    case NodeKind::FieldAccessExpr: case NodeKind::AddrOfExpr:
    case NodeKind::DerefExpr: case NodeKind::ThrowExpr:
        printExpr(static_cast<Expr*>(node), indent);
        break;
    // Statements — सभी statement node kinds
    case NodeKind::CompoundStmt: case NodeKind::DeclStmt:
    case NodeKind::ExprStmt: case NodeKind::IfStmt:
    case NodeKind::ForStmt: case NodeKind::WhileStmt:
    case NodeKind::DoWhileStmt: case NodeKind::SwitchStmt:
    case NodeKind::CaseStmt: case NodeKind::BreakStmt:
    case NodeKind::ContinueStmt: case NodeKind::ReturnStmt:
    case NodeKind::GotoStmt: case NodeKind::LabelStmt:
    case NodeKind::AsmStmt: case NodeKind::TryCatchStmt:
    case NodeKind::ExeBlockStmt: case NodeKind::StringOutputStmt:
        printStmt(static_cast<Stmt*>(node), indent);
        break;
    // Declarations — सभी declaration node kinds
    case NodeKind::VarDecl: case NodeKind::FuncDecl:
    case NodeKind::ParamDecl: case NodeKind::ClassDecl:
    case NodeKind::UnionDecl: case NodeKind::FieldDecl:
    case NodeKind::TypedefDecl: case NodeKind::ExternDecl:
        printDecl(static_cast<Decl*>(node), indent);
        break;
    }
}

/**
 * @brief Single expression node को उसके children के साथ print करो।
 *
 * @param e      Print करने वाला expression node।
 * @param indent Current indentation depth।
 */
void ASTPrinter::printExpr(Expr* e, int indent) {
    printIndent(indent);
    switch (e->nk) {
    case NodeKind::IntLiteralExpr: {
        auto* n = static_cast<IntLiteralExpr*>(e);
        os_ << "IntLiteralExpr " << n->value << "\n";
        break;
    }
    case NodeKind::FloatLiteralExpr: {
        auto* n = static_cast<FloatLiteralExpr*>(e);
        os_ << "FloatLiteralExpr " << n->value << "\n";
        break;
    }
    case NodeKind::StringLiteralExpr: {
        auto* n = static_cast<StringLiteralExpr*>(e);
        os_ << "StringLiteralExpr \"" << n->value << "\"\n";
        break;
    }
    case NodeKind::CharLiteralExpr: {
        auto* n = static_cast<CharLiteralExpr*>(e);
        os_ << "CharLiteralExpr " << n->value << " (" << n->byte_count << " bytes)\n";
        break;
    }
    case NodeKind::BoolLiteralExpr: {
        auto* n = static_cast<BoolLiteralExpr*>(e);
        os_ << "BoolLiteralExpr " << (n->value ? "TRUE" : "FALSE") << "\n";
        break;
    }
    case NodeKind::IdentifierExpr: {
        auto* n = static_cast<IdentifierExpr*>(e);
        os_ << "IdentifierExpr '" << n->name << "'\n";
        break;
    }
    case NodeKind::BinaryExpr: {
        auto* n = static_cast<BinaryExpr*>(e);
        os_ << "BinaryExpr '" << binOpKindToString(n->op) << "'\n";
        print(n->lhs, indent + 1);
        print(n->rhs, indent + 1);
        break;
    }
    case NodeKind::UnaryExpr: {
        auto* n = static_cast<UnaryExpr*>(e);
        os_ << "UnaryExpr '" << unOpKindToString(n->op) << "'"
            << (n->is_postfix ? " postfix" : " prefix") << "\n";
        print(n->operand, indent + 1);
        break;
    }
    case NodeKind::TernaryExpr: {
        auto* n = static_cast<TernaryExpr*>(e);
        os_ << "TernaryExpr\n";
        print(n->cond, indent + 1);
        print(n->then_expr, indent + 1);
        print(n->else_expr, indent + 1);
        break;
    }
    case NodeKind::ChainedCmpExpr: {
        auto* n = static_cast<ChainedCmpExpr*>(e);
        os_ << "ChainedCmpExpr\n";
        for (auto* op : n->operands)
            print(op, indent + 1);
        break;
    }
    case NodeKind::PowerExpr: {
        auto* n = static_cast<PowerExpr*>(e);
        os_ << "PowerExpr\n";
        print(n->base, indent + 1);
        print(n->exp, indent + 1);
        break;
    }
    case NodeKind::CallExpr: {
        auto* n = static_cast<CallExpr*>(e);
        os_ << "CallExpr\n";
        print(n->callee, indent + 1);
        for (auto* arg : n->args)
            print(arg, indent + 1);
        break;
    }
    case NodeKind::PostfixCastExpr: {
        auto* n = static_cast<PostfixCastExpr*>(e);
        os_ << "PostfixCastExpr";
        if (n->target_type) os_ << " to=" << n->target_type->toString();
        os_ << "\n";
        print(n->expr, indent + 1);
        break;
    }
    case NodeKind::SizeofExpr: {
        auto* n = static_cast<SizeofExpr*>(e);
        os_ << "SizeofExpr";
        if (n->target_type) os_ << " type=" << n->target_type->toString();
        os_ << "\n";
        if (n->target_expr) print(n->target_expr, indent + 1);
        break;
    }
    case NodeKind::OffsetExpr: {
        auto* n = static_cast<OffsetExpr*>(e);
        os_ << "OffsetExpr class='" << n->class_name << "' member='" << n->member_name << "'\n";
        break;
    }
    case NodeKind::ArrayIndexExpr: {
        auto* n = static_cast<ArrayIndexExpr*>(e);
        os_ << "ArrayIndexExpr\n";
        print(n->base, indent + 1);
        print(n->index, indent + 1);
        break;
    }
    case NodeKind::FieldAccessExpr: {
        auto* n = static_cast<FieldAccessExpr*>(e);
        os_ << "FieldAccessExpr '" << n->field << "'" << (n->is_arrow ? " (->)" : " (.)") << "\n";
        print(n->object, indent + 1);
        break;
    }
    case NodeKind::AddrOfExpr: {
        auto* n = static_cast<AddrOfExpr*>(e);
        os_ << "AddrOfExpr\n";
        print(n->operand, indent + 1);
        break;
    }
    case NodeKind::DerefExpr: {
        auto* n = static_cast<DerefExpr*>(e);
        os_ << "DerefExpr\n";
        print(n->operand, indent + 1);
        break;
    }
    case NodeKind::ThrowExpr: {
        auto* n = static_cast<ThrowExpr*>(e);
        os_ << "ThrowExpr\n";
        print(n->code, indent + 1);
        break;
    }
    default: break;
    }
}

/**
 * @brief Single statement node को उसके children के साथ print करो।
 *
 * @param s      Print करने वाला statement node।
 * @param indent Current indentation depth।
 */
void ASTPrinter::printStmt(Stmt* s, int indent) {
    printIndent(indent);
    switch (s->nk) {
    case NodeKind::CompoundStmt: {
        auto* n = static_cast<CompoundStmt*>(s);
        os_ << "CompoundStmt\n";
        for (auto* st : n->stmts)
            print(st, indent + 1);
        break;
    }
    case NodeKind::DeclStmt: {
        auto* n = static_cast<DeclStmt*>(s);
        os_ << "DeclStmt\n";
        print(n->decl, indent + 1);
        break;
    }
    case NodeKind::ExprStmt: {
        auto* n = static_cast<ExprStmt*>(s);
        os_ << "ExprStmt\n";
        print(n->expr, indent + 1);
        break;
    }
    case NodeKind::IfStmt: {
        auto* n = static_cast<IfStmt*>(s);
        os_ << "IfStmt\n";
        print(n->cond, indent + 1);
        print(n->then_body, indent + 1);
        if (n->else_body) print(n->else_body, indent + 1);
        break;
    }
    case NodeKind::ForStmt: {
        auto* n = static_cast<ForStmt*>(s);
        os_ << "ForStmt\n";
        if (n->init) print(n->init, indent + 1);
        if (n->cond) print(n->cond, indent + 1);
        if (n->post) print(n->post, indent + 1);
        print(n->body, indent + 1);
        break;
    }
    case NodeKind::WhileStmt: {
        auto* n = static_cast<WhileStmt*>(s);
        os_ << "WhileStmt\n";
        print(n->cond, indent + 1);
        print(n->body, indent + 1);
        break;
    }
    case NodeKind::DoWhileStmt: {
        auto* n = static_cast<DoWhileStmt*>(s);
        os_ << "DoWhileStmt\n";
        print(n->body, indent + 1);
        print(n->cond, indent + 1);
        break;
    }
    case NodeKind::SwitchStmt: {
        auto* n = static_cast<SwitchStmt*>(s);
        os_ << "SwitchStmt\n";
        print(n->expr, indent + 1);
        for (auto* c : n->cases)
            print(c, indent + 1);
        break;
    }
    case NodeKind::CaseStmt: {
        auto* n = static_cast<CaseStmt*>(s);
        if (n->value) {
            os_ << "CaseStmt\n";
            print(n->value, indent + 1);
        } else {
            os_ << "DefaultStmt\n";
        }
        for (auto* st : n->stmts)
            print(st, indent + 1);
        break;
    }
    case NodeKind::BreakStmt:
        os_ << "BreakStmt\n";
        break;
    case NodeKind::ContinueStmt:
        os_ << "ContinueStmt\n";
        break;
    case NodeKind::ReturnStmt: {
        auto* n = static_cast<ReturnStmt*>(s);
        os_ << "ReturnStmt\n";
        if (n->value) print(n->value, indent + 1);
        break;
    }
    case NodeKind::GotoStmt: {
        auto* n = static_cast<GotoStmt*>(s);
        os_ << "GotoStmt '" << n->label << "'\n";
        break;
    }
    case NodeKind::LabelStmt: {
        auto* n = static_cast<LabelStmt*>(s);
        os_ << "LabelStmt '" << n->name << "'\n";
        if (n->stmt) print(n->stmt, indent + 1);
        break;
    }
    case NodeKind::AsmStmt: {
        os_ << "AsmStmt\n";
        break;
    }
    case NodeKind::TryCatchStmt: {
        auto* n = static_cast<TryCatchStmt*>(s);
        os_ << "TryCatchStmt\n";
        print(n->try_body, indent + 1);
        print(n->catch_body, indent + 1);
        break;
    }
    case NodeKind::ExeBlockStmt: {
        auto* n = static_cast<ExeBlockStmt*>(s);
        os_ << "ExeBlockStmt\n";
        print(n->body, indent + 1);
        break;
    }
    case NodeKind::StringOutputStmt: {
        auto* n = static_cast<StringOutputStmt*>(s);
        os_ << "StringOutputStmt";
        if (n->format) os_ << " fmt=\"" << n->format->value << "\"";
        os_ << "\n";
        for (auto* arg : n->args)
            print(arg, indent + 1);
        break;
    }
    default: break;
    }
}

/**
 * @brief Single declaration node को उसके children के साथ print करो।
 *
 * @param d      Print करने वाला declaration node।
 * @param indent Current indentation depth।
 */
void ASTPrinter::printDecl(Decl* d, int indent) {
    printIndent(indent);
    switch (d->nk) {
    case NodeKind::VarDecl: {
        auto* n = static_cast<VarDecl*>(d);
        os_ << "VarDecl '" << n->name << "'";
        if (n->type) os_ << " type=" << n->type->toString();
        if (n->init) {
            os_ << " init=\n";
            print(n->init, indent + 1);
        } else {
            os_ << "\n";
        }
        break;
    }
    case NodeKind::FuncDecl: {
        auto* n = static_cast<FuncDecl*>(d);
        os_ << "FuncDecl '" << n->name << "'";
        if (n->return_type) os_ << " returns " << n->return_type->toString();
        os_ << "\n";
        for (auto* p : n->params)
            print(p, indent + 1);
        if (n->body) print(n->body, indent + 1);
        break;
    }
    case NodeKind::ParamDecl: {
        auto* n = static_cast<ParamDecl*>(d);
        os_ << "ParamDecl '" << n->name << "'";
        if (n->type) os_ << " type=" << n->type->toString();
        os_ << "\n";
        if (n->default_value) print(n->default_value, indent + 1);
        break;
    }
    case NodeKind::ClassDecl: {
        auto* n = static_cast<ClassDecl*>(d);
        os_ << "ClassDecl '" << n->name << "'";
        if (!n->base_name.empty()) os_ << " : " << n->base_name;
        os_ << "\n";
        for (auto* m : n->members)
            print(m, indent + 1);
        break;
    }
    case NodeKind::UnionDecl: {
        auto* n = static_cast<UnionDecl*>(d);
        os_ << "UnionDecl '" << n->name << "'\n";
        for (auto* m : n->members)
            print(m, indent + 1);
        break;
    }
    case NodeKind::FieldDecl: {
        auto* n = static_cast<FieldDecl*>(d);
        os_ << "FieldDecl '" << n->name << "'";
        if (n->type) os_ << " type=" << n->type->toString();
        if (n->bit_width >= 0) os_ << " bits=" << n->bit_width;
        os_ << "\n";
        break;
    }
    case NodeKind::TypedefDecl: {
        auto* n = static_cast<TypedefDecl*>(d);
        os_ << "TypedefDecl '" << n->name << "'";
        if (n->type) os_ << " type=" << n->type->toString();
        os_ << "\n";
        break;
    }
    case NodeKind::ExternDecl: {
        auto* n = static_cast<ExternDecl*>(d);
        os_ << "ExternDecl\n";
        if (n->inner) print(n->inner, indent + 1);
        break;
    }
    default: break;
    }
}

} // namespace holyc

#pragma once

#include <ostream>
#include "ast/AST.h"

namespace holyc {

/**
 * @brief AST subtree को indentation के साथ output stream पर recursively print करता है।
 */
class ASTPrinter {
public:
    explicit ASTPrinter(std::ostream& os) : os_(os) {}

    /**
     * @brief Node और उसके सभी children को दिए गए indentation level पर print करो।
     *
     * @param node   Print करने वाला AST node (nullptr हो सकता है)।
     * @param indent Current indentation depth (हर level दो spaces जोड़ता है)।
     */
    void print(Node* node, int indent = 0);

private:
    std::ostream& os_;
    void printIndent(int indent);
    void printExpr(Expr* e, int indent);
    void printStmt(Stmt* s, int indent);
    void printDecl(Decl* d, int indent);
};

} // namespace holyc

#pragma once

#include <vector>
#include "ast/AST.h"

namespace holyc {

/**
 * @brief AST का root; सभी top-level declarations और statements रखता है।
 */
struct TranslationUnit {
    std::vector<Node*> decls; //!< Top-level declarations और executable statements।
};

} // namespace holyc

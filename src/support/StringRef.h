#pragma once

#include <string_view>

namespace holyc {

/**
 * @brief String का non-owning reference; std::string_view का alias।
 */
using StringRef = std::string_view;

} // namespace holyc

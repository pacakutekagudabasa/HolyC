#pragma once

#include <cstdint>
#include <string>

namespace holyc {

/**
 * @brief Source buffer में point करने वाला (file, line, column) location।
 */
struct SourceLocation {
    const char* file_path = nullptr; //!< SourceManager filename string में pointer।
    uint32_t line = 0;               //!< 1-based line number; 0 का मतलब unknown।
    uint32_t col = 0;                //!< 1-based column number; 0 का मतलब unknown।

    /**
     * @brief Location को "file:line:col" format करो, या unset हो तो "<unknown>:0:0"।
     *
     * @return Human-readable location string।
     */
    std::string toString() const {
        if (!file_path)
            return "<unknown>:0:0";
        return std::string(file_path) + ":" + std::to_string(line) + ":" + std::to_string(col);
    }
};

} // namespace holyc

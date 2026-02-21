#pragma once

#include <deque>
#include <string>
#include <vector>

#include "SourceLocation.h"

namespace holyc {

/**
 * @brief Source buffers own करता है और byte offsets को line/column locations में map करता है।
 */
class SourceManager {
public:
    /**
     * @brief Disk से file load करो; fileId या failure पर -1 लौटाओ।
     *
     * @param path Source file का filesystem path।
     * @return Success पर non-negative fileId, failure पर -1।
     */
    int loadFile(const std::string& path);

    /**
     * @brief In-memory string से virtual file create करो; fileId लौटाओ।
     *
     * @param name Virtual file का display name (जैसे "<stdin>")।
     * @param content Source text।
     * @return Non-negative fileId।
     */
    int loadString(const std::string& name, const std::string& content);

    /**
     * @brief fileId के लिए raw source buffer लौटाओ, या invalid हो तो ""।
     *
     * @param fileId loadFile या loadString से returned file identifier।
     * @return Source text का reference, या invalid id पर empty string।
     */
    const std::string& getBuffer(int fileId) const;

    /**
     * @brief fileId के लिए filename लौटाओ, या invalid हो तो ""।
     *
     * @param fileId File identifier।
     * @return Filename का reference, या invalid id पर empty string।
     */
    const std::string& getFileName(int fileId) const;

    /**
     * @brief fileId के अंदर byte offset को (file, line, col) SourceLocation में convert करो।
     *
     * Fast forward scanning के लिए sequential-access cache use करता है।
     *
     * @param fileId File identifier।
     * @param offset Source buffer में byte offset।
     * @return Corresponding SourceLocation, या error पर default-constructed।
     */
    SourceLocation getLocation(int fileId, size_t offset) const;

    /**
     * @brief दिए गए 1-based line number पर source line (newline के बिना) लौटाओ।
     *
     * @param fileId File identifier।
     * @param line   1-based line number।
     * @return Source line text, या out of range हो तो ""।
     */
    std::string getLine(int fileId, uint32_t line) const;

private:
    struct FileEntry {
        std::string filename;
        std::string content;
        std::vector<size_t> line_offsets;
        mutable size_t   last_query_offset = 0; //!< getLocation() के लिए sequential-access cache।
        mutable uint32_t last_query_line   = 1; //!< last_query_offset के corresponding line number।
    };

    /**
     * @brief entry.line_offsets को हर line start के byte offset से populate करो।
     *
     * @param entry Populate करने वाला FileEntry।
     */
    void buildLineOffsets(FileEntry& entry);

    std::deque<FileEntry> files_;
};

} // namespace holyc

#include "SourceManager.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace holyc {

static const std::string kEmpty;

/**
 * @brief Disk से file को नए FileEntry में load करो और उसका fileId लौटाओ।
 *
 * @param path Source file का filesystem path।
 * @return Success पर non-negative fileId, failure पर -1।
 */
int SourceManager::loadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return -1;
    std::ostringstream ss;
    ss << in.rdbuf();

    FileEntry entry;
    entry.filename = path;
    entry.content = ss.str();
    buildLineOffsets(entry);
    files_.push_back(std::move(entry));
    return static_cast<int>(files_.size() - 1);
}

/**
 * @brief String buffer से virtual file create करो और उसका fileId लौटाओ।
 *
 * @param name    Virtual file का display name।
 * @param content Source text।
 * @return Non-negative fileId।
 */
int SourceManager::loadString(const std::string& name, const std::string& content) {
    FileEntry entry;
    entry.filename = name;
    entry.content = content;
    buildLineOffsets(entry);
    files_.push_back(std::move(entry));
    return static_cast<int>(files_.size() - 1);
}

/**
 * @brief File का raw source buffer लौटाओ, या invalid हो तो empty string।
 *
 * @param fileId File identifier।
 * @return Source text का reference।
 */
const std::string& SourceManager::getBuffer(int fileId) const {
    if (fileId < 0 || fileId >= static_cast<int>(files_.size()))
        return kEmpty;
    return files_[fileId].content;
}

/**
 * @brief File entry का filename लौटाओ, या invalid हो तो empty string।
 *
 * @param fileId File identifier।
 * @return Filename का reference।
 */
const std::string& SourceManager::getFileName(int fileId) const {
    if (fileId < 0 || fileId >= static_cast<int>(files_.size()))
        return kEmpty;
    return files_[fileId].filename;
}

/**
 * @brief Byte offset को (file, line, col) location में convert करो।
 *
 * Lexer sequentially advance करे तो fast-path binary search avoid करता है।
 *
 * @param fileId File identifier।
 * @param offset Source buffer में byte offset।
 * @return Corresponding SourceLocation, या error पर default-constructed।
 */
SourceLocation SourceManager::getLocation(int fileId, size_t offset) const {
    if (fileId < 0 || fileId >= static_cast<int>(files_.size()))
        return {};

    const auto& entry = files_[fileId];
    const auto& offsets = entry.line_offsets;

    uint32_t line;
    if (offset >= entry.last_query_offset) {
        uint32_t last = entry.last_query_line;
        if (last < static_cast<uint32_t>(offsets.size()) && offset < offsets[last]) {
            line = last;
        } else {
            auto it = std::upper_bound(offsets.begin(), offsets.end(), offset);
            line = static_cast<uint32_t>(it - offsets.begin());
            entry.last_query_offset = offset;
            entry.last_query_line   = line;
        }
    } else {
        auto it = std::upper_bound(offsets.begin(), offsets.end(), offset);
        line = static_cast<uint32_t>(it - offsets.begin());
        entry.last_query_offset = offset;
        entry.last_query_line   = line;
    }

    if (line == 0 || offsets.empty()) return {};
    uint32_t col = static_cast<uint32_t>(offset - offsets[line - 1] + 1);
    return {entry.filename.c_str(), line, col};
}

/**
 * @brief Single source line (trailing newline stripped) लौटाओ।
 *
 * @param fileId File identifier।
 * @param line   1-based line number।
 * @return Source line text, या out of range हो तो ""।
 */
std::string SourceManager::getLine(int fileId, uint32_t line) const {
    if (fileId < 0 || fileId >= static_cast<int>(files_.size()) || line == 0)
        return {};

    const auto& entry = files_[fileId];
    if (line > entry.line_offsets.size())
        return {};

    size_t start = entry.line_offsets[line - 1];
    size_t end = (line < entry.line_offsets.size())
                     ? entry.line_offsets[line]
                     : entry.content.size();

    while (end > start && (entry.content[end - 1] == '\n' || entry.content[end - 1] == '\r'))
        --end;

    return entry.content.substr(start, end - start);
}

/**
 * @brief entry.line_offsets को हर line start के byte offset से populate करो।
 *
 * @param entry वह FileEntry जिसका content already set है।
 */
void SourceManager::buildLineOffsets(FileEntry& entry) {
    entry.line_offsets.clear();
    entry.line_offsets.push_back(0);
    for (size_t i = 0; i < entry.content.size(); ++i) {
        if (entry.content[i] == '\n')
            entry.line_offsets.push_back(i + 1);
    }
}

} // namespace holyc

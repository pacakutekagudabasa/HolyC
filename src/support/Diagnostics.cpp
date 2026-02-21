#include "Diagnostics.h"
#include "SourceManager.h"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace holyc {

namespace {
// ANSI color codes — terminal में रंग के लिए
static const char* ANSI_RESET   = "\033[0m";
static const char* ANSI_BOLD    = "\033[1m";
static const char* ANSI_RED     = "\033[1;31m";
static const char* ANSI_YELLOW  = "\033[1;33m";
static const char* ANSI_CYAN    = "\033[1;36m";
static const char* ANSI_WHITE   = "\033[1;37m";
} // namespace

void Diagnostics::error(SourceLocation loc, const std::string& msg) {
    if (maxErrors_ > 0 && errorCount_ >= maxErrors_) {
        if (errorCount_ == maxErrors_) {
            ++errorCount_;
            std::cerr << "fatal: too many errors emitted, stopping now\n";
        }
        return;
    }
    ++errorCount_;
    if (storeEntries_) entries_.push_back({DiagLevel::Error, loc, msg, {}, {}});
    emit("error", loc, msg, {}, {});
}

void Diagnostics::error(SourceLocation loc, const std::string& msg, SourceRange range) {
    if (maxErrors_ > 0 && errorCount_ >= maxErrors_) {
        if (errorCount_ == maxErrors_) {
            ++errorCount_;
            std::cerr << "fatal: too many errors emitted, stopping now\n";
        }
        return;
    }
    ++errorCount_;
    if (storeEntries_) entries_.push_back({DiagLevel::Error, loc, msg, range, {}});
    emit("error", loc, msg, range, {});
}

void Diagnostics::errorWithFixit(SourceLocation loc, const std::string& msg,
                                  const std::string& fixit) {
    if (maxErrors_ > 0 && errorCount_ >= maxErrors_) {
        if (errorCount_ == maxErrors_) {
            ++errorCount_;
            std::cerr << "fatal: too many errors emitted, stopping now\n";
        }
        return;
    }
    ++errorCount_;
    std::vector<std::string> fixits = {fixit};
    if (storeEntries_) entries_.push_back({DiagLevel::Error, loc, msg, {}, fixits});
    emit("error", loc, msg, {}, fixits);
}

void Diagnostics::warning(SourceLocation loc, const std::string& msg) {
    if (suppressWarnings_) return;
    if (warningsAsErrors_) {
        error(loc, msg);
        return;
    }
    if (storeEntries_) entries_.push_back({DiagLevel::Warning, loc, msg, {}, {}});
    emit("warning", loc, msg, {}, {});
}

void Diagnostics::note(SourceLocation loc, const std::string& msg) {
    if (storeEntries_) entries_.push_back({DiagLevel::Note, loc, msg, {}, {}});
    emit("note", loc, msg, {}, {});
}

/**
 * @brief Diagnostic को stderr पर format और write करो, optionally caret के साथ source context print करो।
 *
 * Valid SourceRange provide हो तो वह SourceManager-based caret से priority लेता है।
 *
 * @param level  Severity string ("error", "warning", "note")।
 * @param loc    Diagnostic की source location।
 * @param msg    Message text।
 * @param range  Underline display के लिए optional source range।
 * @param fixits Message के बाद print करने वाले optional fix-it hint strings।
 */
void Diagnostics::emit(const char* level, SourceLocation loc, const std::string& msg,
                       const SourceRange& range, const std::vector<std::string>& fixits) {
    if (useColor_) {
        const char* levelColor = ANSI_WHITE;
        if (std::string(level) == "error")   levelColor = ANSI_RED;
        if (std::string(level) == "warning") levelColor = ANSI_YELLOW;
        if (std::string(level) == "note")    levelColor = ANSI_CYAN;
        std::cerr << ANSI_BOLD << loc.toString() << ANSI_RESET
                  << ": " << levelColor << level << ANSI_RESET
                  << ": " << ANSI_BOLD << msg << ANSI_RESET << "\n";
    } else {
        std::cerr << loc.toString() << ": " << level << ": " << msg << "\n";
    }

    // SourceRange valid हो और file_path हो तो source line ^~~~ underline के साथ print करो।
    // यह SourceManager-based caret से priority लेता है।
    if (range.valid() && range.start.file_path && range.start.file_path[0] != '\0') {
        std::ifstream src(range.start.file_path);
        if (src) {
            std::string srcLine;
            int lineno = 1;
            while (std::getline(src, srcLine)) {
                if (lineno == static_cast<int>(range.start.line)) {
                    std::cerr << "  " << srcLine << "\n";
                    // Underline build करो: col-1 तक spaces (indent के 2 + col-1), फिर ^~~~
                    uint32_t startCol = range.start.col > 0 ? range.start.col : 1;
                    std::string underline(2 + startCol - 1, ' ');
                    int len = 1;
                    if (range.end.line == range.start.line && range.end.col > startCol)
                        len = static_cast<int>(range.end.col - startCol);
                    underline += '^';
                    for (int i = 1; i < len; ++i) underline += '~';
                    std::cerr << underline << "\n";
                    break;
                }
                ++lineno;
            }
        }
    } else if (sm_ && loc.file_path && loc.line > 0) {
        int fid = resolveFileId(loc.file_path);
        if (fid >= 0)
            renderCaret(fid, loc.line, loc.col);
    }

    // Fix-it hints print करो।
    for (const auto& f : fixits) {
        std::cerr << "  fix: " << f << "\n";
    }
}

/**
 * @brief (fileId, line) पर source line को column col पर ^ caret के साथ print करो।
 *
 * @param fileId SourceManager में file identifier।
 * @param line   1-based line number।
 * @param col    1-based column number।
 */
void Diagnostics::renderCaret(int fileId, uint32_t line, uint32_t col) {
    std::string srcLine = sm_->getLine(fileId, line);
    if (srcLine.empty())
        return;

    std::cerr << "  " << srcLine << "\n";

    // Caret line build करो: col-1 तक spaces, फिर ^ और ~~~।
    if (col == 0)
        col = 1;
    std::string caret(col - 1 + 2, ' '); // leading "  " indent के लिए +2
    caret += '^';

    // Visual emphasis के लिए कुछ tildes add करो अगर room हो।
    size_t remaining = srcLine.size() - (col - 1);
    if (remaining > 1) {
        size_t tildes = remaining - 1;
        if (tildes > 6)
            tildes = 6;
        caret.append(tildes, '~');
    }

    std::cerr << caret << "\n";
}

/**
 * @brief SourceManager entries scan करके path का fileId ढूंढो; न मिले तो -1 लौटाओ।
 *
 * Linear scan; typical compiler invocations में few files होते हैं इसलिए ठीक है।
 *
 * @param path Lookup करने वाला file path string।
 * @return Matching fileId, या न मिले तो -1।
 */
int Diagnostics::resolveFileId(const char* path) const {
    if (!sm_ || !path)
        return -1;

    // Linear scan; typical compiler invocations में few files के लिए fine।
    for (int i = 0;; ++i) {
        const std::string& name = sm_->getFileName(i);
        if (name.empty())
            return -1;
        if (name.c_str() == path || name == path)
            return i;
    }
}

} // namespace holyc

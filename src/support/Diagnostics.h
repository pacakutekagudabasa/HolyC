#pragma once

#include <string>
#include <vector>

#include "SourceLocation.h"

namespace holyc {

class SourceManager;

/** @brief Diagnostic message की severity level। */
enum class DiagLevel { Note, Warning, Error };

/** @brief Caret display के लिए underline annotations print करने वाला optional source range। */
struct SourceRange {
    SourceLocation start;
    SourceLocation end;
    bool valid() const { return start.line > 0; }
};

/** @brief Single stored diagnostic entry (LSP / programmatic consumers के लिए)। */
struct DiagEntry {
    DiagLevel      level;
    SourceLocation loc;
    std::string    message;
    SourceRange    range;   //!< Underline display के लिए optional source range।
    std::vector<std::string> fixits; //!< Optional fix-it hint strings।
};

/**
 * @brief Optional caret display और colour के साथ compiler diagnostics collect और emit करता है।
 */
class Diagnostics {
public:
    explicit Diagnostics(SourceManager* sm) : sm_(sm) {}

    /**
     * @brief loc पर error emit करो और error counter increment करो।
     *
     * @param loc Error की source location।
     * @param msg Error message text।
     */
    void error(SourceLocation loc, const std::string& msg);

    /**
     * @brief Warning emit करो (या warningsAsErrors set हो तो error)।
     *
     * @param loc Warning की source location।
     * @param msg Warning message text।
     */
    void warning(SourceLocation loc, const std::string& msg);

    /**
     * @brief Informational note emit करो।
     *
     * @param loc Note की source location।
     * @param msg Note message text।
     */
    void note(SourceLocation loc, const std::string& msg);

    /**
     * @brief Underline display के लिए explicit source range के साथ error emit करो।
     *
     * @param loc   Error की source location।
     * @param msg   Error message text।
     * @param range Caret display में underline करने वाला source range।
     */
    void error(SourceLocation loc, const std::string& msg, SourceRange range);

    /**
     * @brief Diagnostic के बाद fix-it hint append करके error emit करो।
     *
     * @param loc   Error की source location।
     * @param msg   Error message text।
     * @param fixit Human-readable fix-it suggestion।
     */
    void errorWithFixit(SourceLocation loc, const std::string& msg,
                        const std::string& fixit);

    /**
     * @brief True लौटाओ अगर कोई error emit हुई है।
     *
     * @return true जब errorCount_ > 0 हो।
     */
    bool hasErrors() const { return errorCount_ > 0; }

    /**
     * @brief Emit हुई errors की total संख्या लौटाओ।
     *
     * @return Current error count।
     */
    int errorCount() const { return errorCount_; }

    /**
     * @brief Error state reset करो (REPL में inputs के बीच use होता है)।
     */
    void clearErrors() { errorCount_ = 0; entries_.clear(); }

    /**
     * @brief Enable हो तो सभी warnings errors में promote होती हैं।
     *
     * @param v Enable flag।
     */
    void setWarningsAsErrors(bool v) { warningsAsErrors_ = v; }

    /**
     * @brief Enable हो तो warnings silently drop होती हैं।
     *
     * @param v Enable flag।
     */
    void setSuppressWarnings(bool v) { suppressWarnings_ = v; }

    /**
     * @brief n errors के बाद emit बंद करो (0 = unlimited)।
     *
     * @param n Emit करने वाली maximum errors की संख्या।
     */
    void setMaxErrors(int n) { maxErrors_ = n; }

    /**
     * @brief Error output में ANSI colour enable करो।
     *
     * @param v Enable flag।
     */
    void setColor(bool v) { useColor_ = v; }

    /**
     * @brief LSP / programmatic consumers के लिए entry storage enable करो।
     *
     * Default off।
     *
     * @param v Enable flag।
     */
    void setStoreEntries(bool v) { storeEntries_ = v; }

    /** @brief Stored diagnostic entries लौटाओ। */
    const std::vector<DiagEntry>& entries() const { return entries_; }

private:
    void emit(const char* level, SourceLocation loc, const std::string& msg,
              const SourceRange& range, const std::vector<std::string>& fixits);
    void renderCaret(int fileId, uint32_t line, uint32_t col);

    /**
     * @brief SourceManager entries scan करके SourceLocation file_path से file ID resolve करो।
     *
     * @param path Lookup करने वाला file path string।
     * @return Matching fileId, या न मिले तो -1।
     */
    int resolveFileId(const char* path) const;

    SourceManager* sm_;
    int errorCount_ = 0;
    bool warningsAsErrors_ = false;
    bool suppressWarnings_ = false;
    int maxErrors_ = 0; // 0 = unlimited
    bool useColor_ = false; // stderr tty हो (या forced हो) तो true set करो
    bool storeEntries_ = false;
    std::vector<DiagEntry> entries_;
};

} // namespace holyc

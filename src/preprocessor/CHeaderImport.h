#pragma once
#include <string>
#include <vector>

namespace holyc {

/**
 * @brief C system header से function declarations import करता है, उन्हें
 *        HolyC extern declarations में convert करके token stream में inject करता है।
 */
class CHeaderImport {
public:
    /**
     * @brief दिए गए system header path पर C preprocessor run करो और सभी top-level
     *        C functions के लिए HolyC extern declarations की string लौटाओ।
     *
     * किसी भी failure पर empty string लौटाता है (non-fatal)।
     *
     * @param headerPath Resolved system header file का absolute path।
     * @param headerName #include directive में जैसा लिखा original header name।
     * @return HolyC extern declaration strings का newline-separated sequence,
     *         या header process न हो सके तो empty string।
     */
    static std::string import(const std::string& headerPath,
                               const std::string& headerName);

private:
    /**
     * @brief `cc -E -P -x c <header>` run करो और preprocessed output लौटाओ।
     *
     * @param headerPath Header file का absolute path; isSafeHeaderPath() pass करना ज़रूरी।
     * @return Preprocessed text, या failure पर empty string।
     */
    static std::string runCPreprocessor(const std::string& headerPath);

    /**
     * @brief Preprocessed C output parse करो और function declarations extract करो,
     *        उन्हें HolyC extern declaration strings में लौटाओ।
     *
     * @param cpp_output runCPreprocessor() से produce हुई preprocessed C source text।
     * @return HolyC extern declaration strings का vector, हर discovered function के लिए एक।
     */
    static std::vector<std::string> extractFuncDecls(const std::string& cpp_output);

    /**
     * @brief C type string (जैसे "unsigned int") को HolyC type string में convert करो।
     *
     * @param ctype C type string, qualifiers और pointer star include हो सकते हैं।
     * @return Equivalent HolyC type string (जैसे "U32" या "U8*")।
     */
    static std::string cTypeToHolyC(const std::string& ctype);

    /**
     * @brief C parameter list string को HolyC parameter list string में convert करो।
     *
     * @param params    Raw C parameter list (surrounding parentheses के बिना)।
     * @param is_vararg Return पर true set होता है अगर parameter list '...' से end हो।
     * @return Extern declaration में use के लिए suitable HolyC parameter list string।
     */
    static std::string convertParamList(const std::string& params, bool& is_vararg);
};

} // namespace holyc

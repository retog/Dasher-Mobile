#include "LanguageSet.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace {

struct Letter {
    const char *lower;
    const char *upper;   ///< Empty if the letter has no upper-case form.
};

/// Every letter of every supported language, in the order the boxes are shown.
const Letter kLetters[] = {
    {"a", "A"}, {"á", "Á"}, {"à", "À"}, {"â", "Â"}, {"ä", "Ä"}, {"æ", "Æ"},
    {"b", "B"},
    {"c", "C"}, {"ç", "Ç"}, {"č", "Č"},
    {"d", "D"}, {"ď", "Ď"},
    {"e", "E"}, {"é", "É"}, {"è", "È"}, {"ê", "Ê"}, {"ë", "Ë"},
    {"f", "F"}, {"g", "G"}, {"h", "H"},
    {"i", "I"}, {"í", "Í"}, {"ì", "Ì"}, {"î", "Î"}, {"ï", "Ï"},
    {"j", "J"}, {"k", "K"},
    {"l", "L"}, {"ĺ", "Ĺ"}, {"ľ", "Ľ"},
    {"m", "M"},
    {"n", "N"}, {"ñ", "Ñ"}, {"ň", "Ň"},
    {"o", "O"}, {"ó", "Ó"}, {"ò", "Ò"}, {"ô", "Ô"}, {"ö", "Ö"}, {"œ", "Œ"},
    {"p", "P"}, {"q", "Q"},
    {"r", "R"}, {"ŕ", "Ŕ"},
    {"s", "S"}, {"š", "Š"}, {"ß", ""},
    {"t", "T"}, {"ť", "Ť"},
    {"u", "U"}, {"ú", "Ú"}, {"ù", "Ù"}, {"û", "Û"}, {"ü", "Ü"},
    {"v", "V"}, {"w", "W"}, {"x", "X"},
    {"y", "Y"}, {"ý", "Ý"}, {"ÿ", "Ÿ"},
    {"z", "Z"}, {"ž", "Ž"},
};

struct Language {
    const char *code;
    /// Letters besides a–z, separated by blanks.
    const char *extraLetters;
    /// Punctuation besides the common set, separated by blanks.
    const char *extraPunctuation;
    const char *legacyAlphabetId;
    const char *corpusAsset;
    const char *legacyUserTextFile;
};

/// In alphabetical order of the codes.
const Language kLanguages[] = {
    {"de", "ä ö ü ß", "", "", "", ""},
    {"en", "", "", "English with limited punctuation", "", "training_english_GB.txt"},
    {"es", "á é í ñ ó ú ü", "¿ ¡", "", "", ""},
    {"fr", "à â æ ç é è ê ë î ï ô œ ù û ü ÿ", "", "", "", ""},
    {"it", "à è é ì ò ù", "", "", "", ""},
    {"sk", "á ä č ď é í ĺ ľ ň ó ô ŕ š ť ú ý ž", "", "Slovak", "training_slovak_SK.txt",
     "training_slovak_SK.txt"},
};

const char *const kPunctuation[] = {",", ".", "?", "!", "'", "\"", "-", ":", ";", "(", ")"};

constexpr char kUserTextPrefix[] = "user_text_";
constexpr char kUserTextSuffix[] = ".txt";

const Language *findLanguage(const std::string &code) {
    for (const Language &language : kLanguages) {
        if (code == language.code) return &language;
    }
    return nullptr;
}

std::vector<std::string> splitOn(const std::string &text, const char *separators) {
    std::vector<std::string> parts;
    std::string part;
    for (const char c : text) {
        if (std::strchr(separators, c)) {
            if (!part.empty()) parts.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    if (!part.empty()) parts.push_back(part);
    return parts;
}

std::string xmlEscaped(const std::string &text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

void appendNode(std::string &xml, const std::string &text) {
    xml += "<node label=\"" + xmlEscaped(text) + "\"><textCharAction/></node>\n";
}

}

namespace DasherAndroid {

std::vector<std::string> SupportedLanguages() {
    std::vector<std::string> codes;
    for (const Language &language : kLanguages) codes.emplace_back(language.code);
    return codes;
}

std::vector<std::string> ParseLanguages(const std::string &languages) {
    std::set<std::string> known;
    for (const std::string &code : splitOn(languages, "+, \t")) {
        if (findLanguage(code)) known.insert(code);
    }
    if (known.empty()) known.insert("en");
    return {known.begin(), known.end()};
}

std::string JoinLanguages(const std::vector<std::string> &languages) {
    std::string joined;
    for (const std::string &code : languages) {
        if (!joined.empty()) joined += '+';
        joined += code;
    }
    return joined;
}

bool LanguagesIntersect(const std::string &a, const std::string &b) {
    const std::vector<std::string> left = splitOn(a, "+, \t");
    for (const std::string &code : splitOn(b, "+, \t")) {
        if (std::find(left.begin(), left.end(), code) != left.end()) return true;
    }
    return false;
}

std::string AlphabetIdFor(const std::vector<std::string> &languages) {
    if (languages.size() == 1) {
        const Language *language = findLanguage(languages[0]);
        if (language && language->legacyAlphabetId[0]) return language->legacyAlphabetId;
    }
    return "Dasher Mobile " + JoinLanguages(languages);
}

std::string UserTextFileFor(const std::vector<std::string> &languages) {
    return kUserTextPrefix + JoinLanguages(languages) + kUserTextSuffix;
}

bool IsUserTextFile(const std::string &fileName, std::string *languages) {
    const size_t prefix = sizeof(kUserTextPrefix) - 1;
    const size_t suffix = sizeof(kUserTextSuffix) - 1;
    if (fileName.size() <= prefix + suffix
        || fileName.compare(0, prefix, kUserTextPrefix) != 0
        || fileName.compare(fileName.size() - suffix, suffix, kUserTextSuffix) != 0) {
        return false;
    }
    if (languages) *languages = fileName.substr(prefix, fileName.size() - prefix - suffix);
    return true;
}

std::string CorpusAssetFor(const std::string &language) {
    const Language *found = findLanguage(language);
    return found ? found->corpusAsset : "";
}

std::string ImportedCorpusFileFor(const std::string &language) {
    return "corpus_" + language + ".txt";
}

std::string LegacyUserTextFileFor(const std::string &language) {
    const Language *found = findLanguage(language);
    return found ? found->legacyUserTextFile : "";
}

std::string AlphabetXml(const std::vector<std::string> &languages) {
    std::set<std::string> extraLetters;
    std::vector<std::string> punctuation(std::begin(kPunctuation), std::end(kPunctuation));
    for (const std::string &code : languages) {
        const Language *language = findLanguage(code);
        if (!language) continue;
        for (const std::string &letter : splitOn(language->extraLetters, " ")) {
            extraLetters.insert(letter);
        }
        for (const std::string &mark : splitOn(language->extraPunctuation, " ")) {
            if (std::find(punctuation.begin(), punctuation.end(), mark) == punctuation.end()) {
                punctuation.push_back(mark);
            }
        }
    }

    std::string lower;
    std::string upper;
    for (const Letter &letter : kLetters) {
        // A one-byte letter is one of a–z, which every language has.
        if (letter.lower[1] != '\0' && !extraLetters.count(letter.lower)) continue;
        appendNode(lower, letter.lower);
        if (letter.upper[0]) appendNode(upper, letter.upper);
    }

    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<alphabet name=\"" + xmlEscaped(AlphabetIdFor(languages))
        + "\" orientation=\"LR\" trainingFilename=\"" + xmlEscaped(UserTextFileFor(languages))
        + "\" colorsName=\"European/Asian\">\n";
    xml += "<group name=\"Lower case letters\" colorInfoName=\"lowercase\">\n" + lower + "</group>\n";
    xml += "<group name=\"Upper case letters\" colorInfoName=\"uppercase\">\n" + upper + "</group>\n";
    xml += "<group name=\"Punctuation\" colorInfoName=\"limitedPunctuation\">\n";
    for (const std::string &mark : punctuation) appendNode(xml, mark);
    xml += "</group>\n";
    xml += "<group name=\"paragraphSpace\" colorInfoName=\"paragraphSpace\">\n";
    appendNode(xml, "\xC2\xB6");       // "¶", newline
    appendNode(xml, "\xE2\x96\xA1");   // "□", space
    xml += "</group>\n</alphabet>\n";
    return xml;
}

}

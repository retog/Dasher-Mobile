#include "SymbolText.h"

namespace {

constexpr char kSpaceSymbol[] = "\xE2\x96\xA1";   ///< U+25A1 WHITE SQUARE "□"
constexpr char kNewlineSymbol[] = "\xC2\xB6";     ///< U+00B6 PILCROW SIGN "¶"

bool startsWithAt(const std::string &s, size_t pos, const char *prefix, size_t len) {
    return s.compare(pos, len, prefix) == 0;
}

}

namespace DasherAndroid {

std::string CorpusToSymbolText(const std::string &corpus) {
    std::string out;
    out.reserve(corpus.size() + corpus.size() / 4);
    bool inGap = false;
    for (const char c : corpus) {
        if (c == ' ' || c == '\n' || c == '\r') {
            if (!inGap) out += kSpaceSymbol;
            inGap = true;
        } else {
            out += c;
            inGap = false;
        }
    }
    return out;
}

std::string ToPlainText(const std::string &symbolText) {
    constexpr size_t kSpaceLen = sizeof(kSpaceSymbol) - 1;
    constexpr size_t kNewlineLen = sizeof(kNewlineSymbol) - 1;
    std::string out;
    out.reserve(symbolText.size());
    for (size_t i = 0; i < symbolText.size();) {
        if (startsWithAt(symbolText, i, kSpaceSymbol, kSpaceLen)) {
            out += ' ';
            i += kSpaceLen;
        } else if (startsWithAt(symbolText, i, kNewlineSymbol, kNewlineLen)) {
            out += '\n';
            i += kNewlineLen;
        } else {
            out += symbolText[i++];
        }
    }
    return out;
}

}

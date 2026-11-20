#include "PersonalDictionary.h"

#include "LanguageSet.h"
#include "SymbolText.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

constexpr char kHeader[] = "#dasher-personal-dictionary 1";
constexpr char kEscape[] = "\xC2\xA7";            ///< "§", DasherCore's context-switch escape.
constexpr char kSpaceSymbol[] = "\xE2\x96\xA1";   ///< "□"
constexpr size_t kMaxWordBytes = 64;
constexpr unsigned kMaxWeight = 16;
constexpr unsigned kManualWeight = 4;
/// DasherCore gives a training segment at most this many symbols of context.
constexpr size_t kContextSymbols = 5;

/// Punctuation outside ASCII that ends a word.
const char *const kWidePunctuation[] = {
    "\xC2\xA7",      // §
    "\xC2\xA1",      // ¡
    "\xC2\xBF",      // ¿
    "\xC2\xAB",      // «
    "\xC2\xBB",      // »
    "\xC2\xB6",      // ¶
    "\xC2\xA0",      // no-break space
    "\xE2\x80\x93",  // –
    "\xE2\x80\x94",  // —
    "\xE2\x80\x98",  // ‘
    "\xE2\x80\x99",  // ’
    "\xE2\x80\x9A",  // ‚
    "\xE2\x80\x9C",  // “
    "\xE2\x80\x9D",  // ”
    "\xE2\x80\x9E",  // „
    "\xE2\x80\xA6",  // …
    "\xE2\x96\xA1",  // □
};

size_t charLength(const std::string &text, size_t pos) {
    size_t next = pos + 1;
    while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
    return next - pos;
}

size_t charCount(const std::string &text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size(); i += charLength(text, i)) ++count;
    return count;
}

bool isAsciiAlnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// Whether the character at @p pos is part of a word.  An apostrophe or hyphen is, if it joins
/// two word characters ("don't", "e-mail").
bool isWordChar(const std::string &text, size_t pos) {
    const char c = text[pos];
    if (static_cast<unsigned char>(c) < 0x80) {
        if (isAsciiAlnum(c)) return true;
        if (c != '\'' && c != '-') return false;
        return pos > 0 && pos + 1 < text.size()
            && (isAsciiAlnum(text[pos - 1]) || static_cast<unsigned char>(text[pos - 1]) >= 0x80)
            && (isAsciiAlnum(text[pos + 1]) || static_cast<unsigned char>(text[pos + 1]) >= 0x80)
            && isWordChar(text, pos + 1);
    }
    for (const char *mark : kWidePunctuation) {
        if (text.compare(pos, std::char_traits<char>::length(mark), mark) == 0) return false;
    }
    return true;
}

struct Token {
    std::string word;
    bool joined;   ///< Whether exactly one space separates it from the token before.
};

std::vector<Token> tokenize(const std::string &text) {
    std::vector<Token> tokens;
    std::string gap;
    bool first = true;
    for (size_t i = 0; i < text.size();) {
        if (!isWordChar(text, i)) {
            const size_t length = charLength(text, i);
            gap.append(text, i, length);
            i += length;
            continue;
        }
        Token token;
        while (i < text.size() && isWordChar(text, i)) {
            const size_t length = charLength(text, i);
            token.word.append(text, i, length);
            i += length;
        }
        token.joined = !first && gap == " ";
        tokens.push_back(token);
        gap.clear();
        first = false;
    }
    return tokens;
}

bool usable(const std::string &word) {
    return !word.empty() && word.size() <= kMaxWordBytes;
}

std::string normalized(const std::string &text) {
    std::string out;
    for (const Token &token : tokenize(text)) {
        if (!usable(token.word)) return {};
        if (!out.empty()) out += ' ';
        out += token.word;
    }
    return out;
}

}

namespace DasherAndroid {

void PersonalDictionary::Load(const std::string &path) {
    m_path = path;
    m_entries.clear();
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        // count \t manual \t lastUsed \t languages \t text
        std::vector<std::string> fields;
        size_t start = 0;
        for (int i = 0; i < 4; ++i) {
            const size_t tab = line.find('\t', start);
            if (tab == std::string::npos) break;
            fields.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (fields.size() != 4 || start >= line.size()) continue;
        Entry entry;
        entry.count = static_cast<unsigned>(std::strtoul(fields[0].c_str(), nullptr, 10));
        entry.manual = fields[1] == "1";
        entry.lastUsed = std::strtoll(fields[2].c_str(), nullptr, 10);
        entry.languages = fields[3];
        entry.text = line.substr(start);
        m_entries[{entry.text, entry.languages}] = entry;
    }
}

bool PersonalDictionary::Save() const {
    if (m_path.empty()) return false;
    const std::string temporary = m_path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << kHeader << '\n';
        for (const auto &item : m_entries) {
            const Entry &entry = item.second;
            out << entry.count << '\t' << (entry.manual ? 1 : 0) << '\t' << entry.lastUsed << '\t'
                << entry.languages << '\t' << entry.text << '\n';
        }
        if (!out.good()) return false;
    }
    // Replace the file in one step, so a crash never leaves half a dictionary.
    return std::rename(temporary.c_str(), m_path.c_str()) == 0;
}

void PersonalDictionary::count(const std::string &text, const std::string &languages, long long now) {
    Entry &entry = m_entries[{text, languages}];
    entry.text = text;
    entry.languages = languages;
    entry.count += 1;
    entry.lastUsed = now;
}

void PersonalDictionary::Learn(const std::string &text, const std::string &languages, long long now) {
    const std::vector<Token> tokens = tokenize(text);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string &word = tokens[i].word;
        if (!usable(word)) continue;
        if (charCount(word) >= 2) count(word, languages, now);
        std::string phrase = word;
        for (size_t j = i + 1; j < tokens.size() && j <= i + 2; ++j) {
            if (!tokens[j].joined || !usable(tokens[j].word)) break;
            phrase += ' ';
            phrase += tokens[j].word;
            count(phrase, languages, now);
        }
    }
    prune();
}

bool PersonalDictionary::Add(const std::string &text, const std::string &languages, long long now) {
    const std::string entryText = normalized(text);
    if (entryText.empty()) return false;
    Entry &entry = m_entries[{entryText, languages}];
    entry.text = entryText;
    entry.languages = languages;
    entry.manual = true;
    entry.lastUsed = now;
    return true;
}

bool PersonalDictionary::Remove(const std::string &text, const std::string &languages) {
    return m_entries.erase({text, languages}) > 0;
}

void PersonalDictionary::Clear(bool manualToo) {
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (manualToo || !it->second.manual) {
            it = m_entries.erase(it);
        } else {
            // Forget how often it was written, but keep what the user added.
            it->second.count = 0;
            ++it;
        }
    }
}

void PersonalDictionary::prune() {
    if (m_entries.size() <= kMaxEntries) return;
    std::vector<const Entry *> learned;
    for (const auto &item : m_entries) {
        if (!item.second.manual) learned.push_back(&item.second);
    }
    std::sort(learned.begin(), learned.end(), [](const Entry *a, const Entry *b) {
        if (a->count != b->count) return a->count < b->count;
        return a->lastUsed < b->lastUsed;
    });
    // Make room for a tenth more, so pruning does not run on every word written.
    const size_t target = kMaxEntries - kMaxEntries / 10;
    std::vector<Key> dropped;
    for (const Entry *entry : learned) {
        if (m_entries.size() - dropped.size() <= target) break;
        dropped.emplace_back(entry->text, entry->languages);
    }
    for (const Key &key : dropped) m_entries.erase(key);
}

std::vector<PersonalDictionary::Entry> PersonalDictionary::Entries() const {
    std::vector<Entry> entries;
    entries.reserve(m_entries.size());
    for (const auto &item : m_entries) entries.push_back(item.second);
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.count != b.count) return a.count > b.count;
        return a.manual && !b.manual;
    });
    return entries;
}

unsigned PersonalDictionary::Weight(const Entry &entry) {
    if (entry.manual) return std::min(kManualWeight + entry.count, kMaxWeight);
    const bool phrase = entry.text.find(' ') != std::string::npos;
    if (entry.count < (phrase ? 3u : 2u)) return 0;
    return std::min(entry.count, kMaxWeight);
}

std::string PersonalDictionary::TrainingText(const std::string &languages) const {
    std::string out;
    for (const auto &item : m_entries) {
        const Entry &entry = item.second;
        if (!entry.languages.empty() && !LanguagesIntersect(entry.languages, languages)) continue;
        const unsigned weight = Weight(entry);
        if (weight == 0) continue;
        // A new context holding a space, then the entry and the space after it.
        std::string once = std::string(kEscape) + "#" + kSpaceSymbol + "#";
        once += ToSymbolText(entry.text);
        once += kSpaceSymbol;
        for (unsigned i = 0; i < weight; ++i) out += once;
    }
    return out;
}

bool SplitTrainingSegment(const std::string &segment, std::string &context, std::string &text) {
    const size_t escape = sizeof(kEscape) - 1;
    context.clear();
    size_t start = 0;
    bool named = false;
    if (segment.compare(0, escape, kEscape) == 0 && segment.compare(escape, escape, kEscape) != 0
        && segment.size() > escape) {
        const std::string delimiter = segment.substr(escape, charLength(segment, escape));
        const size_t contextStart = escape + delimiter.size();
        const size_t contextEnd = segment.find(delimiter, contextStart);
        if (contextEnd != std::string::npos) {
            context = segment.substr(contextStart, contextEnd - contextStart);
            start = contextEnd + delimiter.size();
            named = true;
        }
    }
    text.clear();
    for (size_t i = start; i < segment.size();) {
        // A written "§" is doubled.
        if (segment.compare(i, escape, kEscape) == 0 && segment.compare(i + escape, escape, kEscape) == 0) {
            i += escape;
        }
        const size_t length = charLength(segment, i);
        text.append(segment, i, length);
        i += length;
    }
    return named;
}

std::string LearnableText(const std::string &symbolContext, const std::string &symbolText) {
    const std::string context = ToPlainText(symbolContext);
    const std::string text = ToPlainText(symbolText);
    if (context.empty() || text.empty() || !isWordChar(text, 0)) return text;

    // The word the text continues starts after the last character of the context that is not
    // part of a word.
    size_t wordStart = 0;
    size_t symbols = 0;
    for (size_t i = 0; i < context.size(); i += charLength(context, i)) {
        ++symbols;
        if (!isWordChar(context, i)) wordStart = i + charLength(context, i);
    }
    if (wordStart == context.size()) return text;   // The context ends a word; the text starts one.
    if (wordStart > 0 || symbols < kContextSymbols) {
        return context.substr(wordStart) + text;
    }
    // The context is all one word and may be cut short: leave the continued word out.
    size_t end = 0;
    while (end < text.size() && isWordChar(text, end)) end += charLength(text, end);
    return text.substr(end);
}

}

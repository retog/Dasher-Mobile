#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

/**
 * @file PersonalDictionary.h
 * @brief The user's own frequent words and phrases, kept across sessions.
 *
 * DasherCore's language model is rebuilt from training text every time a session starts.  The
 * personal dictionary counts the words and short phrases the user writes, and adds the frequent
 * ones to that training text, weighted by how often they were written, so the boxes of the
 * user's own vocabulary grow from session to session.  The user can also add entries by hand.
 *
 * An entry is tied to the language set that was active when it was learned ("de+en"), and is
 * used whenever a session shares a language with it.  An entry with no language is used with
 * every language; entries added by hand may be either.
 */

namespace DasherAndroid {

class PersonalDictionary {
public:
    struct Entry {
        std::string text;        ///< A word, or two to three words separated by single spaces.
        std::string languages;   ///< Canonical language set, or empty for every language.
        unsigned count = 0;      ///< Times written.
        bool manual = false;     ///< Added by the user, never dropped to make room.
        long long lastUsed = 0;  ///< Seconds since the epoch.
    };

    /// Most entries kept; beyond it the least used learned entries are dropped.
    static constexpr size_t kMaxEntries = 4000;

    /// Reads the entries from @p path; a missing or unreadable file leaves the dictionary empty.
    void Load(const std::string &path);

    /// Writes the entries to the path given to @ref Load.  @return Whether that succeeded.
    bool Save() const;

    /**
     * @brief Counts the words and phrases of text the user has written.
     *
     * A word is a run of characters other than blanks, line breaks and punctuation, at least
     * two characters long.  A phrase is two or three words in a row separated by single spaces.
     *
     * @param text      Plain text.
     * @param languages Canonical language set the text was written with.
     * @param now       Seconds since the epoch.
     */
    void Learn(const std::string &text, const std::string &languages, long long now);

    /// Adds an entry by hand, or marks an existing one as added by hand.
    /// @return False if @p text holds nothing to add.
    bool Add(const std::string &text, const std::string &languages, long long now);

    /// @return Whether the entry existed.
    bool Remove(const std::string &text, const std::string &languages);

    /// Drops the learned entries; with @p manualToo also those added by hand.
    void Clear(bool manualToo);

    /// All entries, the most often written first.
    std::vector<Entry> Entries() const;

    /**
     * @brief How many times an entry is repeated in the training text.
     *
     * Zero until a word has been written twice and a phrase three times; entries added by hand
     * always count.
     */
    static unsigned Weight(const Entry &entry);

    /**
     * @brief Training text for a session with the given languages, in DasherCore's training
     *        file format: every entry that applies, as often as its weight, each after a word
     *        boundary and followed by one.  Characters are plain; the caller converts spaces.
     */
    std::string TrainingText(const std::string &languages) const;

private:
    using Key = std::pair<std::string, std::string>;   ///< (text, languages)

    void count(const std::string &text, const std::string &languages, long long now);
    void prune();

    std::string m_path;
    std::map<Key, Entry> m_entries;
};

/**
 * @brief Splits a segment DasherCore appends to the user training file into the context it was
 *        written after and the text itself, undoing the escaping of the context-switch
 *        character "§".  Both are returned as written, i.e. as symbol text.
 *
 * @return Whether the segment names its context.  One that does not continues the segment
 *         written before it.
 */
bool SplitTrainingSegment(const std::string &segment, std::string &context, std::string &text);

/**
 * @brief The plain text to learn words from, given a training segment's context and text.
 *
 * If the text continues a word begun in the context, the beginning is put back in front of it;
 * if the context may not hold the whole beginning, the continued word is left out.
 */
std::string LearnableText(const std::string &context, const std::string &text);

}

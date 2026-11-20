#pragma once

#include <string>
#include <vector>

/**
 * @file LanguageSet.h
 * @brief The languages Dasher predicts for, and the alphabet generated for a set of them.
 *
 * A session predicts for one or several languages at once.  DasherCore knows one alphabet and
 * one language model per session, so a set of languages is turned into a single alphabet that
 * holds the letters of all of them, and its language model is trained on the text of all of
 * them (see AndroidFileUtils.cpp).  The alphabet is generated instead of read from the bundled
 * alphabet files, so every combination of languages has one.
 *
 * A language set is written as its codes in alphabetical order joined by '+', e.g. "de+en".
 * That string names the alphabet, tags the text learned while the set was active, and tags
 * personal dictionary entries.
 */

namespace DasherAndroid {

/// The codes of all languages an alphabet can be generated for, in alphabetical order.
/// DasherLanguage in DasherEngine.kt lists the same codes.
std::vector<std::string> SupportedLanguages();

/**
 * @brief Parses a language set: codes separated by '+', ',' or blanks.
 * @return The known codes, alphabetically and without duplicates; {"en"} if there is none.
 */
std::vector<std::string> ParseLanguages(const std::string &languages);

/// @return The codes joined by '+'.  Given the result of @ref ParseLanguages, the canonical
///         name of the set.
std::string JoinLanguages(const std::vector<std::string> &languages);

/// @return Whether two language sets (canonical or not) share a language.
bool LanguagesIntersect(const std::string &a, const std::string &b);

/**
 * @brief The DasherCore alphabet ID for a language set.
 *
 * English and Slovak alone keep the IDs of the alphabets that used to be bundled for them
 * ("English with limited punctuation", "Slovak"), which KenLM model files are named after.
 */
std::string AlphabetIdFor(const std::vector<std::string> &languages);

/// The file text written with this language set is learned to, e.g. "user_text_de+en.txt".
std::string UserTextFileFor(const std::vector<std::string> &languages);

/// Whether @p fileName is a file made by @ref UserTextFileFor; if so @p languages receives
/// the language set in its name.
bool IsUserTextFile(const std::string &fileName, std::string *languages = nullptr);

/// Bundled training corpus (an asset under "training/") of a language; empty if it has none.
std::string CorpusAssetFor(const std::string &language);

/// File in the user data directory a training corpus for a language is imported to.
std::string ImportedCorpusFileFor(const std::string &language);

/// User training file DasherCore wrote for a language before language sets existed; empty
/// if there was none.
std::string LegacyUserTextFileFor(const std::string &language);

/**
 * @brief Generates the alphabet XML for a language set.
 *
 * The letters of all languages are merged into one lower-case and one upper-case group, each
 * accented letter next to its base letter, followed by punctuation and the "¶" / "□" symbols
 * for newline and space (see SymbolText.h).
 */
std::string AlphabetXml(const std::vector<std::string> &languages);

}

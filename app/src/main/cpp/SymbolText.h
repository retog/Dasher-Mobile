#pragma once

#include <string>

/**
 * @file SymbolText.h
 * @brief Conversion between plain text and the symbol text DasherCore alphabets produce.
 *
 * The bundled alphabets declare space as @c <node label="□"><textCharAction unicode="32"/>
 * and newline as @c <node label="¶">.  DasherCore's alphabet parser ignores the @c unicode
 * attribute, so these symbols output their labels "□" and "¶", and a real space or newline
 * in a training corpus matches no symbol at all (the language model silently skips it).
 *
 * Rather than changing the symbols (KenLM models look symbols up by this text), corpora are
 * converted to symbol text when loaded, and engine output is converted back to plain text
 * before it reaches a host.
 */

namespace DasherAndroid {

/**
 * @brief Prepares a plain-text training corpus: each run of spaces and tabs becomes one "□",
 *        and each run of line breaks starts a new text.
 *
 * Every line of the bundled corpora is a paragraph.  A line break becomes DasherCore's
 * context-switch escape, the same marker it writes before each new text in the user training
 * file, so paragraph starts are learned in the default context every text starts in.  Line
 * breaks are not learned as "¶": that would make Enter one of the largest boxes after every
 * sentence, so "¶" is learned only from the user's own text.
 */
std::string CorpusToSymbolText(const std::string &corpus);

/**
 * @brief Converts alphabet symbol text to plain text: "□" → ' ', "¶" → '\n'.
 */
std::string ToPlainText(const std::string &symbolText);

/**
 * @brief Converts plain text from an editor to alphabet symbol text: ' ' → "□", and each line
 *        break ("\r\n", "\n" or a lone "\r") → "¶".  The inverse of @ref ToPlainText.
 */
std::string ToSymbolText(const std::string &plain);

}

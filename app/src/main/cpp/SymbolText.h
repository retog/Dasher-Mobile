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
 * @brief Prepares a plain-text training corpus: each run of spaces and line breaks becomes
 *        one "□".
 *
 * Line breaks in the bundled corpora end paragraphs of prose.  Learning them as "¶" would make
 * Enter one of the largest boxes after every sentence, and in the keyboard Enter can trigger
 * the editor's send or search action, so "¶" is learned only from the user's own text.
 */
std::string CorpusToSymbolText(const std::string &corpus);

/**
 * @brief Converts alphabet symbol text to plain text: "□" → ' ', "¶" → '\n'.
 */
std::string ToPlainText(const std::string &symbolText);

}

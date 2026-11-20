#pragma once

#include "PersonalDictionary.h"

#include <string>

struct AAssetManager;

/**
 * @file AndroidFiles.h
 * @brief What the file layer (AndroidFileUtils.cpp) is told by, and offers to, the JNI bridge.
 *
 * DasherCore reads alphabets and training text and writes learned text through
 * @c Dasher::FileUtils, which this app implements.  The functions here say where files live
 * and which languages the next session is created for.
 */

namespace DasherAndroid {

void SetAssetManager(AAssetManager *assetManager);

/// Sets the directory user files live in and loads the personal dictionary from it.
void SetUserDataDir(const std::string &userDataDir);

/**
 * @brief Sets the languages of the session created next (see LanguageSet.h).
 *
 * The alphabet a session reads and the text it is trained on depend on it, so it must be set
 * before the session is realised and not changed while the session lives.
 *
 * @return The canonical language set.
 */
std::string SetLanguages(const std::string &languages);

/// The canonical language set last given to @ref SetLanguages.
std::string Languages();

/// The personal dictionary in the user data directory.
PersonalDictionary &Dictionary();

/// Deletes all text learned from the user and the learned dictionary entries.  Sessions
/// created afterwards start without it.
void ForgetLearnedText();

}

#include "DasherCore/FileUtils.h"
#include "AndroidFiles.h"
#include "LanguageSet.h"
#include "PersonalDictionary.h"
#include "SymbolText.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <dirent.h>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <ios>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#define LOG_TAG "DasherFileUtils"

namespace {

AAssetManager *g_assetManager = nullptr;
std::string g_userDataDir;
/// Languages of the session being created, canonical (see LanguageSet.h).
std::vector<std::string> g_languages = {"en"};
DasherAndroid::PersonalDictionary g_dictionary;
/// The last symbols of the learned text written last, the context of a segment without one.
std::string g_lastLearnedTail;

constexpr char kAlphabetPattern[] = "alphabet.*.xml";
constexpr char kDictionaryFile[] = "personal_dictionary.tsv";
/// The training text of a session, put together from all its sources.
constexpr char kTrainingCacheFile[] = "training_cache.txt";
/// DasherCore's context-switch escape with an empty context: a new text starts.
constexpr char kNewText[] = "\xC2\xA7##";
/// DasherCore gives a training segment at most this many symbols of context.
constexpr size_t kContextSymbols = 5;

std::string joinPath(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

std::string resolvePath(const std::string &path) {
    if (path.empty()) return path;
    if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) {
        return path;
    }
    if (g_userDataDir.empty()) {
        return path;
    }
    return joinPath(g_userDataDir, path);
}

std::string readAsset(const std::string &assetPath) {
    if (!g_assetManager || assetPath.empty()) return {};
    AAsset *asset = AAssetManager_open(g_assetManager, assetPath.c_str(), AASSET_MODE_BUFFER);
    if (!asset) return {};
    std::string data;
    const off_t len = AAsset_getLength(asset);
    if (len > 0) {
        data.resize(static_cast<size_t>(len));
        const int read = AAsset_read(asset, data.data(), static_cast<size_t>(len));
        data.resize(read > 0 ? static_cast<size_t>(read) : 0);
    }
    AAsset_close(asset);
    return data;
}

std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    std::ostringstream data;
    data << in.rdbuf();
    return data.str();
}

void replaceAll(std::string &text, const std::string &from, const std::string &to) {
    for (size_t pos = 0; (pos = text.find(from, pos)) != std::string::npos; pos += to.size()) {
        text.replace(pos, from.size(), to);
    }
}

std::vector<std::string> userDataFiles() {
    std::vector<std::string> names;
    if (DIR *dir = opendir(g_userDataDir.c_str())) {
        while (const dirent *entry = readdir(dir)) names.emplace_back(entry->d_name);
        closedir(dir);
    }
    return names;
}

/// The last @p count UTF-8 characters of @p text.
std::string lastSymbols(const std::string &text, size_t count) {
    size_t pos = text.size();
    while (pos > 0 && count > 0) {
        --pos;
        if ((static_cast<unsigned char>(text[pos]) & 0xC0) != 0x80) --count;
    }
    return text.substr(pos);
}

/**
 * Trains the language model of a session for the current language set: on the corpus of each
 * of its languages, on all text learned while one of its languages was active, and on the
 * frequent entries of the personal dictionary.
 *
 * The parts are put into one file, as the trainer measures its progress against a file's size.
 */
void trainForLanguages(AbstractParser *parser) {
    const std::string languages = DasherAndroid::JoinLanguages(g_languages);
    std::string training;
    const auto add = [&training](const std::string &symbolText) {
        if (symbolText.empty()) return;
        if (!training.empty()) training += kNewText;
        training += symbolText;
    };

    for (const std::string &language : g_languages) {
        // Corpora separate words with real spaces and line breaks, but the alphabet types "□"
        // (see SymbolText.h); without converting, the language model never sees a word gap.
        const std::string asset = DasherAndroid::CorpusAssetFor(language);
        if (!asset.empty()) add(DasherAndroid::CorpusToSymbolText(readAsset("training/" + asset)));
        add(DasherAndroid::CorpusToSymbolText(
            readFile(resolvePath(DasherAndroid::ImportedCorpusFileFor(language)))));

        // Text learned before language sets existed, already symbol text.
        const std::string legacy = DasherAndroid::LegacyUserTextFileFor(language);
        if (!legacy.empty()) {
            std::string text = readFile(resolvePath(legacy));
            if (language == "en") {
                // The English alphabet then typed its labels for the full stop and apostrophe.
                replaceAll(text, "\xC2\xB7", ".");
                replaceAll(text, "\xE2\x97\x8C\xCC\x93", "'");
            }
            add(text);
        }
    }
    for (const std::string &name : userDataFiles()) {
        std::string writtenWith;
        if (DasherAndroid::IsUserTextFile(name, &writtenWith)
            && DasherAndroid::LanguagesIntersect(writtenWith, languages)) {
            add(readFile(resolvePath(name)));
        }
    }
    add(g_dictionary.TrainingText(languages));

    const std::string cachePath = resolvePath(kTrainingCacheFile);
    if (training.empty()) {
        std::remove(cachePath.c_str());
        return;
    }
    {
        std::ofstream cache(cachePath, std::ios::binary | std::ios::trunc);
        cache << training;
        if (!cache.good()) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "cannot write %s", cachePath.c_str());
            return;
        }
    }
    const bool ok = parser->ParseFile(cachePath, true);
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "trained for %s on %zu bytes ok=%d",
                        languages.c_str(), training.size(), ok ? 1 : 0);
}

/// Counts the words of a segment of learned text in the personal dictionary.
void learnWords(const std::string &segment, const std::string &languages) {
    std::string context;
    std::string text;
    if (!DasherAndroid::SplitTrainingSegment(segment, context, text)) {
        context = g_lastLearnedTail;
    }
    g_lastLearnedTail = lastSymbols(context + text, kContextSymbols);
    g_dictionary.Learn(DasherAndroid::LearnableText(context, text), languages,
                       static_cast<long long>(std::time(nullptr)));
    g_dictionary.Save();
}

bool parseAsset(AbstractParser *parser, const std::string &assetPath) {
    if (!parser) return false;
    const std::string data = readAsset(assetPath);
    if (data.empty()) return false;
    std::istringstream stream(data);
    const bool ok = parser->Parse(assetPath, stream, false);
    if (ok) {
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "parsed asset: %s (%zu bytes)", assetPath.c_str(), data.size());
    }
    return ok;
}

void scanAssetDir(AbstractParser *parser,
                  const std::string &dir,
                  const std::regex &pattern) {
    if (!g_assetManager || !parser) return;
    AAssetDir *assetDir = AAssetManager_openDir(g_assetManager, dir.c_str());
    if (!assetDir) return;

    const char *entry = nullptr;
    while ((entry = AAssetDir_getNextFileName(assetDir)) != nullptr) {
        std::string fileName(entry);
        if (!std::regex_search(fileName, pattern)) continue;
        const std::string path = dir.empty() ? fileName : joinPath(dir, fileName);
        parseAsset(parser, path);
    }

    AAssetDir_close(assetDir);
}

}

namespace DasherAndroid {

void SetAssetManager(AAssetManager *assetManager) {
    g_assetManager = assetManager;
}

void SetUserDataDir(const std::string &userDataDir) {
    if (g_userDataDir == userDataDir) return;
    g_userDataDir = userDataDir;
    g_dictionary.Load(joinPath(userDataDir, kDictionaryFile));
}

std::string SetLanguages(const std::string &languages) {
    g_languages = ParseLanguages(languages);
    return JoinLanguages(g_languages);
}

std::string Languages() {
    return JoinLanguages(g_languages);
}

PersonalDictionary &Dictionary() {
    return g_dictionary;
}

void ForgetLearnedText() {
    for (const std::string &name : userDataFiles()) {
        if (IsUserTextFile(name)) std::remove(resolvePath(name).c_str());
    }
    for (const std::string &language : SupportedLanguages()) {
        const std::string legacy = LegacyUserTextFileFor(language);
        if (!legacy.empty()) std::remove(resolvePath(legacy).c_str());
    }
    // The training text of the last session holds a copy of what was learned.
    std::remove(resolvePath(kTrainingCacheFile).c_str());
    g_lastLearnedTail.clear();
    g_dictionary.Clear(false);
    g_dictionary.Save();
}

}

int Dasher::FileUtils::GetFileSize(const std::string &strFileName) {
    std::ifstream f(resolvePath(strFileName), std::ios::binary | std::ios::ate);
    if (!f.is_open()) return 0;
    return static_cast<int>(f.tellg());
}

void Dasher::FileUtils::ScanFiles(AbstractParser *parser, const std::string &strPattern) {
    if (!parser) return;

    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "ScanFiles: pattern=%s", strPattern.c_str());

    // The only alphabet a session knows is the one generated for its languages.
    if (strPattern == kAlphabetPattern) {
        std::istringstream xml(DasherAndroid::AlphabetXml(g_languages));
        parser->Parse("alphabet for " + DasherAndroid::JoinLanguages(g_languages), xml, false);
        return;
    }
    // That alphabet names the file its text is learned to as its training file.
    if (DasherAndroid::IsUserTextFile(strPattern)) {
        trainForLanguages(parser);
        return;
    }

    std::ifstream local(resolvePath(strPattern), std::ios::binary);
    if (local.is_open()) {
        local.close();
        const std::string localPath = resolvePath(strPattern);
        const bool parsedLocal = parser->ParseFile(localPath, true);
        __android_log_print(ANDROID_LOG_DEBUG,
                            LOG_TAG,
                            "parsed local: %s ok=%d",
                            localPath.c_str(),
                            parsedLocal ? 1 : 0);
    }

    std::regex pattern(strPattern);
    scanAssetDir(parser, "", pattern);
    scanAssetDir(parser, "colors", pattern);
    scanAssetDir(parser, "settings", pattern);
}

bool Dasher::FileUtils::WriteUserDataFile(const std::string &filename,
                                           const std::string &strNewText,
                                           bool append) {
    std::ofstream f(resolvePath(filename), append ? std::ios::app : std::ios::trunc);
    if (!f.is_open()) return false;
    f << strNewText;
    std::string languages;
    if (DasherAndroid::IsUserTextFile(filename, &languages)) {
        learnWords(strNewText, languages);
    }
    return f.good();
}

std::string Dasher::FileUtils::GetFullFilenamePath(const std::string strFilename) {
    return resolvePath(strFilename);
}

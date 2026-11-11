/**
 * @file native_tests.cpp
 * @brief Host tests of the native layer: language sets, the personal dictionary and steering
 *        into a symbol's box, the latter two against a real DasherCore session.
 */

#include "AndroidDasherInterface.h"
#include "AndroidFiles.h"
#include "LanguageSet.h"
#include "PersonalDictionary.h"

#include <android/asset_manager.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            ++g_failures;                                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << ": failed: " #condition << std::endl; \
        }                                                                                   \
    } while (false)

#define CHECK_EQ(actual, expected)                                                          \
    do {                                                                                    \
        const auto actualValue = (actual);                                                  \
        const auto expectedValue = (expected);                                              \
        if (!(actualValue == expectedValue)) {                                              \
            ++g_failures;                                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #actual " is \"" << actualValue  \
                      << "\", expected \"" << expectedValue << "\"" << std::endl;           \
        }                                                                                   \
    } while (false)

using DasherAndroid::PersonalDictionary;

bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream data;
    data << in.rdbuf();
    return data.str();
}

std::string makeTempDir() {
    char pattern[] = "/tmp/dasher_native_tests_XXXXXX";
    const char *dir = mkdtemp(pattern);
    if (!dir) {
        std::cerr << "cannot create a temporary directory" << std::endl;
        std::exit(2);
    }
    return dir;
}

unsigned countOf(const PersonalDictionary &dictionary, const std::string &text, const std::string &languages) {
    for (const auto &entry : dictionary.Entries()) {
        if (entry.text == text && entry.languages == languages) return entry.count;
    }
    return 0;
}

void testLanguageSets() {
    using namespace DasherAndroid;
    CHECK_EQ(JoinLanguages(ParseLanguages("sk+de")), std::string("de+sk"));
    CHECK_EQ(JoinLanguages(ParseLanguages("en, de,en")), std::string("de+en"));
    CHECK_EQ(JoinLanguages(ParseLanguages("xx")), std::string("en"));
    CHECK_EQ(JoinLanguages(ParseLanguages("")), std::string("en"));

    CHECK(LanguagesIntersect("de+en", "en"));
    CHECK(LanguagesIntersect("fr", "de+fr+it"));
    CHECK(!LanguagesIntersect("de+en", "sk"));
    CHECK(!LanguagesIntersect("", "en"));

    // English and Slovak alone keep the alphabet IDs their KenLM model files are named after.
    CHECK_EQ(AlphabetIdFor({"en"}), std::string("English with limited punctuation"));
    CHECK_EQ(AlphabetIdFor({"sk"}), std::string("Slovak"));
    CHECK_EQ(AlphabetIdFor({"de", "en"}), std::string("Dasher Mobile de+en"));

    CHECK_EQ(UserTextFileFor({"de", "en"}), std::string("user_text_de+en.txt"));
    std::string languages;
    CHECK(IsUserTextFile("user_text_de+en.txt", &languages));
    CHECK_EQ(languages, std::string("de+en"));
    CHECK(!IsUserTextFile("training_slovak_SK.txt"));
    CHECK(!IsUserTextFile("user_text_.txt"));

    const std::string english = AlphabetXml({"en"});
    CHECK(contains(english, "label=\"a\""));
    CHECK(contains(english, "label=\"Z\""));
    CHECK(!contains(english, "ß"));
    CHECK(contains(english, "label=\"&quot;\""));
    CHECK(contains(english, "trainingFilename=\"user_text_en.txt\""));

    const std::string mixed = AlphabetXml({"de", "sk"});
    CHECK(contains(mixed, "label=\"ß\""));
    CHECK(contains(mixed, "label=\"š\""));
    CHECK(contains(mixed, "label=\"Ü\""));
    CHECK(!contains(mixed, "label=\"ñ\""));
    // An accented letter follows its base letter: a á ä b.
    CHECK(mixed.find("label=\"a\"") < mixed.find("label=\"á\""));
    CHECK(mixed.find("label=\"á\"") < mixed.find("label=\"ä\""));
    CHECK(mixed.find("label=\"ä\"") < mixed.find("label=\"b\""));

    CHECK(contains(AlphabetXml({"es"}), "label=\"¿\""));
    CHECK(!contains(AlphabetXml({"en"}), "label=\"¿\""));
}

void testDictionaryLearning() {
    PersonalDictionary dictionary;
    dictionary.Learn("see you at the Hauptbahnhof, see you soon", "de+en", 100);
    CHECK_EQ(countOf(dictionary, "see", "de+en"), 2u);
    CHECK_EQ(countOf(dictionary, "Hauptbahnhof", "de+en"), 1u);
    CHECK_EQ(countOf(dictionary, "see you", "de+en"), 2u);
    CHECK_EQ(countOf(dictionary, "see you at", "de+en"), 1u);
    // A comma ends a phrase, and a one-letter word is no entry of its own.
    CHECK_EQ(countOf(dictionary, "Hauptbahnhof see", "de+en"), 0u);
    dictionary.Learn("a b", "en", 100);
    CHECK_EQ(countOf(dictionary, "a", "en"), 0u);
    CHECK_EQ(countOf(dictionary, "a b", "en"), 1u);
    // Apostrophes and hyphens inside a word belong to it; accented letters are letters.
    dictionary.Learn("don't e-mail 'Zürich'", "en", 100);
    CHECK_EQ(countOf(dictionary, "don't", "en"), 1u);
    CHECK_EQ(countOf(dictionary, "e-mail", "en"), 1u);
    CHECK_EQ(countOf(dictionary, "Zürich", "en"), 1u);
    // Entries are kept per language set.
    CHECK_EQ(countOf(dictionary, "see", "en"), 0u);
}

void testDictionaryWeights() {
    PersonalDictionary::Entry word;
    word.text = "Zürich";
    word.count = 1;
    CHECK_EQ(PersonalDictionary::Weight(word), 0u);
    word.count = 2;
    CHECK_EQ(PersonalDictionary::Weight(word), 2u);
    word.count = 500;
    CHECK_EQ(PersonalDictionary::Weight(word), 16u);

    PersonalDictionary::Entry phrase;
    phrase.text = "see you";
    phrase.count = 2;
    CHECK_EQ(PersonalDictionary::Weight(phrase), 0u);
    phrase.count = 3;
    CHECK_EQ(PersonalDictionary::Weight(phrase), 3u);

    PersonalDictionary::Entry manual;
    manual.text = "Retinue";
    manual.manual = true;
    CHECK_EQ(PersonalDictionary::Weight(manual), 4u);
}

void testDictionaryTrainingText() {
    PersonalDictionary dictionary;
    dictionary.Learn("Zürich Zürich", "de", 1);
    dictionary.Learn("Bratislava Bratislava", "sk", 1);
    dictionary.Learn("once", "de", 1);
    CHECK(dictionary.Add("  Retinue ", "", 1));
    CHECK(!dictionary.Add(" ,. ", "", 1));

    const std::string german = dictionary.TrainingText("de+en");
    // Each use starts a new context holding a space, and ends with one: "§#□#Zürich□".
    CHECK(contains(german, "\xC2\xA7#\xE2\x96\xA1#Zürich\xE2\x96\xA1"));
    CHECK(contains(german, "Retinue"));       // No language: used with every language.
    CHECK(!contains(german, "Bratislava"));   // Learned with Slovak only.
    CHECK(!contains(german, "once"));         // Written once: not frequent.
    CHECK(contains(dictionary.TrainingText("sk"), "Bratislava"));
    CHECK(contains(dictionary.TrainingText("sk"), "Retinue"));
    CHECK(!contains(dictionary.TrainingText("sk"), "Zürich"));
}

void testDictionaryPersistence(const std::string &dir) {
    const std::string path = dir + "/dictionary_test.tsv";
    {
        PersonalDictionary dictionary;
        dictionary.Load(path);
        dictionary.Learn("guten Morgen guten Morgen", "de", 42);
        dictionary.Add("Retinue", "", 43);
        CHECK(dictionary.Save());
    }
    PersonalDictionary reloaded;
    reloaded.Load(path);
    CHECK_EQ(countOf(reloaded, "guten Morgen", "de"), 2u);
    CHECK_EQ(countOf(reloaded, "guten", "de"), 2u);
    bool manualKept = false;
    for (const auto &entry : reloaded.Entries()) {
        if (entry.text == "Retinue") manualKept = entry.manual && entry.languages.empty() && entry.lastUsed == 43;
    }
    CHECK(manualKept);

    CHECK(reloaded.Remove("guten", "de"));
    CHECK(!reloaded.Remove("guten", "de"));
    reloaded.Clear(false);
    CHECK_EQ(reloaded.Entries().size(), size_t(1));   // What the user added stays.
    reloaded.Clear(true);
    CHECK(reloaded.Entries().empty());
}

void testDictionaryPruning() {
    PersonalDictionary dictionary;
    dictionary.Add("keepme", "", 0);
    dictionary.Learn("often often often", "en", 1);
    for (size_t i = 0; i < PersonalDictionary::kMaxEntries + 50; ++i) {
        dictionary.Learn("w" + std::to_string(i) + "x", "en", 2);
    }
    CHECK(dictionary.Entries().size() <= PersonalDictionary::kMaxEntries);
    CHECK_EQ(countOf(dictionary, "often", "en"), 3u);
    bool manualKept = false;
    for (const auto &entry : dictionary.Entries()) manualKept = manualKept || entry.text == "keepme";
    CHECK(manualKept);
}

void testTrainingSegments() {
    using namespace DasherAndroid;
    std::string context;
    std::string text;
    // "§#o□#you□soon": written after "o ", in symbol text.
    CHECK(SplitTrainingSegment("\xC2\xA7#o\xE2\x96\xA1#you\xE2\x96\xA1soon", context, text));
    CHECK_EQ(context, std::string("o\xE2\x96\xA1"));
    CHECK_EQ(text, std::string("you\xE2\x96\xA1soon"));
    CHECK_EQ(LearnableText(context, text), std::string("you soon"));

    // No context named: the segment continues the one before.
    CHECK(!SplitTrainingSegment("more", context, text));
    CHECK_EQ(text, std::string("more"));
    // A written "§" is doubled in the file.
    CHECK(!SplitTrainingSegment("\xC2\xA7\xC2\xA7 5", context, text));
    CHECK_EQ(text, std::string("\xC2\xA7 5"));

    // The text continues a word whose beginning the context holds.
    CHECK_EQ(LearnableText("a\xE2\x96\xA1Zür", "ich\xE2\x96\xA1ist"), std::string("Zürich ist"));
    CHECK_EQ(LearnableText("Zür", "ich"), std::string("Zürich"));
    // Five symbols of one word may not be all of it: the continued word is left out.
    CHECK_EQ(LearnableText("bahnh", "of\xE2\x96\xA1ist"), std::string(" ist"));
    // The context ends a word: nothing to join.
    CHECK_EQ(LearnableText("see\xE2\x96\xA1", "you"), std::string("you"));
}

/// Renders frames until a move started by SteerToSymbol / SteerBack is over.
void finishSteering(AndroidDasherInterface &dasher, long &timeMs) {
    for (int frame = 0; frame < 2000 && dasher.IsSteering(); ++frame) {
        dasher.Frame(timeMs += 16);
        dasher.TakeFrameStrings();
    }
    CHECK(!dasher.IsSteering());
    // DasherCore asks for one more frame after it moved.
    dasher.Frame(timeMs += 16);
    dasher.TakeFrameStrings();
}

void steer(AndroidDasherInterface &dasher, long &timeMs, const std::string &text) {
    for (size_t i = 0; i < text.size();) {
        size_t next = i + 1;
        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
        const std::string symbol = text.substr(i, next - i);
        if (!dasher.SteerToSymbol(symbol)) {
            ++g_failures;
            std::cerr << "cannot steer to \"" << symbol << "\"" << std::endl;
            return;
        }
        finishSteering(dasher, timeMs);
        i = next;
    }
}

/// Steers through a longer text, checking after every frame that nothing but the letters asked
/// for was written, not even in passing.
void testSteeringWritesOnlyWhatIsAsked(const std::string &dir) {
    const std::string text =
        "Quer durch Zürich: fünf Bären (groß), Öl & Käse? Nie! "
        "Ťažký kôň šiel dnu; žltá ruža \"kvitne\" - vraj. "
        "Xylophon, Jazz, Whisky; qqq zzz ÄÖÜ";
    AndroidDasherInterface dasher(dir, "de+sk");
    dasher.SetScreenSize(1080, 700);
    long timeMs = 500000;
    dasher.Frame(timeMs += 16);
    dasher.TakeFrameStrings();

    std::string written;
    int frames = 0;
    int letters = 0;
    for (size_t i = 0; i < text.size();) {
        size_t next = i + 1;
        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
        const std::string symbol = text.substr(i, next - i);
        i = next;
        if (symbol == "&") {
            CHECK(!dasher.SteerToSymbol(symbol));   // Not in the alphabet.
            continue;
        }
        if (!dasher.SteerToSymbol(symbol)) {
            ++g_failures;
            std::cerr << "cannot steer to \"" << symbol << "\" after \"" << written << "\"" << std::endl;
            return;
        }
        ++letters;
        for (int frame = 0; frame < 2000 && dasher.IsSteering(); ++frame) {
            dasher.Frame(timeMs += 16);
            dasher.TakeFrameStrings();
            ++frames;
            const std::string now = dasher.GetOutputText();
            if (now != written && now != written + symbol) {
                ++g_failures;
                std::cerr << "wrote \"" << now << "\" on the way to \"" << symbol << "\"" << std::endl;
                return;
            }
        }
        written += symbol;
        CHECK_EQ(dasher.GetOutputText(), written);
    }
    // A letter takes a zoom or two, well under a second at 60 frames a second.
    CHECK(frames < letters * 40);
    if (std::getenv("DASHER_TEST_VERBOSE")) {
        std::cout << letters << " letters in " << frames << " frames" << std::endl;
    }
}

void testSession(const std::string &dir) {
    long timeMs = 1000;
    {
        AndroidDasherInterface dasher(dir, "sk+de");
        CHECK_EQ(dasher.GetLanguages(), std::string("de+sk"));
        CHECK(!dasher.SteerToSymbol("a"));   // Not realised yet.
        dasher.SetScreenSize(800, 600);
        CHECK(!dasher.Frame(timeMs += 16).empty());
        dasher.TakeFrameStrings();

        // Each spoken letter is written, whatever its box's size: letters of both languages,
        // upper case, punctuation and the space.
        steer(dasher, timeMs, "Tschüß, žaba");
        CHECK_EQ(dasher.GetOutputText(), std::string("Tschüß, žaba"));
        CHECK(!dasher.SteerToSymbol("ñ"));   // Not a letter of these languages.
        CHECK(!dasher.SteerToSymbol("7"));

        // Moving back deletes the letter written last.
        CHECK(dasher.SteerBack());
        finishSteering(dasher, timeMs);
        CHECK_EQ(dasher.GetOutputText(), std::string("Tschüß, žab"));
        steer(dasher, timeMs, "y žaby žaby");
        CHECK_EQ(dasher.GetOutputText(), std::string("Tschüß, žaby žaby žaby"));

        // Text loaded from an editor can be moved back into and continued.
        CHECK_EQ(dasher.SetContext("Hallo"), std::string("Hallo"));
        dasher.Frame(timeMs += 16);
        dasher.TakeFrameStrings();
        CHECK(dasher.SteerBack());
        finishSteering(dasher, timeMs);
        CHECK_EQ(dasher.GetOutputText(), std::string("Hall"));
        CHECK(dasher.SteerBack());
        finishSteering(dasher, timeMs);
        CHECK_EQ(dasher.GetOutputText(), std::string("Hal"));
        steer(dasher, timeMs, "lo du");
        CHECK_EQ(dasher.GetOutputText(), std::string("Hallo du"));

        // Back to the very start of a text, and no further.
        dasher.ResetOutputText();
        dasher.Frame(timeMs += 16);
        dasher.TakeFrameStrings();
        CHECK(!dasher.SteerBack());
        steer(dasher, timeMs, "ab");
        CHECK(dasher.SteerBack());
        finishSteering(dasher, timeMs);
        CHECK(dasher.SteerBack());
        finishSteering(dasher, timeMs);
        CHECK_EQ(dasher.GetOutputText(), std::string(""));
        CHECK(!dasher.SteerBack());
    }

    // Ending the session saved what was written, tagged with its languages ...
    const std::string learned = readFile(dir + "/user_text_de+sk.txt");
    CHECK(contains(learned, "žaby"));
    CHECK(contains(learned, "Tschüß"));
    // ... and counted its words.
    CHECK_EQ(countOf(DasherAndroid::Dictionary(), "žaby", "de+sk"), 3u);
    CHECK_EQ(countOf(DasherAndroid::Dictionary(), "žaby žaby", "de+sk"), 2u);
    CHECK_EQ(countOf(DasherAndroid::Dictionary(), "Tschüß", "de+sk"), 1u);
    // "Hallo" was loaded from the editor, not written; "lo du" continued it.
    CHECK_EQ(countOf(DasherAndroid::Dictionary(), "Hallo", "de+sk"), 1u);
    CHECK_EQ(countOf(DasherAndroid::Dictionary(), "du", "de+sk"), 1u);

    // A session sharing a language is trained on that text and the frequent words, and on its
    // language's corpus; one sharing none is not.
    {
        AndroidDasherInterface dasher(dir, "sk");
        dasher.SetScreenSize(800, 600);
        dasher.Frame(timeMs += 16);
        const std::string training = readFile(dir + "/training_cache.txt");
        CHECK(contains(training, "Tschüß"));
        CHECK(contains(training, "#žaby\xE2\x96\xA1"));
        CHECK(contains(training, "Drak"));   // From the bundled Slovak corpus.
    }
    std::remove((dir + "/training_cache.txt").c_str());
    {
        AndroidDasherInterface dasher(dir, "en");
        dasher.SetScreenSize(800, 600);
        dasher.Frame(timeMs += 16);
        CHECK(!contains(readFile(dir + "/training_cache.txt"), "žaby"));
        steer(dasher, timeMs, "it's");
        CHECK_EQ(dasher.GetOutputText(), std::string("it's"));
    }

    // Text learned before language sets existed is still read, for its language only.  The
    // English alphabet of then typed "·" for a full stop.
    {
        std::ofstream legacy(dir + "/training_english_GB.txt", std::ios::binary);
        legacy << "\xC2\xA7##well\xC2\xB7\xE2\x96\xA1then";
    }
    {
        AndroidDasherInterface dasher(dir, "en+fr");
        dasher.SetScreenSize(800, 600);
        CHECK(contains(readFile(dir + "/training_cache.txt"), "well.\xE2\x96\xA1then"));
    }
    {
        AndroidDasherInterface dasher(dir, "fr");
        dasher.SetScreenSize(800, 600);
        CHECK(!contains(readFile(dir + "/training_cache.txt"), "then"));
    }

    // An imported training text is read as plain text.
    {
        std::ofstream corpus(dir + "/corpus_fr.txt", std::ios::binary);
        corpus << "Bonjour tout le monde\n";
    }
    {
        AndroidDasherInterface dasher(dir, "fr");
        dasher.SetScreenSize(800, 600);
        CHECK(contains(readFile(dir + "/training_cache.txt"), "Bonjour\xE2\x96\xA1tout"));
    }

    DasherAndroid::ForgetLearnedText();
    CHECK(readFile(dir + "/user_text_de+sk.txt").empty());
    CHECK(readFile(dir + "/user_text_en.txt").empty());
    CHECK(readFile(dir + "/training_english_GB.txt").empty());
    CHECK(readFile(dir + "/training_cache.txt").empty());
    CHECK(!readFile(dir + "/corpus_fr.txt").empty());   // Not learned from the user.
    CHECK(DasherAndroid::Dictionary().Entries().empty());
}

}

int main() {
    const std::string dir = makeTempDir();
    static AAssetManager assets;
    assets.roots = {DASHER_TEST_ASSETS, DASHER_TEST_CORE_DATA};
    DasherAndroid::SetAssetManager(&assets);

    testLanguageSets();
    testDictionaryLearning();
    testDictionaryWeights();
    testDictionaryTrainingText();
    testDictionaryPersistence(dir);
    testDictionaryPruning();
    testTrainingSegments();
    DasherAndroid::SetUserDataDir(dir);
    testSession(dir);
    testSteeringWritesOnlyWhatIsAsked(dir);

    if (g_failures == 0) {
        std::cout << "all native tests passed" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " check(s) failed" << std::endl;
    return 1;
}

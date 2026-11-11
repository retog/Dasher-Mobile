# Dasher Mobile

Dasher Mobile is an Android implementation of the [Dasher](https://www.inference.org.uk/dasher/) probabilistic text input system, designed for efficient and accessible text entry. Users steer a cursor into characters using touch gestures or device tilt; a statistical language model sizes letters by probability, making common letters easier to reach.

The app works as both a **standalone activity** and a system-wide **Input Method Editor (IME)**.

The architecture diagram is available in [`docs/architecture.puml`](docs/architecture.puml).

## Languages

Dasher predicts for one or several languages at once, chosen under **Settings → Languages**: German, English, Spanish, French, Italian and Slovak. With several chosen, the boxes offer the letters of all of them, each accented letter next to its base letter, and one language model sizes them, trained on text of every chosen language. Which language is being written shows in the letters just written, so the model follows a switch of language within a few letters.

A language's model starts from a training text. One is included for Slovak; for the other languages import a plain text file (UTF-8) under **Settings → Training texts** — any long text in the language will do. Without one, a language is learned only from what you write.

A KenLM model belongs to one alphabet, so its file is named after the chosen languages: `kenlm_English with limited punctuation.binary` for English alone, `kenlm_Slovak.binary` for Slovak alone, and otherwise `kenlm_Dasher Mobile ` followed by the language codes, e.g. `kenlm_Dasher Mobile de+en.binary`. KenLM models do not learn.

## Learning your words

What you write is learned, in two ways, both kept across sessions:

- **The text itself** is saved and the language model is trained on it whenever the keyboard starts.
- **A personal dictionary** counts your words and phrases of two or three words. Words written at least twice and phrases written at least three times are added to the training text as often as you wrote them (up to 16 times), so the boxes of your own vocabulary grow.

Both are tied to the languages that were chosen at the time, and are used whenever at least one of those languages is chosen. Under **Settings → Personal dictionary** you can see and delete what was counted, and add words and phrases yourself, either for the chosen languages or for all languages. *Forget everything learned* deletes the saved text and the counted entries; entries you added stay.

Nothing is learned in password fields or where an app asks keyboards not to learn. PPM is the language model that learns.

## Voice input

Two buttons in the top right corner of the Dasher canvas, shown while Dasher is paused, switch a voice mode on and off:

- **Dictation** (microphone): what you say is entered as text, and Dasher continues behind it.
- **Spoken letters** (microphone with an A): say a letter and Dasher scrolls to that letter's box and zooms into it, which writes the letter. Say several letters in a row to write them one after the other.

Spoken letters understands a letter by itself, by its name in the language spoken ("bee", "zed"; "Ypsilon", "Eszett") and by the NATO and German spelling alphabets ("bravo", "Berta"). Say "capital" ("gross") before a letter for upper case, "space" ("Leerzeichen") and the names of punctuation marks ("comma", "question mark"; "Komma", "Fragezeichen") for those, and "back" ("zurück") to delete the last letter. Letter names are known for English and German; in the other languages say the letter itself or a spelling-alphabet word.

Speech is recognised by the device's speech recognition service, in the device's language if that is one of the chosen languages, else in the first chosen one. The app asks for the microphone permission the first time a voice mode is switched on.

---

## Dependencies

| Tool / Library | Version |
|---|---|
| Android Studio | Ladybug (2024.2.1) or newer |
| Android NDK | 27.0.12077973 |
| CMake | 3.22.1 |
| Android SDK | API 24 (min) |
| [DasherCore](https://github.com/janmurin2/DasherCore) | `third_party/DasherCore/` |
| [KenLM](https://github.com/kpu/kenlm) | `third_party/kenlm/` |
| Jetpack Compose / AndroidX | via Gradle |

---

## Building

1. Clone with submodules:
   ```bash
   git clone --recurse-submodules https://github.com/janmurin2/Dasher-Mobile.git
   cd Dasher-Mobile
   ```

2. Open the project in Android Studio and let Gradle sync.

3. Build:
   ```bash
   ./gradlew assembleDebug      # debug APK
   ./gradlew assembleRelease    # release APK (update signing config first)
   ./gradlew assembleRelease -Pnightly   # release APK signed with the nightly key, as the nightly builds are (see below)
   ```

CMake compiles the native libraries (`DasherCore`, `KenLM`) automatically as part of the Gradle build.

## Tests

Kotlin unit tests run on the JVM:

```bash
./gradlew testDebugUnitTest
```

The native layer (language sets, the personal dictionary, moving to a spoken letter) has its own tests, which build it together with DasherCore for the computer you are on, without the Android NDK:

```bash
cmake -S app/src/test/cpp -B build/native-tests
```

```bash
cmake --build build/native-tests
```

```bash
ctest --test-dir build/native-tests --output-on-failure
```

## Nightly builds

`.github/workflows/nightly.yml` builds a release APK every night at 02:30 UTC if `master` changed since the previous nightly release, and publishes it as a pre-release tagged `nightly-YYYY-MM-DD`. Runs started by hand (Actions → Nightly APK build → Run workflow) are tagged `nightly-YYYY-MM-DD-X1`, `-X2`, … for that day, as is a scheduled run whose tag is already taken.

Nightly APKs are signed with the nightly key, so each installs over the previous one. To install one over a build signed with a different key, uninstall that build first.

The key is not in the repository but in two repository secrets; until they are set, the workflow stops with an error. Create the keystore once, with Java's `keytool`:

```bash
keytool -genkeypair -keystore nightly.jks -storetype PKCS12 -alias dasher-nightly \
  -keyalg RSA -keysize 4096 -validity 10000 -dname "CN=Dasher Mobile Nightly"
```

or, without Java, with OpenSSL (or macOS's LibreSSL), which makes an equivalent keystore:

```bash
openssl req -x509 -newkey rsa:4096 -sha256 -days 10000 -nodes \
  -subj "/CN=Dasher Mobile Nightly" -keyout key.pem -out cert.pem
openssl pkcs12 -export -inkey key.pem -in cert.pem -name dasher-nightly -out nightly.jks
rm key.pem cert.pem
```

Then put the keystore and its password (the one `keytool` or `openssl` asked for) into the two secrets:

```bash
base64 < nightly.jks | gh secret set NIGHTLY_KEYSTORE_BASE64 --repo retog/Dasher-Mobile
gh secret set NIGHTLY_KEYSTORE_PASSWORD --repo retog/Dasher-Mobile
```

Keep `nightly.jks` and its password somewhere safe outside the repository: a nightly signed with a new key does not install over the earlier ones. To sign a build locally as the workflow does, set `NIGHTLY_KEYSTORE_FILE` (the path of `nightly.jks`), `NIGHTLY_KEYSTORE_PASSWORD` and `NIGHTLY_KEY_ALIAS=dasher-nightly`, then run `./gradlew assembleRelease -Pnightly`.

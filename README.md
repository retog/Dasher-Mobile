# Dasher Mobile

Dasher Mobile is an Android implementation of the [Dasher](https://www.inference.org.uk/dasher/) probabilistic text input system, designed for efficient and accessible text entry. Users steer a cursor into characters using touch gestures or device tilt; a statistical language model sizes letters by probability, making common letters easier to reach.

The app works as both a **standalone activity** and a system-wide **Input Method Editor (IME)**.

The architecture diagram is available in [`docs/architecture.puml`](docs/architecture.puml).

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

## Nightly builds

`.github/workflows/nightly.yml` builds a release APK every night at 02:30 UTC if `master` changed since the previous nightly release, and publishes it as a pre-release tagged `nightly-YYYY-MM-DD`. Runs started by hand (Actions → Nightly APK build → Run workflow) are tagged `nightly-YYYY-MM-DD-X1`, `-X2`, … for that day, as is a scheduled run whose tag is already taken.

Nightly APKs are signed with the nightly key, so each installs over the previous one. To install one over a build signed with a different key, uninstall that build first.

The key is not in the repository but in two repository secrets; until they are set, the workflow stops with an error. To create them once:

```bash
keytool -genkeypair -keystore nightly.jks -storetype PKCS12 -alias dasher-nightly \
  -keyalg RSA -keysize 4096 -validity 10000 -dname "CN=Dasher Mobile Nightly"
base64 < nightly.jks | gh secret set NIGHTLY_KEYSTORE_BASE64
gh secret set NIGHTLY_KEYSTORE_PASSWORD   # the password keytool asked for
```

Without Java, OpenSSL (or macOS's LibreSSL) makes an equivalent keystore:

```bash
openssl req -x509 -newkey rsa:4096 -sha256 -days 10000 -nodes \
  -subj "/CN=Dasher Mobile Nightly" -keyout key.pem -out cert.pem
openssl pkcs12 -export -inkey key.pem -in cert.pem -name dasher-nightly -out nightly.jks
rm key.pem cert.pem
```

Keep `nightly.jks` and its password somewhere safe outside the repository: a nightly signed with a new key does not install over the earlier ones. To sign a build locally as the workflow does, set `NIGHTLY_KEYSTORE_FILE` (the path of `nightly.jks`), `NIGHTLY_KEYSTORE_PASSWORD` and `NIGHTLY_KEY_ALIAS=dasher-nightly`, then run `./gradlew assembleRelease -Pnightly`.

# Nightly test signing key

`nightly-test.jks` is a self-signed, **non-secret** test certificate used to
sign release APKs, in particular the nightly builds (see
`.github/workflows/nightly.yml`), so that each nightly build has a consistent
signature and can be installed over the previous one without uninstalling.

It is **not** a production signing key. Its password is intentionally checked
in alongside it in `nightly-test.properties` — treat both files as public.
Never use this key for a real release build.

Regenerate it (if it's ever lost or needs rotating) with:

```bash
keytool -genkeypair -v \
  -keystore keystore/nightly-test.jks \
  -storetype PKCS12 \
  -alias dasher-nightly \
  -keyalg RSA -keysize 2048 -validity 10000 \
  -storepass dasher-nightly-test -keypass dasher-nightly-test \
  -dname "CN=Dasher Mobile Nightly Test, OU=Dasher Mobile, O=Dasher Mobile, L=Unknown, ST=Unknown, C=US"
```

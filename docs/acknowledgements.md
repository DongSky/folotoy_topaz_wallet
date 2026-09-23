[简体中文](acknowledgements.zh_CN.md)

# Acknowledgements

Topaz Wallet builds on the work of the following projects and their contributors.

## Project foundation and references

- **[FoloToy AI Passport](https://github.com/FoloToy/ai-passport)** provides the
  hardware platform, ESP-IDF project foundation, BSP drivers, reference demos,
  engineering documentation and validation tooling used by this project. The
  original MIT copyright notice is retained in [LICENSE](../LICENSE).
- **[FoloToy product documentation](https://ai-passport.folotoy.cn/)** informed
  device interaction and the navy/green visual reference. Original FoloToy names
  and product assets remain attributed to their owners.
- **[neverleftWTF/folotoy-ai-passport-re](https://github.com/neverleftWTF/folotoy-ai-passport-re/tree/c739b4dd95588a0e41624eda05eb1655571d1a2f)**
  supplied public research into the original BLE protocol (MIT, stock 1.0.0).
  This research was a reference; matching service UUIDs alone does not establish
  compatibility. The stock 1.0.3 implementation also used independent analysis
  of application behavior. No provisioned device backup is distributed.

## Firmware and fonts

| Project | Contribution | Version / license |
| --- | --- | --- |
| [Espressif ESP-IDF](https://github.com/espressif/esp-idf) | ESP32-C3 runtime and tooling | 5.5.3; Apache-2.0 plus bundled component licenses |
| [Espressif components](https://github.com/espressif/esp-iot-solution) | LVGL port, buttons and audio codec support | Versions pinned in `dependencies.lock`; component licenses apply |
| [LVGL](https://github.com/lvgl/lvgl) | Device UI and image rendering | 9.5.0; MIT |
| [Adobe Source Han Sans](https://github.com/adobe-fonts/source-han-sans) | Chinese font glyphs | SIL Open Font License 1.1; [retained license](../assets/fonts/SourceHanSansSC-LICENSE.txt) |
| [lv_font_conv](https://github.com/lvgl/lv_font_conv) | Reproducible font conversion | 1.5.3; MIT |

## Android and development tools

| Project | Contribution | Version / license |
| --- | --- | --- |
| [AndroidX](https://developer.android.com/jetpack/androidx) | Android integration and instrumentation | Core 1.15.0; Apache-2.0 |
| [ZXing](https://github.com/zxing/zxing) | QR encoding and decoding | 3.5.3; Apache-2.0 |
| [Gson](https://github.com/google/gson) | Structured data parsing | 2.11.0; Apache-2.0 |
| [bitcoinj](https://github.com/bitcoinj/bitcoinj) | Mnemonic-based wallet import | 0.16.3; Apache-2.0 |
| [Bouncy Castle](https://www.bouncycastle.org/) | Cryptographic primitives | 1.83; Bouncy Castle license (MIT-style) |
| [Gradle](https://github.com/gradle/gradle) | Android build tooling and wrapper | 8.14.3; Apache-2.0 |
| [JUnit](https://github.com/junit-team/junit4) | Unit testing | 4.13.2; EPL-1.0 |
| [actionlint](https://github.com/rhysd/actionlint) | Workflow validation | 1.7.12; MIT |

This is a guide to major direct dependencies, not a replacement for their license
texts or the notices of transitive dependencies. Keep all applicable notices when
redistributing source or binaries. Topaz Wallet is an independent derivative;
acknowledgement does not imply endorsement or official support by these projects.

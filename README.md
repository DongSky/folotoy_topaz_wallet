[简体中文](README.zh_CN.md)

# Topa Wallet

A companion app and device application that brings personal cards and receiving
codes to AI Passport. Edit content on Android, sync it over Bluetooth, and carry
your card with you.

## Features

- Avatar, nickname, introduction and social handles.
- External Alipay/WeChat receiving-code image import.
- Import a wallet using a mnemonic phrase to generate receiving QR codes.
- Preview pages on Android and sync them to the device over Bluetooth.
- Keep the device's Card and Image entry points. Official WeChat mini-program
  compatibility still requires real-device acceptance.

## Source layout

`android/` contains the companion, `main/` the firmware application, `components/bsp/`
board drivers, `assets/fonts/` the licensed Chinese font, and `tests/` host tests.
`tools/` provides validation and firmware packaging. `docs/`, `skills/` and upstream
reference demos are retained to keep engineering guidance and regression tests usable.
See [application details](docs/development/card-wallet-redesign.md).

## Build

Use ESP-IDF 5.5.3 for ESP32-C3 and an 8 MB board:

```sh
. "$IDF_PATH/export.sh"
./tools/validate.sh
```

For Android, install JDK 17+ and Android SDK 35, set `JAVA_HOME` and `ANDROID_HOME`,
then run:

```sh
cd android
./gradlew testDebugUnitTest assembleDebug lintDebug
./gradlew connectedDebugAndroidTest  # requires a connected test device/emulator
```

`local.properties`, caches and signing keys are intentionally excluded. The APK
provided in `dist/` is the latest verified debug build, version 0.3-dev (code 3).
Its debug signing private key is not distributed. A locally rebuilt APK may use
a different signature and cannot necessarily update an existing installation.

## Prepared artifacts and data preservation

Download APK and firmware bundles from [Releases](https://github.com/DongSky/folotoy_topaz_wallet/releases).
The local `dist/` folder is Git-ignored and contains the checked APK, firmware images,
relative-path manifests, SHA-256 checksums and bilingual flashing notes. The images
contain compiled application code, not a device backup or provisioned identity.
No original Flash/NVS dump, personal wallet data, serial logs, ELF/MAP or old Git
history is included. Source and releases are hosted at
[DongSky/folotoy_topaz_wallet](https://github.com/DongSky/folotoy_topaz_wallet).

Existing provisioned devices must use the listed segmented images to retain NVS
and resources. The merged full image is for intentional provisioning; do not flash
it over existing settings. A blank board also needs legitimate device provisioning
and stock resources, which are not distributed here.

Build and host tests passed; Android has 62 passing unit tests and ten emulator
tests. The renamed firmware has not been tested on the physical device. Real BLE,
WeChat, display/QR and audio acceptance remain pending.

## License

Preserve [LICENSE](LICENSE) (FoloToy MIT) and third-party notices, including the font
license under `assets/fonts/`. Tests use public synthetic fixtures.

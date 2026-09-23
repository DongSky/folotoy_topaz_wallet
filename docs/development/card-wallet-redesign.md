[简体中文](card-wallet-redesign.zh_CN.md)

# Topa Wallet Application

The current implementation is described in the [project guide](../../README.md).
Android version 0.3-dev retains its package ID for compatible upgrades. Its mnemonic
entry is ordinary visible multiline text; only the optional BIP39 passphrase is masked.

## Architecture and compatibility

The phone derives public BTC BIP84, EVM BIP44 and Solana SLIP10 addresses locally,
queries public balance/price APIs and renders offline wallet pages. Bitcoin uses
the first receiving address, not full HD-account discovery. No mnemonic or seed is
persisted or transferred. Original BLE identity comes from the device's existing
read-only `cardid` partition; no generated identity or personal provisioning is included.
Stock profile, image, built-in avatar, RTTTL music and screenshot protocols are
implemented alongside the authenticated owner-only PCW snapshot service. UUID,
serial advertising and mini-program wire formats are retained. Compatibility is
not established until the unmodified official mini-program passes hardware tests.

## Layout and operation

The 240 by 320 UI uses navy and green, a licensed Chinese font, Card/Image home
entries, then card details, social accounts, payment codes, addresses and assets.
UP/DOWN selects, OK enters or reveals, long OK returns. Long UP opens pairing;
long DOWN requests physical confirmation before resetting the paired phone.
Snapshots are limited to 52 pages and stored in two atomic banks. New private
pages cannot inherit an older snapshot's reveal permission. Legacy screenshots
reject wallet pages and pairing/reset dialogs.

## Firmware data

`partitions.csv` preserves stock NVS, identity and image/audio resource locations.
Wallet banks start at `0x500000`/`0x660000`; screenshot scratch starts at `0x7c0000`.
RTTTL is limited to 1,024 bytes, ten minutes and octaves 0–8. Capture uses 40-row
strips instead of a full heap framebuffer. Never distribute provisioned Flash dumps.

## Validation

Complete firmware/build and host checks, 62 Android unit tests, ten API 35 emulator
tests and lint passed. Native LVGL verified font rendering and capture. Physical
BLE/SMP, official WeChat, ROM avatar decoding, music, QR scanning and runtime heap
acceptance remain unverified for this build. Artifact identities live in the local
Git-ignored `dist/manifest.json`, with SHA-256 checksums and flashing instructions.

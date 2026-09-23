[简体中文](CI-build-and-release.zh_CN.md)

# Build and Release

`.github/workflows/build-firmware.yml` runs on manual dispatch and uploads the
verified firmware as an Actions artifact. It does not publish GitHub Releases or
run on tags. Main-branch firmware checks remain in `firmware-checks.yml`.

For Topaz Wallet releases, the maintainer checks source and packaged binaries for
personal data, verifies checksums and signs off on known hardware limitations.
Create a release at the reviewed commit and upload only the checked APK, firmware
bundle, manifest, sanitization report and checksums. Do not upload device backups,
NVS, personal profiles, signing keys, ELF/MAP or raw build/serial logs.

The firmware bundle contains segmented images for existing provisioned devices
and a merged image for explicit complete provisioning. Include the data-preserving
flashing instructions with every bundle. Use a prerelease while physical-device
acceptance remains incomplete. Keep the APK version and release notes consistent;
a prerelease tag may refine the existing development version without rebuilding
already verified binaries.

Update both changelog languages before tagging. Automatic tag publishing is
disabled so a CI rebuild cannot replace a manually audited release asset.

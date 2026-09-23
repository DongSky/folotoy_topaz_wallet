[简体中文](CI-sync-main.zh_CN.md)

# Optional Upstream Synchronization

The reference `.github/workflows/sync-main.yml` is manual-only and additionally
requires GitHub to mark the repository as a fork. The standalone Topa Wallet
repository does not automatically merge upstream firmware. No scheduled sync runs.
Review changes from `FoloToy/ai-passport` before integrating them; preserve the
application, data layout and original BLE compatibility requirements.

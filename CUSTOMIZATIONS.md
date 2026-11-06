# rasj81/ESPWebDav: customised FYSETC SD-WIFI firmware

Development line is `main`. `fysetc` and `master` are untouched mirrors of FYSETC/ESPWebDAV and ardyesp/ESPWebDAV, kept only so upstream changes can be compared.

- Base: FYSETC/ESPWebDAV `fysetc` branch, commit ccf2209 (2022-09-15). Upstream has been quiet since, so expect few or no upstream merges.
- Custom features (see the commit on `main`): HTTPS file manager, Digest login, buffered logging, FIRMWARE.BIN self-update, PRINTER_WAIT and Printer mode, SETUP.INI settings.
- Added with the printer-in-SPI-mode work: web firmware update without a login, status LED patterns with a one-time IP blink (LED=, M60), BOOT_WAIT (stay off the card after power-up), SD init retries at slower speeds with the SdFat error code in the failure message, and the `/_diag` page (CS edges, clock/data activity, `rsv` flag). The diagnostic sampler is still in the main loop.
- Build: Arduino IDE, board Generic ESP8285 Module, library SdFat. The folder must be named `ESPWebDAV`. Flash with the card's switch on USB2UART (hold FLSH while plugging in USB).
- Branches: `main` = stable and includes everything from `diagnostic-cs-sense` (merged 2026-11-06, see DIAGNOSTIC.md); `diagnostic-cs-sense` is kept as it was.
- Pulling upstream (only if it ever changes): `git remote add upstream https://github.com/FYSETC/ESPWebDAV.git && git fetch upstream && git merge upstream/fysetc`.
- Public repo: keep Wi-Fi names, passwords and certificates out of it. `ini/SETUP.INI` holds placeholders only.

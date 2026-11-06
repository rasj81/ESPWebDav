# rasj81/ESPWebDav: customised FYSETC SD-WIFI firmware

Development line is `main`. `fysetc` and `master` are untouched mirrors of FYSETC/ESPWebDAV and ardyesp/ESPWebDAV, kept only so upstream changes can be compared.

- Base: FYSETC/ESPWebDAV `fysetc` branch, commit ccf2209 (2022-09-15). Upstream has been quiet since, so expect few or no upstream merges.
- Custom features (see the commit on `main`): HTTPS file manager, Digest login, buffered logging, FIRMWARE.BIN self-update, PRINTER_WAIT and Printer mode, SETUP.INI settings.
- Build: Arduino IDE, board Generic ESP8285 Module, library SdFat. The folder must be named `ESPWebDAV`. Flash with the card's switch on USB2UART (hold FLSH while plugging in USB).
- Branches: `main` = stable; `diagnostic-cs-sense` = measures why the printer is seen as busy (see DIAGNOSTIC.md); merge into `main` only if it proves useful.
- Pulling upstream (only if it ever changes): `git remote add upstream https://github.com/FYSETC/ESPWebDAV.git && git fetch upstream && git merge upstream/fysetc`.
- Public repo: keep Wi-Fi names, passwords and certificates out of it. `ini/SETUP.INI` holds placeholders only.

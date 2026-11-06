# Diagnostic build (branch `diagnostic-cs-sense`): why is the card "busy" with the printer idle?

Sketch: `ESPWebDAV.ino` at the repo root (Arduino IDE needs the folder to be named `ESPWebDAV`). Based on the snapshot of `~/Downloads/ESPWebDAV` taken 2026-11-05 (commit on `main`). Bus-sharing rules (PRINTER_WAIT, Printer mode) are unchanged; this only measures and reports.

## What changed
- `sdControl.*`: the CS_SENSE interrupt also counts falling edges and remembers the latest one. `sample()` (called from `loop()`) reads the SD clock (GPIO14) and command (GPIO13) pins in a tight loop to see whether the printer is clocking the card. No interrupts on those pins (the clock would starve the chip).
- `network.cpp`: the "Printer is using the card" reject message is replaced by a diagnostic string.
- `ESPWebDAV.ino`: calls `sdcontrol.sample()` in `loop()`.

## Use
1. Build for Generic ESP8285 Module, flash with the card on `USB2UART` (hold FLSH while plugging in USB, release, Upload).
2. Put the card in the printer, leave the printer idle at the main menu, wait for the Wi-Fi to come up.
3. Browse the card over WebDAV. The diagnostic text appears as a file entry. Refresh 2-3 times, 5+ seconds apart, and record each string.

## Reading it
`Printer_busy_diag_up<s>s_cs<0|1>_edges<N>_last<ms>ms_d<dN>_in<dms>ms_clk<ms>_cmd<ms>_wait<s>s_left<ms>ms`
- `up` seconds since boot. `cs` level of CS_SENSE right now (0 = low).
- `edges` total falling edges on CS_SENSE since boot (while we did not own the bus). `last` ms since the latest (0 = none).
- `d<dN>_in<dms>` edges since the previous report and the ms between reports.
- `clk` / `cmd` ms since the clock / command pin was last seen changing (0 = never).
- `wait` the PRINTER_WAIT setting. `left` ms until the page would accept you (> 0 = still blocked).

## Interpreting
- `d` > 0 on every refresh at idle: something touches DAT3 constantly (printer polling, or noise).
- `edges` flat but `cs`0: line stuck low; edge detection is the wrong test.
- `clk`/`cmd` stay 0 while `edges` rise: the edges are not SD traffic.
- `clk`/`cmd` recent at idle: the printer really is clocking the card; a clock-based signal would not help.

## Separate finding
The ESP's own SD update file is `FIRMWARE.BIN` (`firmware.h`), the same name the printer's bootloader looks for. That is how the printer picked up the SD-WIFI firmware. Consider renaming `FW_FILE` (e.g. `ESPFW.BIN`) so a printer can never flash it.

## Status LED (this branch)
`statusLed.*` replaces the blocking blink code; `loop()` no longer stalls for 50 ms every second.

| LED | Meaning |
|---|---|
| off | Wi-Fi connecting |
| solid on | no Wi-Fi, or this board is using the card right now |
| 1 short flash / s | connected, card free |
| 2 short flashes / s | printer used the card within PRINTER_WAIT |
| 1 s on, 1 s off | Printer mode (`M58 PRINTER`) |

After the first Wi-Fi connect since power-up the IP address is blinked once: 3 fast flashes, then each digit as that many flashes (0 = one long flash), a long pause at each dot, 3 fast flashes to finish. `10.0.0.24` = 1, long, (dot) long, (dot) long, (dot) 2, 4. About 15-55 s depending on the address.

Control: `LED=AUTO|ON|OFF` in SETUP.INI (ON/OFF also suppress the IP blink); `M60 AUTO|ON|OFF` until restart; `M60 IP` repeats the address.

The saved settings version goes from 7 to 8. Flashing this build over an older one migrates cleanly; flashing an older build back over it resets the settings except Wi-Fi name, password and device name (SETUP.INI on the card restores the rest).

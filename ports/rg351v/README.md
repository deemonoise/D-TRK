# D-TRK for Anbernic RG351V on ArkOS

[Русская версия](README_ru.md)

## Install and start

1. Extract `d-trk-rg351v.zip` into the active `ports/` folder on your games card. On ArkOS this is `/roms/ports/` or, if you use the second card for games, `/roms2/ports/`.
2. Keep `D-TRK.sh` beside the `d-trk-rg351v/` directory. Restart EmulationStation from its menu to rescan the Ports list, then open **Ports → D-TRK**.
3. The first launch opens a demo. Press **Start** to play it. To return to ArkOS, open **FILE → Quit tracker**.

The port uses SDL2, KMSDRM video and ALSA playback. On the 640×480 display, the tracker retains its 3∶2 layout and renders its text at the screen resolution.

## Controls

| Buttons | Action |
| --- | --- |
| L1 / R1 | Switch screens |
| L2 / R2 | Switch tabs or GRID views. In MIX track controls, select a track |
| D-pad | Move the cursor. When editing a number, left/right changes it by 1 and up/down by 10 where the parameter supports it |
| A / B | Confirm / back |
| X / Y | Context menu / encoder click |
| Select | Shift. In GRID, Select + A clears and Select + B undoes |
| Start | Start or stop playback |

Projects, samples, presets and the launch log live in `saves/d-trk-rg351v/` on the same card, outside the app folder. Use **FILE → SYSTEM → Network file transfer…** to see the address and data path. For SSH, turn on **Options → Enable Remote Services** in ArkOS.

Use the USB OTG port for USB MIDI. The stock RG351V ArkOS image lacks the Bluetooth and PipeWire services that D-TRK uses for BLE MIDI on KNULLI.

## Build

Build the common SDL target with a Linux ARM64 toolchain whose libc is compatible with your ArkOS install. Package that executable with `ports/rg351v/package.sh /path/to/d-trk`. The archive is written to `target/rg351v/d-trk-rg351v.zip`.

# D-TRK for Anbernic RG35XXSP on KNULLI

[Русская версия](README_ru.md)

## Installation and launch

1. Extract `d-trk-rg35xxsp.zip` into `/userdata/roms/ports/`. Place `D-TRK-RG35XXSP.sh` next to the `d-trk-rg35xxsp/` directory.
2. In KNULLI, choose **Start → Game Settings → Update Gamelists**, then open **Ports → D-TRK-RG35XXSP**. If that entry is named **D-TRK** in the KNULLI metadata, open it under that name.
3. The first launch opens a demo project. Press **Start** to play and again to stop. Choose **FILE → New** to select a template.

## Controls

| Buttons | Action |
|---|---|
| **L1 / R1** | Switch screens. From GRID, L1 opens FILE. |
| **L2 / R2** | Switch tabs or GRID views. In **MIX → X → Track controls**, select a track. |
| **D-pad** | Move the cursor or selection. |
| **A / B** | Confirm or go back. In GRID, A enters a note and **A + D-pad** edits a value. |
| **X / Y** | Open the context menu or click the encoder. |
| **Select + A / B** | Clear a step or selection in GRID, or undo an edit in GRID or SONG. Select acts as Shift. |
| **Start**, **Vol− / Vol+** | Start or stop playback, or adjust the volume through KNULLI. |

In **MIX**, up and down adjust the master volume, while left and right move the crossfader. In **Track controls**, the D-pad adjusts track volume and pan. Press **A** to toggle solo or **B** to toggle mute.

To exit, save with **FILE → Save**, then choose **Quit tracker**. If you have unsaved edits, **Keep editing** returns you to the project.

## Display, audio and recording

The launcher uses SDL Mali with sharp text on the built-in 640 × 480 display. Audio uses the system ALSA device `default` through KNULLI and PipeWire.

Open **MIX → X → Record…** and choose **RESAMPLE** to record the tracker's own output. If playback is stopped, press **Start** after starting the recording. The RG35XXSP launcher disables **MIC** and **LINE**. **RESAMPLE (NO SHIELD)** in the source list means you can record internal audio.

## Project files

Data lives in `/userdata/saves/d-trk-rg35xxsp/`. To transfer a project, copy `projects/NAME.mtp` and the matching `projects/NAME/` directory, if present. That directory contains samples and custom wavetables. To transfer all data, close the tracker and copy the entire directory. Put MIDI files for import in `midi/` and WAV files in `samples/`.

For Wi-Fi transfers, open **FILE → SYSTEM → Network file transfer…** to find the handheld's IP address. The screen shows the shared label `saves/d-trk/`. The RG35XXSP projects are in `saves/d-trk-rg35xxsp/` inside `smb://IP/share`. For SCP, use `/userdata/saves/d-trk-rg35xxsp/`.

## Building for RG35XXSP

The SDL build needs a C++17 compiler, Make and SDL2 with `sdl2-config` on your PATH. Run these commands from the repository root.

```sh
make -C ports/rg35xxsp -j4 all check-navigation
target/rg35xxsp/native/d-trk --self-test
ports/rg35xxsp/package.sh
```

The last command builds the ARM64 archive with Docker. To package an existing ARM64 ELF, pass the path to it as an argument. The archive is written to `target/brick/d-trk-rg35xxsp.zip`. The existing command `DTRK_TARGET=rg35xxsp ports/brick/package.sh` also works.

## Troubleshooting

- **Missing from Ports.** Check that `D-TRK-RG35XXSP.sh` and the `d-trk-rg35xxsp/` directory are directly inside `/userdata/roms/ports/`, then update the gamelists.
- **Permission denied.** Run `chmod +x /userdata/roms/ports/D-TRK-RG35XXSP.sh /userdata/roms/ports/d-trk-rg35xxsp/d-trk`.
- **No audio or an immediate return to the menu.** Check the volume and audio output in KNULLI. Look for SDL or startup errors in `/userdata/saves/d-trk-rg35xxsp/launcher.log`.

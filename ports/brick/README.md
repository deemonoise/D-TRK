# D-TRK for TrimUI Brick on KNULLI

[Русская версия](README_ru.md)

## Installation and launch

1. Extract `d-trk-brick.zip` into `/userdata/roms/ports/` on the handheld. The folder should contain `D-TRK.sh` next to the `d-trk/` directory.
2. In KNULLI, choose **Start → Game Settings → Update Gamelists**, then open **Ports → D-TRK**.
3. The first launch opens a demo project. Press **Start** to play it and again to stop. To write your own pattern, open **FILE → New** and choose an empty template.

The percentage in the top bar comes from the handheld's power controller. The plug above the battery means USB power is connected and stays visible at **100%**. The lightning bolt inside the battery means it is charging.

## Controls

| Buttons | Action |
|---|---|
| **L1 / R1** | Switch screens. From GRID, L1 opens FILE. |
| **L2 / R2** | Switch tabs or GRID views. In **MIX → X → Track controls**, select a track. |
| **D-pad** | Move the cursor or selection. |
| **A / B** | Confirm or go back. In GRID, A enters a note and **A + D-pad** edits a value. |
| **X / Y** | Open the context menu or click the encoder. |
| **Select + A / B** | Clear a step or selection in GRID, or undo an edit in GRID or SONG. Select acts as Shift. |
| **Start**, **Vol− / Vol+** | Start or stop playback, or adjust the handheld volume. |

In **MIX**, up and down adjust the master volume, while left and right move the crossfader. In **Track controls**, the D-pad adjusts track volume and pan. Press **A** to toggle solo or **B** to toggle mute.

To exit, save with **FILE → Save**, then choose **Quit tracker**. If you have unsaved edits, **Keep editing** returns you to the project.

## Audio and recording

The Brick audio engine allocates up to 64 voices. How many notes can sound at once depends on the instruments, effects and modulation. When the budget fills, the synthesizer releases voices according to its usual priority rules.

For the built-in speaker and wired headphones, launch the tracker from **Ports → D-TRK**. The app opens the Brick sound card directly through ALSA, so it can play a pattern while recording from the built-in microphone. The launcher restores the previous KNULLI audio profile and input settings even if the tracker exits unexpectedly. If you selected a Bluetooth or USB output in KNULLI before launching, the tracker uses the system audio route.

Open **MIX → X → Record…** and choose a source.

- **MIC** records from the built-in microphone when the app has direct access to the Brick codec. In this mode, **Gain +0 dB** means unity digital gain.
- **LINE** uses an available SDL capture input. To use an external USB input, select it as the system audio source in KNULLI. Availability depends on the selected audio route.
- **RESAMPLE** records the tracker's own output. If playback is stopped, press **Start** after starting the recording.

## Project files

Data lives in `/userdata/saves/d-trk/`. To transfer a project, copy `projects/NAME.mtp` along with the matching `projects/NAME/` directory, if present. That directory contains the project's samples and custom wavetables. For a full backup, close the tracker and copy the entire `saves/d-trk/` directory. Put MIDI files for import in `midi/` and WAV files in `samples/`. The last launch log is in `launcher.log`.

For Wi-Fi transfers, open **FILE → SYSTEM → Network file transfer…** to find the handheld's IP address. On macOS, choose **Go → Connect to Server** in Finder and enter `smb://IP/share`. In Windows File Explorer, enter `\\IP\share`. Replace `IP` with the handheld's address. In the share, use `saves/d-trk/` for project data and `roms/ports/` for the app. [KNULLI network transfer](https://knulli.org/play/add-games/network-transfer/)

If SMB is disabled and SSH is running, the screen shows the SCP address. The same data is available at `/userdata/saves/d-trk/`.

## Building for Brick

The SDL build needs a C++17 compiler, Make and SDL2 with `sdl2-config` on your PATH. Run these commands from the repository root.

```sh
make -C ports/brick -j4 all check-navigation
target/brick/native/d-trk --self-test
ports/brick/package.sh
```

The last command builds the ARM64 archive with Docker. To package an existing ARM64 ELF, pass the path to it as an argument. The archive is written to `target/brick/d-trk-brick.zip`.

## Troubleshooting

- **Missing from Ports.** Check that `D-TRK.sh` and the `d-trk/` directory are directly inside `/userdata/roms/ports/`, then update the gamelists.
- **Permission denied.** Restore execute permissions with `chmod +x /userdata/roms/ports/D-TRK.sh /userdata/roms/ports/d-trk/d-trk`.
- **No audio or MIC is unavailable.** Check the output selected in KNULLI and launch the app from Ports. For the built-in mic, select the built-in audio output before launching. Audio routing and SDL messages are in `/userdata/saves/d-trk/launcher.log`.

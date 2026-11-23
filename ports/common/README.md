# D-TRK handheld port guide

[Русская версия](README_ru.md)

## Pattern editing

| Buttons | Action |
|---|---|
| **↑ / ↓** | Move to the previous or next step, or select a row in a list. |
| **← / →** | Select a track in GRID Overview or a NOTE, VEL or FX field in Detail. |
| **A** | On an empty NOTE cell of a melodic track, enter the last note used on that track and advance by the Edit step setting. In a menu, confirm the selection. |
| **A + D-pad** | On a note, ↑/↓ change pitch by a scale degree and ←/→ by an octave. For numeric parameters, ←/→ change the value by 1 and ↑/↓ by 10. In option lists, each step selects the adjacent value. Releasing A leaves the cursor in place. |
| **B / X** | B goes back. X opens the context menu. In GRID, it includes Undo and **Edit step… → Note OFF / Clear all**. |
| **L1 / R1** | Switch to the previous or next screen. From GRID, press **L1** to open FILE. |
| **L2 / R2** | Switch to the previous or next tab within a screen. In GRID, cycle through Overview, Detail, Piano roll and Automation. In the MIX **TRACKS** mode, select a track. |
| **Y** | Click the encoder. **Select + Y** clicks with Shift held. In GRID, a plain click toggles edit mode where available. |
| **Select** | Shift. Hold **Select + L2 / R2** to select a track in GRID, TRACK or INST. |
| **Select + A / B** | **A** clears a step or selection in GRID, or the current parameter point in Automation. **B** undoes an edit in GRID or SONG. |
| **Start** | Start or stop playback. |
| **L2 + Start** | Trigger Fill during playback. KNULLI uses **Select + Start** for screenshots. |
| **Vol− / Vol+** | Turn the volume down or up. |

**Touch pointer.** Open **FILE → SYSTEM → Touch pointer**. Use the D-pad to move the pointer, **L2** for finer movement and **A** to tap. Hold **A** and move the pointer to drag. Press **R2** to return to the usual controls.

**MIX.** MIX opens in **MAIN** mode. **↑/↓** adjust the project master volume and **←/→** move the crossfader. In **X → Track controls**, the same buttons adjust the selected track's volume and pan. Use **L2/R2** to select a track, **A** to toggle solo and **B** to toggle mute. Choose **X → Main controls** to return to the master controls. In either mode, holding a direction repeats the adjustment and holding **Select** increases the step to eight. The **X** menu also gives access to recording, scenes, PERF and A/B scene selection.

**Names.** In the Name row in TRACK and INST, **←/→** move the caret and **↑/↓** select the adjacent character. On the on-screen keyboard, press **↑** from the top row to move to the text field. Press **A** to return to the key grid. Typing inserts a character at the caret. **DEL** deletes the character to its left.

**BANK and the on-screen QWERTY keyboard.** Use the D-pad to move between cells horizontally or vertically. In **INST**, press **X** to preview or **Select + X** to open the preset menu. For note and movement recording settings, open **GRID → X → Recording setup**.

## Saving and transferring files

On **FILE**, use **Save As…** to save the project under a new name, **Save** to update the current project and **Load…** to open a saved project.

**Return to KNULLI.** Open **FILE**, select **Quit tracker** with the D-pad and press **A**. The tracker exits immediately if the project is saved. If you have unsaved edits, choose **Keep editing** to return to the project or **Quit without saving** to discard the edits and exit. To save before exiting, choose **Keep editing**, then **Save**.

## Shared SDL build

You need a C++17 compiler, Make and SDL2 with `sdl2-config` on your PATH. Run these commands from the repository root.

```sh
make -C ports/common -j4 all check-navigation check-engine check-render check-battery check-peripherals
target/ports/native/d-trk --self-test
target/ports/native/d-trk --windowed
python3 -m unittest discover -s ports/common/tests
```

Both ports use this binary. Each launcher selects the audio and display settings, and the app selects the layout by controller name. The `BUILD` parameter sets the build directory relative to the directory passed to Make. To package an existing shared ARM64 binary, pass its path to either port's `package.sh` script.

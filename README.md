# GridCollider

GridCollider is a C++20 JUCE standalone desktop app scaffold inspired by Orca-style glyph grids, SuperCollider OSC workflows, and minimalist geometric composition systems.

This version includes an optional embedded SuperCollider host-audio engine. It also keeps the external OSC bridge path available, so the app can run with or without the Alchemy-style embedded SuperCollider libraries.

The interpreter implements an Orca-style operator registry. Uppercase letter operators run on every transport tick, lowercase letter operators run when adjacent to a bang (`*`), and evaluation proceeds deterministically from top-left to bottom-right. Grid-mutating operators update the visible grid.

Operators emit typed internal events (`NoteEvent`, `ControlEvent`, `TriggerEvent`, `BusRouteEvent`, `GridMutationEvent`, and `LogEvent`) with common tick, source-cell, instrument, pitch, velocity, duration, parameter-map, and optional target-address fields. `EventRouter` routes those events to the status log, event monitor, and OSC output.

OSC output uses JUCE's OSC sender. The top control area sets host and port, connect or disconnect, show connection status, and toggle debug logging. Outgoing OSC messages use:

- `/gc/note instrument pitch velocity duration x y tick`
- `/gc/control target parameter value x y tick`
- `/gc/trigger name x y tick`
- `/gc/grid width height encodedGrid`

When the embedded SuperCollider runtime is available, the same internal note and trigger events are also rendered directly inside the JUCE audio callback using starter SynthDefs for `kick`, `snare`, `hat`, `bass`, `tone`, `grain`, and `drone`. Embedded output now runs through a private SuperCollider bus with a master limiter/drive node. Control events can update master level/drive, instrument levels, and pan on the most recent active synth for an instrument.

Embedded note channels map to starter instruments: `0=tone`, `1=bass`, `2=drone`, `3=grain`, `9=kick`, `a=snare`, and `b=hat`.

## Build

```sh
cmake -S . -B build
cmake --build build --config Debug
```

CMake fetches JUCE automatically. On Linux, install the normal JUCE desktop build dependencies for your distribution before configuring.

## Release

On macOS, build, zip, tag, and upload a GitHub Release asset with:

```sh
scripts/release_macos.sh
```

The script reads the project version from `CMakeLists.txt`, writes `dist/GridCollider-<version>-macOS.zip`, pushes the current branch, creates/pushes `v<version>`, and creates or updates the GitHub Release asset. Use `--no-push` to build and zip locally without tagging or uploading.

## Composition Controls

- The upper view is a finite-state composition map. A composition can hold up to 16 states.
- Each state can hold up to 8 lanes. A lane can be an Orca-style grid or a SuperCollider SynthDef code pane.
- The main surface is arranged as title/transport, transition-code pane, FSM view, and active grid view.
- Drag the divider below the transition-code pane to resize the code/workspace split.
- Drag the divider between the FSM and grid panes to resize the left/right split.
- Each state owns its BPM, transition-code rule, and grid stack. Selecting a state swaps the visible `TRANSITIONS.SC` pane and grid tabs to that state.
- During playback, the selected state's `~linear` and `~weighted` maps are parsed when its `ADV` condition fires and can move the active state.
- The FSM view draws directed transition connections from every state's transition-code pane. Weighted connections display their probability on the line.
- Click a state node to select it. Double-click empty space in the state map, press F4, or use the state `+` button to add a state.
- Use the `STATE` controls for previous/next/add state, and the lane controls for previous/next/add lane inside the selected state. The `GRID`/`SC` button swaps the selected lane between a grid and a SuperCollider code pane.
- The `ADV` controls set how the selected state advances through the transition map: `MANUAL`, after a number of `BEATS`, after a number of `BARS`, or on a trigger named `advance`, `next`, or `transition`.
- Click a state to select it. With the FSM focused, Backspace/Delete removes it, Command/Ctrl + C copies it, and Command/Ctrl + V duplicates it after the selected state.
- The `1:` field sets the selected grid's clock ratio as `1:X.x` against the state tempo. `1:1.0` follows the state tempo; larger values run slower.
- `SYNC` locks the selected lane to the state's phase. `OFFSET` shifts it by 0-360 degrees inside its tempo-ratio cycle.
- New grids default to `32x32`. The `SIZE` fields resize the selected grid from `1x1` up to `64x32`, preserving existing cells that remain inside the new bounds.
- The active grid auto-fits to the visible editor pane, so `64x32` remains fully viewable when the transition-code and FSM panes are minimized.
- The optional `MIXER` view shows one channel for every lane in every state. Grid and SuperCollider lanes both get level and pan; the final `MASTER` channel has level only.
- The interface uses a Source TEXT-inspired visual system: dark code-field surfaces, lightweight source-derived strata, token-coloured grid/code cells, syntax-bright state/channel accents, and live operational readouts generated from the app's own renderer and runtime state.
- Use the native `File` menu for `Load`, `Save`, and `Save As`. `.gridcollider` documents preserve the full composition: states, BPM, transition programs, lane stacks, grid contents, SC code, timing, mixer settings, and active selection.

## Editor Controls

- Type printable ASCII characters to write into cells.
- Arrow keys move the cursor; Shift + arrow extends a rectangular selection.
- Drag with the mouse to select cells.
- Command/Ctrl + C, X, and V copy, cut, and paste rectangular selections.
- Backspace and Delete clear cells. Command/Ctrl + K clears the whole grid.
- Command/Ctrl + Plus/Minus zooms. Command/Ctrl + 0 resets zoom.
- Command/Ctrl + R toggles line and column rulers.
- Press F1/F2 for previous/next lane and F3 to add an empty grid lane.
- Command/Ctrl + 1-9 jumps directly to an existing grid slot.
- F5 starts or pauses transport. F6 stops transport. F7 resets the frame counter.
- F8 loads a bundled `.orca` example into the grid.
- F9 loads, F10 saves, and F11 opens recent patterns.
- F12 sends a direct embedded SuperCollider test note.

## Making Sound

GridCollider first tries the embedded SuperCollider audio engine. On this machine it looks for the Alchemy host-audio libraries under `/Users/user/Documents/weld2/ChucK_weld_engine/build-supercollider-host`.

The external bridge is still useful for debugging or when the embedded libraries are unavailable:

```sh
/Applications/SuperCollider.app/Contents/MacOS/sclang supercollider/GridColliderBridge.scd
```

Then connect OSC to `127.0.0.1` port `57120`, load a bundled example, and press `PLAY`.

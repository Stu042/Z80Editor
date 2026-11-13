# Z80Editor

A native Qt 6 assembly editor for Z80 and ZX Spectrum Next (Z80N) programs.
Press **F5** to run the current editor contents using the Z80Test simulator and
view the final registers, flags, instruction count, T-states, and simulated time.

## Build

Keep these as separate sibling projects:

```text
CLionProjects/
  Z80Test/
  Z80Editor/
```

Install a C/C++ toolchain, CMake 3.16+, and Qt 6 development libraries (Widgets;
Qt Test is also needed when tests are enabled). For example, Arch Linux provides
these in `base-devel`, `cmake`, and `qt6-base`.

Open the **Z80Editor** directory as a new CMake project in CLion, select the
`Z80Editor` run configuration, and build/run. CMake builds Z80Test from its
sibling project automatically; the original simulator's sources remain in that
project.

Or use a terminal:

```sh
cmake -S . -B build
cmake --build build -j
./build/Z80Editor
./build/Z80Editor ../Z80Test/examples/next.asm
ctest --test-dir build --output-on-failure
```

If Z80Test is elsewhere:

```sh
cmake -S . -B build -DZ80TEST_SOURCE_DIR=/path/to/Z80Test
```

To build without Qt Test, add `-DBUILD_TESTING=OFF` at configure time. The editor
uses C++17 and Qt; the simulator remains C17.

## Editing and running

The app reopens the last successfully opened or saved file on startup. A file
passed on the command line takes precedence. If the last file is unavailable,
the app starts with an untitled document.

- **Ctrl+N**, **Ctrl+O**, **Ctrl+S**, **Ctrl+Shift+S**: new, open, save, save as.
- **Ctrl+Z** / **Ctrl+Shift+Z**: undo / redo.
- **Ctrl+F**, **F3**: find and find next, wrapping at the end of the document.
- **F5**: run, or continue a paused stepping session; **Shift+F5**: stop.
- **F10**: step over one instruction, running CALL statements through their
  return (including nested calls). **F11**: step one instruction, entering CALLs.
  The first press starts from the initial registers and executes one instruction.
  The next instruction is highlighted; registers, flags, instruction count,
  T-states and simulated time refresh after each step.
- Stepping preserves memory and CPU state. Editing source or changing initial
  registers, clock or step limit resets the stepping session. BREAK, errors,
  end of code and the cumulative step limit end it; the next step starts again.
  Long calls and F5 continuation stay cancellable with Shift+F5.
- The editor provides line numbers, Z80/Z80N syntax highlighting, and four-space
  tab indentation. Assembly files are UTF-8 text.
- Set initial registers in the Simulation section, e.g.
  `A=10 BC=0x1234 SP=0xfffe AF'=0x1234`. Omitted registers default to zero.
  Use `PC=0x8000` for code assembled at `ORG $8000`. Register inputs are passed
  as separate process arguments; there is no shell expansion or need to quote
  dollar signs or alternate-register apostrophes.
- Set the clock frequency and step limit before running. End programs with
  `BREAK`; `HALT` waits and eventually reaches the step limit in this simulator.
- The Simulator field defaults to the Z80Test executable built with this project.
  Browse to a different Z80Test executable if desired. Changes to simulator
  sources are picked up when you rebuild the editor project.
  Select **Use bundled** to restore execution overlays if a saved external
  simulator path is selected. The hint below the field shows whether overlays
  are enabled; external executables provide final results only.

F5 writes a temporary snapshot of the editor text and runs the bundled simulator
core in short batches so the UI stays responsive. A custom Simulator executable
is still launched asynchronously, with its results shown after completion.
It works with unsaved files and unsaved changes without overwriting an existing
source file. The temporary source is removed after completion. You can continue
editing during execution; the status bar warns when the results are for an older
snapshot. The editor can run one simulation at a time.

Results include both normal output and errors. A reported source-line error
highlights the corresponding line in the editor when the text still matches the
run snapshot. Use **Copy results** to copy the entire output. Clock, step limit,
register inputs, simulator path, and window layout are remembered between sessions.
File operations and closing the application prompt to save modified source.

Stepping and runs with the bundled simulator paint read-only annotations beside
executed statements. They show changed registers and memory bytes in hexadecimal,
plus the instruction's T-states. Each line shows its latest execution in a loop.
Annotations are drawn over the editor and never become source text, saved content,
or undo entries. Editing source clears them, and a fresh run or stepping session
starts with empty annotations. Continuing a paused session keeps its annotations.
Custom external simulator executables provide final results only.

This editor uses the instruction set and hardware limitations of Z80Test. Stepping uses the simulator core built from the configured Z80Test source project;
the Simulator executable field selects the backend for fresh F5 runs.

## Verification

The Qt integration tests launch the real Z80Test executable and verify F5,
unsaved-source execution, register arguments, error-line highlighting, step limits,
cancellation, preservation of the original source file, single stepping, nested
and conditional CALLs, memory persistence, continuation, and debug session resets. They run with Qt's
`offscreen` platform so no display is needed for CTest.

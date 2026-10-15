# Optional profile-guided Release build

The normal `build.cmd release` remains an unprofiled optimized
build and runs the full automated suite. PGO is optional and never changes
guest clocks or instruction semantics.

From a Git checkout with the configured MSVC 2026 x86 Build Tools:

```bat
build.cmd release
tools\build-pgo.cmd instrument
tools\build-pgo.cmd train
tools\build-pgo.cmd optimize
```

The instrumented GUI lives in **`build/pgo/sghemu-gui.exe`**. Model descriptors
are copied there from tracked source files; ROMs, DLLs and existing saved state
are not copied. Open your firmware folder normally and, if wanted, configure
your Yamaha DLL. This training instance uses its own settings/NVM below
`build/pgo/`, not the regular emulator's saved state.

Exercise boot, menus, games, audio and other representative operations, then
close the training GUI normally so its counters are written. Do not use the
instrumented executable for speed measurements: instrumentation is expensive.
`train` launches it with the x86 PGO runtime from the compiler environment on
PATH. That development runtime is not bundled or required by the optimized GUI.

`optimize` merges the counters and replaces the executable at the **same PGO
path**, leaving the normal `build/sghemu-gui.exe` untouched. Review linker output
for mismatched/stale profiles and benchmark the resulting executable. Do not
assume every profile improves every workload. Re-instrument/retrain after source
changes; do not mix databases from different builds or firmware workloads without
deliberately collecting representative coverage.

Once verified, the optimized executable alone can replace `build/sghemu-gui.exe`
in a regular installation. Its dependencies remain ordinary Windows system DLLs;
PGO databases/counters and training settings stay local; ROMs and Yamaha
libraries are not bundled with project releases.

For compiler experiments after tests have already passed,
`build.cmd release gui-only` builds only the GUI; it does **not** run the suite.
Use a complete Release build for source-change verification.

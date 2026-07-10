# pd-playbench

`pd-playbench` is a small development-only C library for scripted gameplay
replay and performance benchmarking in Playdate emulator ports.

It exists to make performance tests deterministic and repeatable. Instead of
driving an emulator with real Playdate button presses, a test can replay the
same frame-based input script every time, collect timing data, and print or save
a report that can be compared across emulator builds and Playdate hardware
revisions.

This is especially useful when comparing Rev A and Rev B Playdate devices,
where small CPU and performance differences can affect emulator frame pacing.

The library is intended for projects such as:

- `pd-rom-picker`
- `pokemini/platform/playdate`
- `nofrendo`
- `vecx`
- `red-viper`

It is not meant for production builds. Compile it only in development builds by
defining `PD_PLAYBENCH_ENABLED`, or omit `src/pd_playbench.c` from release
builds entirely.

## Layout

```text
pd-playbench/
  src/
    pd_playbench.c
    pd_playbench.h
  examples/
    basic_script.c
    nes_smb1_smoke_test.txt
  docs/
    script_format.md
    integration.md
```

## Add As A Submodule

From a Playdate emulator project:

```sh
git submodule add https://github.com/pomettini/pd-playbench.git third_party/pd-playbench
git submodule update --init --recursive
```

Then add the header path and implementation file to your development build:

```make
CFLAGS += -DPD_PLAYBENCH_ENABLED
CFLAGS += -Ithird_party/pd-playbench/src
SRC += third_party/pd-playbench/src/pd_playbench.c
```

For release builds, leave out `-DPD_PLAYBENCH_ENABLED` and do not compile
`pd_playbench.c`.

## Basic Integration

```c
#ifdef PD_PLAYBENCH_ENABLED
#include "pd_playbench.h"
#endif

static const char* bench_script =
    "wait 120\n"
    "press A 6\n"
    "wait 300\n"
    "hold RIGHT 180\n"
    "hold RIGHT+B 120\n"
    "press A 8\n"
    "hold RIGHT+B 240\n"
    "wait 60\n"
    "stop\n";

void init_benchmark(PlaydateAPI* playdate)
{
#ifdef PD_PLAYBENCH_ENABLED
    PDBenchConfig config = {
        .test_name = "nes_smb1_smoke",
        .emulator_name = "nofrendo",
        .rom_name = "Super Mario Bros",
        .build_label = "dev",
        .target_fps = 60,
        .log_to_console = 1,
        .write_report_file = 1,
        .report_path = "benchmarks/latest.txt",
        .input_mode = PD_PLAYBENCH_INPUT_OVERRIDE
    };

    pd_playbench_init(playdate, &config);
    pd_playbench_load_script_from_string(bench_script);
    pd_playbench_start();
#endif
}
```

In the emulator update loop:

```c
PDButtons current;
PDButtons pushed;
PDButtons released;

playdate->system->getButtonState(&current, &pushed, &released);

#ifdef PD_PLAYBENCH_ENABLED
pd_playbench_update();
current = pd_playbench_get_buttons(current);
#endif

emulator_set_input(current);
```

After each emulated frame:

```c
#ifdef PD_PLAYBENCH_ENABLED
pd_playbench_report_frame(frame_time_ms, skipped_frames);
#endif
```

If the emulator cannot report skipped frames, pass `0`.

When the script reaches `stop`, the benchmark stops automatically and prints or
saves the report according to the config.

## Script Example

```text
# Super Mario Bros smoke test
wait 120
press A 6
wait 300
hold RIGHT 180
hold RIGHT+B 120
press A 8
hold RIGHT+B 240
wait 60
stop
```

Supported commands:

- `wait <frames>`
- `press <buttons> <frames>`
- `hold <buttons> <frames>`
- `release`
- `stop`
- `wait_seconds <seconds>`
- `press_seconds <buttons> <seconds>`
- `hold_seconds <buttons> <seconds>`

Button combinations use `+`, for example `RIGHT+B` or `A+B`.

## Reports

Console output is key/value text:

```text
pd-playbench report
test=nes_smb1_smoke
emulator=nofrendo
rom=Super Mario Bros
build=dev
device=device
target_fps=60
total_frames=720
measured_frames=720
elapsed_ms=12345.67
avg_frame_ms=17.15
best_frame_ms=12.10
worst_frame_ms=44.80
slow_frames=12
skipped_frames=3
avg_skipped_per_second=0.24
estimated_fps=58.31
```

Reports can also be written to a Playdate data file with
`pd_playbench_save_report("benchmarks/latest.txt")`. The directory must already
exist.

## Input Modes

`pd-playbench` supports three modes:

- `PD_PLAYBENCH_INPUT_OVERRIDE`: scripted input replaces real input.
- `PD_PLAYBENCH_INPUT_MERGE`: scripted input is ORed with real input.
- `PD_PLAYBENCH_INPUT_MANUAL_TRIGGER`: waits for a trigger button, then runs in
  deterministic override mode.

Override mode is the default because benchmarking should not depend on accidental
real button input.

## Recording

You can capture a live play session into a script instead of writing one by
hand. Recording emits the exact same `wait`/`hold`/`stop` format the parser
reads, so a recording round-trips straight back into replay.

```c
void init_recording(void) {
    pd_playbench_record_start();
}

// In the update loop, once per frame:
PDButtons current;
playdate->system->getButtonState(&current, NULL, NULL);
pd_playbench_record_sample(current);

// When done (e.g. a menu action):
pd_playbench_record_save("scripts/mysession.txt");
// or: const char* script = pd_playbench_record_string();
```

The library records the six standard `PDButtons`. Map any non-standard input to
`PDButtons` before calling `record_sample` — e.g. an emulator that puts NES Start
on the crank records it as the `UP` bit and maps `UP` back to Start on replay.

**Deterministic replay requires a deterministic host.** For a recording to
replay identically (including across Rev A/Rev B), the host must remove all
wall-clock-driven behaviour from the emulated run while recording *and*
replaying — most importantly **disable frameskip** (skipped frames often take a
different code path) and **advance audio by a fixed number of samples per frame**
rather than by elapsed real time. Any real-time input desyncs a replay from its
recording.

## Notes

- No dynamic allocation is used.
- Scripts are parsed into a fixed command buffer; recording reuses the same
  buffer, so a recording is capped at `PD_PLAYBENCH_MAX_COMMANDS` runs (raise it
  with `-DPD_PLAYBENCH_MAX_COMMANDS=...` for long sessions).
- Everything runs in frame counts internally.
- `MENU` is accepted by the script parser, but it does not map to a normal
  `PDButtons` bit in the standard Playdate C API.
- Hardware revision is not detected by this first version; include it in
  `device_label` or `build_label` when you want Rev A/Rev B comparison labels.

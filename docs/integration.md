# Integration

`pd-playbench` is intended to be added to emulator ports as a development-only
submodule.

## Build Setup

Add the submodule:

```sh
git submodule add https://github.com/pomettini/pd-playbench.git third_party/pd-playbench
```

For development builds:

```make
CFLAGS += -DPD_PLAYBENCH_ENABLED
CFLAGS += -Ithird_party/pd-playbench/src
SRC += third_party/pd-playbench/src/pd_playbench.c
```

For release builds, omit both `PD_PLAYBENCH_ENABLED` and `pd_playbench.c`.

The public header provides no-op inline functions when `PD_PLAYBENCH_ENABLED` is
not defined, but most projects should still wrap benchmark-only code in
`#ifdef PD_PLAYBENCH_ENABLED`.

## Initialize

```c
#ifdef PD_PLAYBENCH_ENABLED
PDBenchConfig bench_config = {
    .test_name = "nes_smb1_world_1_1",
    .emulator_name = "nofrendo",
    .rom_name = "Super Mario Bros",
    .build_label = "dev",
    .device_label = "Rev B",
    .target_fps = 60,
    .log_to_console = 1,
    .write_report_file = 1,
    .report_path = "benchmarks/latest.txt",
    .input_mode = PD_PLAYBENCH_INPUT_OVERRIDE
};

pd_playbench_init(playdate, &bench_config);
pd_playbench_load_script_from_file("scripts/nes_smb1_smoke_test.txt");
pd_playbench_start();
#endif
```

Use `device_label` or `build_label` to record Rev A/Rev B labels. This first
version does not attempt to detect hardware revision automatically.

## Input Loop

Call `pd_playbench_update()` once per Playdate update, then replace or merge the
real buttons with the scripted buttons.

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

The default input mode is `PD_PLAYBENCH_INPUT_OVERRIDE`, which ignores real input
while the script is running.

To merge input:

```c
pd_playbench_set_input_mode(PD_PLAYBENCH_INPUT_MERGE);
```

To arm the script and start it only after a manual trigger:

```c
pd_playbench_set_input_mode(PD_PLAYBENCH_INPUT_MANUAL_TRIGGER);
pd_playbench_set_manual_trigger_button(kButtonA);
pd_playbench_start();
```

When the trigger button is seen by `pd_playbench_get_buttons()`, the benchmark
starts and then runs in deterministic override mode.

## Reporting Frames

Call this after each emulated frame:

```c
#ifdef PD_PLAYBENCH_ENABLED
pd_playbench_report_frame(frame_time_ms, skipped_frames);
#endif
```

If the emulator cannot provide skipped frames, pass `0`.

By default, measurement starts when the benchmark starts. You can reset and
control the measured region explicitly:

```c
pd_playbench_begin_measurement();
/* measured frames */
pd_playbench_end_measurement();
```

## Report Output

Console report:

```c
pd_playbench_print_report();
```

File report:

```c
pd_playbench_save_report("benchmarks/latest.txt");
```

The output is key/value text so it is easy to diff, archive, or paste into a
spreadsheet.

The file directory must already exist in the Playdate data filesystem.

# Script Format

`pd-playbench` scripts are plain text. They can be embedded as C strings or
loaded from a file in the Playdate bundle.

Each command is one line. Blank lines and comments are ignored.

```text
# comment
wait 120
press A 6
hold RIGHT+B 240
release
stop
```

## Commands

### `wait <frames>`

Runs for the given number of frames with no scripted buttons held.

```text
wait 120
```

### `press <buttons> <frames>`

Holds the buttons for the given number of frames. `press` and `hold` are
currently equivalent; both are frame-based button states.

```text
press A 6
press A+B 10
```

### `hold <buttons> <frames>`

Holds the buttons for the given number of frames.

```text
hold RIGHT 180
hold RIGHT+B 240
hold LEFT+A 30
```

### `release`

Runs one frame with no scripted buttons held.

```text
release
```

### `stop`

Stops the benchmark. If console logging or file output is enabled, this also
prints or saves the report.

```text
stop
```

### Seconds Commands

Seconds commands are converted to frame counts using `PDBenchConfig.target_fps`.

```text
wait_seconds 2.0
hold_seconds RIGHT 3.5
press_seconds A+B 0.2
```

## Buttons

Supported names:

- `LEFT`
- `RIGHT`
- `UP`
- `DOWN`
- `A`
- `B`
- `MENU`
- `NONE`

Button names are case-insensitive. Combinations use `+`.

```text
hold RIGHT+B 240
press A+B 10
hold LEFT+A 30
```

`MENU` is accepted by the parser, but the standard Playdate C `PDButtons` value
does not expose a normal menu button bit. In this first version it has no effect
on the value returned by `pd_playbench_get_buttons()`.

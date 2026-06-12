# Debug Journal

## 2026-06-12 - cosmos-gui Launcher dies with EPERM right after the legal agreement

### Symptom

On a fresh clone (full `make prep` + `make config` + `make`), `make cosmos-gui`
brings up the COSMOS legal-agreement dialog; the user clicks Ok and then no
NOS3 Launcher window ever appears. `docker ps -a` showed
`cosmos-openc3-operator-1` (image `ballaerospace/cosmos:4.5.0`,
CMD `/bin/sh -c 'ruby Launcher'`) as `Exited (0)` ~17 s after start. Exit code
0 (clean), so not a crash.

### Root cause

A foreground repro with a TTY surfaced the real error (COSMOS swallows it under
the detached `-dit` launch and only writes FATAL to the container log):

```
FATAL: EPERM : Operation not permitted
gui/qt_tool.rb:52:in `setpgrp'
gui/qt_tool.rb:52:in `initialize'
tools/launcher/launcher.rb:25:in `initialize'   # main window, after the legal dialog
```

`QtTool#initialize` runs `Process.setpgrp` (== `setpgid(0,0)`). That is EPERM
when the calling process is a **session leader**. The image CMD
`sh -c 'ruby Launcher'` is exec-optimised by dash into `ruby`, so with the `-t`
TTY allocated by `docker run -dit`, ruby becomes the session-leading PID 1 ->
`setpgrp` is illegal -> the Launcher dies the instant the main window is built
(immediately after the legal dialog the user just accepted).

Confirmed in isolation (TTY via `script -qc`):
- `docker run --rm -it cosmos ruby -e 'Process.setpgrp'`        -> `Errno::EPERM`
- `docker run --rm -it --init cosmos ruby -e 'Process.setpgrp'` -> OK (pid=7)

The System config was ruled out first: loading `system.txt` + all 43 targets
from the committed config with a fresh empty `outputs/` returned
`SYSTEM_OK targets=43`. So the bug is purely the PID-1/session-leader condition,
not config. The GUI tools the Launcher itself spawns (CmdSender, PacketViewer)
were never affected because they run as children of the Launcher, not as PID 1.

### Fix

Add `--init` to the cosmos-gui `docker run` in `scripts/ci_launch.sh`. tini
becomes PID 1 and `ruby Launcher` runs as its child, so `setpgrp` is allowed.
This is load-bearing: removing `--init` reintroduces the EPERM crash.

Two supporting changes in the same branch (made first, while narrowing the
cause): pre-create the gitignored `gsw/cosmos/outputs/{logs,tmp,saved_config,
...}` dirs, and replace the blind `sleep 20` + "you're too late" UX with a
`cosmos_gui_failed()` guard that dumps the exit code, `docker logs` tail, and
the newest `outputs/logs/*exception*.txt` if the container exits during
startup.

### Note

This likely never worked end-to-end on the GUI main window; the recent
cosmos-gui commits (networking, headless CmdTlmServer) did not exercise the
QtTool main-window path, so the EPERM went unnoticed.

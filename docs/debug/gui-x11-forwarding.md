# GUI and X11 forwarding (VS Code devcontainer)

This repo runs in a VS Code Remote Containers devcontainer with
Docker-in-Docker, not raw Docker Desktop. The X11 plumbing is:

1. The VS Code Server inside the devcontainer runs a node process that owns
   `/tmp/.X11-unix/X0` and tunnels the X protocol back to the user's Windows
   host. Verify with `pgrep -af 'vscode-remote-containers-server'`.
2. VS Code injects `DISPLAY=:0` (and `WAYLAND_DISPLAY=...`) into every shell
   it spawns inside the devcontainer, including non-interactive shells. Out-
   of-band shells (cron, `docker exec` into a side container, ssh without X)
   do not get `DISPLAY`.
3. `nos3/scripts/ci_launch.sh` forwards the host `DISPLAY` and bind-mounts
   `/tmp/.X11-unix:ro` into the 42 container, so 42 connects through the same
   tunnel.

Implications:

- `make launch` works from VS Code integrated terminals. In a shell that
  genuinely has no `DISPLAY`, the early guard in `ci_launch.sh` exits with an
  actionable message; export `DISPLAY=:0` or use `NOS3_HEADLESS=1`.
- For CI or true headless contexts, set `NOS3_HEADLESS=1` to skip `xhost` and
  the X mounts. 42 starts without GUI windows. This path is best-effort
  headless, not a fully validated mode.
- Do not add `runArgs`, `containerEnv`, `/mnt/wslg` bind mounts, or manual
  `DISPLAY=:0` / `WAYLAND_DISPLAY=` entries to
  `.devcontainer/devcontainer.json` to "fix" missing X. The devcontainer is
  correct as-is: VS Code provides X via the node tunnel, and explicit
  WSLg / Docker-Desktop plumbing would shadow it and require a rebuild for no
  benefit (see the historical regression below).

The `ci_launch.sh` DISPLAY guard and the `NOS3_HEADLESS` branch around the 42
docker run are load-bearing. Do not delete the guard to make launch quieter:
its job is to fail fast with an actionable message in any shell that has no
`DISPLAY`, instead of running ~180 lines and aborting obscurely on
`xhost +local:*`. Do not wrap `xhost +local:*` in `2>/dev/null || true`; the
unwrapped form is intentional so a real X-server outage is loud, not silent.

## Diagnostic ladder

Run inside the devcontainer, in a terminal that should have working X:

```bash
echo "$DISPLAY"                                  # expect :0
ls -la /tmp/.X11-unix/                           # expect X0 socket present
ss -lxn | grep /tmp/.X11-unix/X0                 # expect a LISTEN row
pgrep -af 'vscode-remote-containers-server'      # expect a node PID
xhost                                            # expect "access control disabled, ..."
```

If `DISPLAY` is empty you are in a shell that did not inherit the tunnel;
open a fresh VS Code integrated terminal or `export DISPLAY=:0`. If the X0
socket has no LISTEN, enable VS Code X11 forwarding in user settings
(`dev.containers.forwardX11` / `remote.X11.forwardX11`) or reattach via
"Reopen in Container".

## Historical regression: do not add WSLg passthrough

- Symptom: `make launch` completes, all containers start, no errors, but the
  42 Cam, 42 Map, and 42 Unit Sphere Viewer windows do not appear.
- Root cause: a prior debugging session added a `runArgs` block mounting
  `/tmp/.X11-unix` plus a `containerEnv` block hard-setting `DISPLAY=:0` to
  `.devcontainer/devcontainer.json`, which collided with the host's
  automatic X forwarding and broke X inside the container; and it wrapped
  `xhost +local:*` in `2>/dev/null || true` in `ci_launch.sh`, hiding the
  "unable to open display" error so launch reported success while X was dead.
- Fix: revert both files; no host-side changes, `make clean`, or Docker
  rebuild needed. Confirm health with the diagnostic ladder above (an
  `xeyes` window proves X is healthy end-to-end).

## cosmos-gui Launcher dies with EPERM right after the legal agreement

This is load-bearing.

- Symptom: on a fresh clone, `make cosmos-gui` shows the COSMOS legal
  dialog; after clicking Ok no Launcher window appears.
  `cosmos-openc3-operator-1` (image `ballaerospace/cosmos:4.5.0`,
  CMD `ruby Launcher`) shows `Exited (0)` about 17 s after start.
- Root cause: a foreground repro with a TTY surfaces the swallowed error:
  `FATAL: EPERM : Operation not permitted` at `gui/qt_tool.rb:52 setpgrp`.
  `QtTool#initialize` runs `Process.setpgrp` (`setpgid(0,0)`), which is
  EPERM when the caller is a session leader. The image CMD `sh -c 'ruby
  Launcher'` is exec-optimised into `ruby`, so with the `-t` TTY from
  `docker run -dit`, ruby becomes the session-leading PID 1 and `setpgrp` is
  illegal, killing the Launcher as the main window is built. The System
  config was ruled out first (loading `system.txt` and all 43 targets from a
  fresh empty `outputs/` returned `SYSTEM_OK targets=43`). Child GUI tools
  (CmdSender, PacketViewer) were never affected because they run as children
  of the Launcher, not as PID 1.
  - `docker run --rm -it cosmos ruby -e 'Process.setpgrp'` raises
    `Errno::EPERM`; the same with `--init` succeeds.
- Fix: add `--init` to the cosmos-gui `docker run` in
  `scripts/ci_launch.sh`, so tini is PID 1 and `ruby Launcher` runs as its
  child. Removing `--init` reintroduces the crash. Two supporting changes in
  the same branch: pre-create the gitignored
  `gsw/cosmos/outputs/{logs,tmp,saved_config,...}` directories, and replace
  the blind `sleep 20` with a `cosmos_gui_failed()` guard that dumps the
  exit code, `docker logs` tail, and the newest
  `outputs/logs/*exception*.txt` if the container exits during startup.
- Note: this main-window path likely never worked end-to-end before; recent
  cosmos-gui commits exercised networking and the headless CmdTlmServer but
  not the QtTool main window, so the EPERM went unnoticed.

# NOS3 42 GUI Windows Not Appearing - Post-Mortem & Prevention

## Summary

After a Kibana-debugging session, `make launch` stopped producing the 42 simulator's GUI windows (42 Cam, 42 Map, 42 Unit Sphere Viewer). The launch itself completed without errors, but no windows appeared. Root cause: a well-intentioned change to `.devcontainer/devcontainer.json` added explicit WSLg passthrough mounts that overrode - and broke - Docker Desktop's automatic WSL2 X-forwarding integration. A second change to `nos3/scripts/ci_launch.sh` silently swallowed the resulting `xhost` error, hiding the failure.

## Symptoms

- `make launch` runs to completion. Containers start. No errors visible in output.
- 42 Cam, 42 Map, and 42 Unit Sphere Viewer windows do not appear on the Windows desktop.
- A friend cloning the same repo at `origin/main` and running `make launch` sees the windows correctly - proving the repo + stock Docker Desktop + WSLg are sufficient with no manual setup.

## Root Cause

Two changes, made during earlier debugging, combined to produce silent X11 failure inside the devcontainer:

### Change 1 - `.devcontainer/devcontainer.json` (the actual cause)

A `runArgs` block and `containerEnv` block were added that explicitly mount WSLg paths and hardcode display environment variables:

```json
"runArgs": [
    "--volume=/mnt/wslg:/mnt/wslg",
    "--volume=/mnt/wslg/.X11-unix:/tmp/.X11-unix",
    "--volume=/usr/lib/wsl:/usr/lib/wsl:ro",
    "--device=/dev/dxg"
],
"containerEnv": {
    "DISPLAY": ":0",
    "WAYLAND_DISPLAY": "wayland-0",
    "XDG_RUNTIME_DIR": "/mnt/wslg/runtime-dir",
    "LD_LIBRARY_PATH": "/usr/lib/wsl/lib"
}
```

**Why this broke things:** Docker Desktop with WSL2 integration already handles WSLg X-forwarding automatically. Containers get working access to the host's X server without any manual configuration. The explicit `runArgs` mount replaced Docker Desktop's working defaults with a manual mount that produced an unreachable X socket inside the devcontainer.

Verification of the breakage was simple:

```bash
xhost
# Output before fix: xhost: unable to open display ":0"
# Output after fix:  access control disabled, clients can connect from any host
```

### Change 2 - `nos3/scripts/ci_launch.sh` (the masking)

In the 42-container launch block, `xhost +local:*` was wrapped in `timeout 2 ... || true`:

```bash
# Before (origin/main, working):
xhost +local:*

# After (broken):
timeout 2 xhost +local:* >/dev/null 2>&1 || true
```

**Why this made debugging harder:** The original unwrapped `xhost` call would have failed loudly under `set -e`, immediately aborting `make launch` with a visible error pointing at X access. The wrapped version silently swallows the failure, so the script kept going and started the 42 container with `DISPLAY=:0` set but no working X socket - producing the silent "everything looks fine but no windows" symptom.

## The Fix

Revert both files to `origin/main`, rebuild the devcontainer:

```bash
# Revert devcontainer config
git checkout origin/main -- .devcontainer/devcontainer.json

# Revert the xhost wrapper (so future X failures fail loud)
git checkout origin/main -- nos3/scripts/ci_launch.sh
```

Then in VS Code: **Command Palette → Dev Containers: Rebuild Container** (regular rebuild, not "Rebuild Without Cache"). Takes 1–3 minutes.

After the new container is up, verify X is reachable:

```bash
echo $DISPLAY
# Expected: :0

xhost
# Expected: "access control disabled, clients can connect from any host"
# or:       "access control enabled, only authorized clients can connect"
# Either is fine. Anything that errors with "unable to open display" is broken.
```

Then:

```bash
cd nos3
make launch
```

42 GUI windows should reappear.

### What does *not* need rebuilding

`make launch` does **not** depend on `make all` / `make fsw` / `make sim` / `make gsw`. The build artifacts live in the workspace tree (`fsw/build/`, `sims/build/`, `gsw/build/`), which is bind-mounted and survives a devcontainer rebuild. **Do not run `make clean`** unless `make launch` actually complains about a missing artifact, in which case rebuild only the missing piece.

## The Debugging Journey That Wasn't

Worth recording because the wrong path was followed for several hours before the actual diagnosis.

The original assistant response to "GUIs not appearing in WSL" was to:

1. Suggest installing/configuring an external X server (VcXsrv, XLaunch).
2. Modify WSL configuration on the host.
3. Adjust Docker Desktop settings.
4. Add explicit WSLg passthrough mounts to `devcontainer.json`.
5. Repeatedly rebuild the Docker environment.

Step 4 is what actually broke a working setup. The first three were unnecessary in WSLg-enabled Docker Desktop and would have done nothing harmful, but step 4 replaced "implicit, working" with "explicit, broken." Step 5 then masked the regression by repeatedly applying it.

The clue that should have stopped the rabbit hole earlier was: **a friend's fresh clone of `main` worked with no manual setup.** That alone proves no host-side X11 configuration is needed. Once that data point exists, the only useful question is *what changed in the repo since the last working state* - not "what infrastructure can I add."

## Prevention

### Add this to the project root notes file

```markdown
## WSLg / X11 Forwarding (Windows + Docker Desktop)

WSLg X forwarding works automatically via Docker Desktop's WSL2 integration.

DO NOT:
- Add `runArgs` mounts for `/mnt/wslg`, `/tmp/.X11-unix`, `/usr/lib/wsl`, or `/dev/dxg` to `.devcontainer/devcontainer.json`.
- Set `DISPLAY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, or `LD_LIBRARY_PATH` in `containerEnv`.
- Suggest installing VcXsrv, XLaunch, or any external X server.
- Suggest editing `.wslconfig` or Docker Desktop settings to fix GUI issues.

Docker Desktop handles all WSLg passthrough correctly with default settings.
Adding explicit mounts will break X access from inside the devcontainer.

To verify X is working from the devcontainer:
    echo $DISPLAY    # should print :0
    xhost            # should print without "unable to open display"

If GUI windows are not appearing, the bug is almost certainly in repo
configuration that has been changed, not in the host setup.
```

### Debugging directive for future debugging sessions

When `make launch` worked before and stopped working:

1. **Confirm the working baseline still exists.** Have a colleague or a fresh clone run `make launch` from `origin/main`. If it works for them, the host setup is fine - the fix is reverting a delta in the repo, not adding infrastructure.
2. **Identify the delta.** `git diff origin/main -- .devcontainer/ nos3/scripts/ Makefile` to see what changed since the last known good state.
3. **Test cheaply before committing to a rebuild.** Inside the running devcontainer, `xhost` and `echo $DISPLAY` reveal X access status without touching infrastructure. If `xhost` errors, the devcontainer X access is the problem and rebuild is needed. If `xhost` works, the bug is downstream (script, inner container).
4. **Do not propose new infrastructure (X servers, WSL configs, Docker reconfigs) until the delta has been ruled out.**

### Lesson

> The fix to a regression is almost always reverting the change that caused it, not adding new infrastructure to compensate.

When something used to work and now doesn't, the working baseline is your strongest debugging asset - anchor on it before reaching for new tools.

## Diagnostic Commands Reference

For future X11 debugging in this repo:

```bash
# Inside devcontainer - is X reachable?
xhost
# Working:  "access control disabled..." or "access control enabled..."
# Broken:   "xhost: unable to open display"

# Are env vars set as WSLg expects?
echo "DISPLAY=$DISPLAY"
echo "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
echo "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
# Expected on WSLg + Docker Desktop:
#   DISPLAY=:0
#   WAYLAND_DISPLAY=wayland-0
#   XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir

# Is the X socket actually mounted and populated?
ls -la /tmp/.X11-unix/
# Expected: contains X0 (a unix socket file)

# Quick visual test (requires x11-apps; ephemeral install OK):
sudo apt-get install -y x11-apps
xeyes &
# If a window appears on the Windows desktop, X works end-to-end.
```

## Files Touched by This Fix

| File | Action |
|------|--------|
| `.devcontainer/devcontainer.json` | Reverted to `origin/main` (removed `runArgs` and `containerEnv` blocks) |
| `nos3/scripts/ci_launch.sh` | Reverted to `origin/main` (restored unwrapped `xhost +local:*`) |

No host-side changes. No `make clean`. No framework rebuild.

---

## Second Occurrence (2026-04-30) - Same Symptom, Different Cause

### Summary

Three days after the prevention notes were added to the project root notes file, the same symptom recurred: `make launch` apparently completed without errors but no 42 GUI windows appeared on the Windows desktop. The user's hypothesis was that the prior regression had been re-introduced. The hypothesis was wrong. The repo was clean and the load-bearing files were byte-identical to `origin/main`. The actual cause was a stale Docker container left over from a previous session, which caused `ci_launch.sh` to abort before ever reaching the 42 launch block.

### Symptoms (identical to first occurrence)

- `make launch` appeared to finish OK from the user's perspective.
- 42 Cam, 42 Map, 42 Unit Sphere Viewer did not appear.
- The user's own first-occurrence post-mortem was at hand and looked like the right starting point.

### Pre-flight checks ruled out the first-occurrence root cause

Before proposing any fix, a read-only investigation against `origin/main` was run. The first-occurrence diagnosis did NOT apply this time:

1. `git status` was clean. `HEAD == origin/main` at commit `0fda60f5 fix(elk): prevent Kibana hangs and implement orphan object purge`.
2. `git diff origin/main -- .devcontainer/devcontainer.json nos3/scripts/ci_launch.sh nos3/Makefile` was empty.
3. `.devcontainer/devcontainer.json` had no `runArgs` block and no `containerEnv` entries for `DISPLAY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, or `LD_LIBRARY_PATH`.
4. `nos3/scripts/ci_launch.sh:176` had the unwrapped `xhost +local:*` immediately before the 42 `docker run`. (Note: line 88 in the cosmos-gui block contains a wrapped `xhost +local:docker 2>/dev/null || true`; that is upstream behaviour and not on the 42 path.)
5. 42 graphics flags were all enabled and unchanged for weeks: `Inp_Sim.txt:7` (`Graphics Front End? = TRUE`), `Inp_Graphics.txt:4` (`Map Window Exists = TRUE`), `Inp_Graphics.txt:6` (`Unit Sphere Window Exists = TRUE`).
6. The recent commits (`94f840af`, `6c6b2029`, `a844d23d`, `4e2022fb`, `0fda60f5`) did not touch `.devcontainer/`, `Inp_Graphics.txt`, the 42-launch block of `ci_launch.sh`, or `make launch`. Commit `4e2022fb` did touch `Inp_IPC.txt` and the sims list in `ci_launch.sh`, but those are the load-bearing tightenings that keep 42 alive (per the project root "DO NOT REVERT" notes), not regressions.

So the first-occurrence prescription ("revert to origin/main") was a no-op: the files were already at origin/main.

### Diagnostic ladder

Following the post-mortem's own ladder:

1. `echo $DISPLAY` returned `:0`. `xhost` printed "access control disabled, clients can connect from any host". `/tmp/.X11-unix/X0` socket was present.
2. `xeyes &` stayed alive for 3 seconds without exiting, proving the X connection succeeded from the devcontainer side.
3. `docker ps -a --filter name=fortytwo` returned no rows. **This was the giveaway.** If `make launch` had actually launched 42 and 42 had then failed to draw windows, there would be a fortytwo container in some state (Up, Restarting, Exited). None existed at all, which meant 42 was never started in the first place.

### Root cause

Re-running `make launch` and capturing its full output (`make launch > /tmp/make_launch.log 2>&1`) revealed the abort point:

```
sc01 - Create spacecraft network...
sc01 - Connect GSW to spacecraft network...
Launch GSW...
Launching COSMOS...
docker: Error response from daemon: Conflict. The container name "/cosmos-openc3-operator-1"
        is already in use by container "2a67249977c5...".
make: *** [Makefile:316: launch] Error 125
```

A `cosmos-openc3-operator-1` container was already running from a previous session. `ci_launch.sh` aborted in the COSMOS launch block (around line 88), well before reaching the 42 launch block at line 176. Net result: 42 was never started; nothing to draw windows; the symptom looked identical to the first occurrence.

This was not visible to the user because:
- The lines BEFORE the abort included normal-looking ELK setup output (`Truncating attack_logs/cfs_god_view.json and omni_logs/*.log...`, `Elasticsearch is green`, `Deleting nos3-telemetry-* indices so Kibana starts clean...`) which gave the impression the launch was healthy.
- `make stop` had been run earlier in the session but did not remove `cosmos-openc3-operator-1`.

### Fix

Two commands:

```bash
docker rm -f cosmos-openc3-operator-1
cd nos3 && make launch
```

### Validation

After the fix:
- `sc01-fortytwo` came up and stayed `Up` for 90+ seconds.
- 42 logs showed the normal startup sequence: mesh material warnings (`Material material_X is not in Matl structure / Setting material to default` - these are expected and are not errors), `Reached CmdScript EOF at Time = 0.000000`, then `Server is listening on port 4278/4277/4378/4377/4478/4477/4279/4245/4227/4234/9999` followed by `Server side of socket established` for each.
- No `cannot open display`. No `Bogus input`. No IPC `WriteToSocket exit(1)`. No segfault.
- All 28 NOS3 containers were `Up`: sc01-fortytwo, sc01-nos-fsw, sc01-nos-engine-server, sc01-truth42sim, sc01-blackboard-sim, sc01-gps, sc01-camsim, sc01-sample-sim, the 14 generic-* sims, the time/sim-bridge/terminal containers, cosmos-openc3-operator-1, and the ELK trio.
- Inside the 42 container, `DISPLAY=:0` was set and `/tmp/.X11-unix/X0` was bind-mounted. `ps -ef` showed `/home/vscode/.nos3/42/42 NOS3InOut` as PID 1.
- The user visually confirmed all three GUI windows (42 Cam, 42 Map, 42 Unit Sphere Viewer) were rendering on the Windows desktop.

### What `make stop` missed

`make stop` runs `scripts/stop.sh`, which kills the per-spacecraft sim and FSW containers but leaves `cosmos-openc3-operator-1` running. (COSMOS is managed by a separate docker compose project and is intentionally not torn down by stop.sh.) That single survivor was enough to break the next `make launch` because COSMOS is launched again by name in `ci_launch.sh` (around line 88), which fails with `docker: Error response from daemon: Conflict`.

Possible follow-up to prevent recurrence (not done as part of this incident; record only):
- Extend `scripts/stop.sh` to also remove `cosmos-openc3-operator-1` (and any other compose-managed survivors), or have `ci_launch.sh` `docker rm -f` known names defensively before `docker run`.

### Updated lesson

Two lessons from this incident, in order of priority:

1. **Same symptom, very different cause.** When 42 GUI windows fail to appear, the FIRST question is not "what did I change in the repo" but "did 42 actually start?" `docker ps -a --filter name=fortytwo --format 'table {{.Names}}\t{{.Status}}'` answers that in one command. If the container row is absent or the container is `Exited`, the launch failed before or during 42 startup, and reverting files will not help. Always run the diagnostic ladder before reaching for `git checkout`.

2. **Stale state from previous sessions is a real failure mode.** `make stop` does not clean up everything. Before re-running `make launch`, glance at `docker ps -a` for survivors that could conflict with new container names. The first thing a fresh launch tries to do is `docker run --name <X>`, and any pre-existing container named `<X>` will abort the launch.

The first-occurrence lesson ("the working baseline is your strongest debugging asset") still holds, but the *second-occurrence variant* of it is: when your repo IS the baseline (already at `origin/main`, working tree clean), the question shifts from "what did I change" to "what state outside the repo is wrong." Look at the runtime: containers, mounts, env, host service health.

### Updated diagnostic protocol

Replace step 1 of the original "Debugging directive for future debugging sessions" section above with:

1. **Check whether 42 actually started.** Run:
    ```bash
    docker ps -a --filter name=fortytwo --format 'table {{.Names}}\t{{.Status}}'
    ```
   - If the row is missing or the container is `Exited`, the launch failed before or during 42 startup. Re-run `make launch > /tmp/make_launch.log 2>&1` and inspect for an early abort. The most common cause is a stale container from a previous session blocking name reuse (typically `cosmos-openc3-operator-1` survives `make stop`). Fix: `docker rm -f <conflicting_container>` and rerun `make launch`.
   - If the row shows `Up`, then proceed to the X11 diagnostic ladder (the original steps 2-4 above).

### Files touched by this fix

None in the repo. The fix was a single `docker rm -f cosmos-openc3-operator-1` followed by `make launch`. The repo configuration was already correct.

---

## Third Occurrence (2026-05-06) - WSLg off-screen window placement

### Summary

Same surface symptom as before (`make launch` finishes, no 42 GUI windows on Windows desktop), but the cause is neither devcontainer regression (first occurrence) nor stale container (second occurrence). The X stack inside the devcontainer is fully healthy: protocol round-trips, every X client connects, windows ARE created. They are placed at absolute coordinates `+-32730+-32709` (close to `INT16_MIN`) inside a parent at `INT16_MIN`, so they render to a virtual location that is not on any visible monitor. The defect lives in WSLg / XWayland on the Windows host, not in the repo.

### Symptoms (subtly different from prior occurrences)

- `DISPLAY=:0` is set in the VS Code integrated terminal.
- `xeyes` does not return to the prompt and does not error -- it hangs alive (visible in `ps`) but no window appears.
- `make launch` would behave identically: 42 starts, runs, and produces no visible windows.

### Pre-flight checks ruled out the prior root causes

In a read-only ladder before any action:

1. `git diff origin/main -- .devcontainer/devcontainer.json` was empty -- first-occurrence regression does not apply.
2. `docker ps -a` was empty -- no stale `cosmos-openc3-operator-1` to conflict; second-occurrence cause does not apply either.
3. `nos3/scripts/ci_launch.sh` had working-tree edits from the active feature branch but preserved the load-bearing pieces (early `DISPLAY` guard, unwrapped `xhost +local:*` on the GUI path, `NOS3_HEADLESS` branch).

### Diagnostic ladder that found the off-screen placement

```bash
echo $DISPLAY                                     # :0
ls /tmp/.X11-unix/                                # X0 socket present
ss -lxn | grep /tmp/.X11-unix/X0                  # u_str LISTEN ...
pgrep -af 'vscode-remote-containers-server'       # node PID alive
xhost                                             # access control disabled, ...
xdpyinfo | head -20                               # vendor: Microsoft Corporation
                                                  # full X round-trip succeeds
xset q | head -5                                  # X server responds with full config

xeyes &
XPID=$!
sleep 2
ps -p $XPID -o pid,stat,cmd                       # alive in 'S' state
xwininfo -root -tree | grep xeyes
# 0x60000a "xeyes": ...  150x100+38+59  +-32730+-32709
#                                       ^^^^^^^^^^^^^^^
# This is the absolute on-root position. -32730,-32709 is OFF-SCREEN.

xclock -geometry 200x200+300+300 &
sleep 2; xwininfo -root -tree | grep xclock
# 0x... "xclock": ...  200x200+38+59  +-32730+-32709
# Same exact off-screen coords. Forced -geometry was ignored at absolute level.

xlogo &; sleep 2; xwininfo -root -tree | grep xlogo
# Same coords. Universal across X clients.
```

The signature pattern is: every X client lands at absolute `+-32730+-32709`, parent at `INT16_MIN`, regardless of `-geometry` hints.

### Root cause

WSLg / XWayland (or the VS Code Wayland-X bridge currently in path) is placing X clients into a virtual parent at `INT16_MIN`, so all windows render off any visible monitor. The chain is:

devcontainer X client -> /tmp/.X11-unix/X0 (in devcontainer) -> VS Code Remote-Containers tunnel -> WSL2 host -> WSLg / XWayland -> Windows compositor

Inside the devcontainer, every step responds correctly. The breakdown is at the WSLg/XWayland or Windows compositor stage on the host. Possible triggers (cannot be confirmed from inside the devcontainer):

- A WSLg version that has a placement regression (`wsl --update` may resolve)
- A Windows display configuration change (monitor unplugged, scaling changed, virtual desktop swap) confused WSLg's coordinate space
- A VS Code Windows-host build with a Wayland bridge regression
- Long-running VS Code or WSL session in a stuck state

### Fix (Windows-side)

In increasing cost; stop as soon as a fresh `xeyes` test shows a window inside `(0,0)..(1440,900)`:

1. Windows PowerShell: `wsl --shutdown`. Reopen VS Code and the devcontainer. Verify with the snippet below.
2. Windows PowerShell: `wsl --update`, then `wsl --shutdown`. Reopen.
3. Windows display settings: confirm only intended monitors are active and scaling is at a known-good value. Restart WSL.
4. Help -> Check for Updates in VS Code on the Windows host. Reopen container.
5. Reboot Windows.

### Verification command

After any attempt, from a fresh VS Code integrated terminal in the devcontainer:

```bash
xeyes -geometry 200x200+200+200 &
sleep 2
xwininfo -root -tree | grep xeyes
# Working:  absolute coords like +200+200 (somewhere inside 0..1440 x 0..900)
# Broken:   +-32730+-32709
kill %1 2>/dev/null
```

A visible window on the Windows desktop is the gold standard; the `xwininfo` coordinate is the fast proxy.

### Workaround if Windows-side fix has to wait

```bash
NOS3_HEADLESS=1 make launch
```

The `NOS3_HEADLESS` branch in `ci_launch.sh` skips the X mount and DISPLAY pass-through to the 42 container, so FSW, sims, and ELK still come up. No GUI windows, but the rest of the stack is unblocked.

### Updated diagnostic protocol

Before reaching for `git checkout`, `docker rm -f`, or any infrastructure change, run THIS triage in order:

1. `docker ps -a --filter name=fortytwo` -- did 42 actually start? (covers second-occurrence cause)
2. `git diff origin/main -- .devcontainer/devcontainer.json` -- is the devcontainer clean? (covers first-occurrence cause)
3. `xdpyinfo | head -5` -- does the X server round-trip? If yes, devcontainer X is fine.
4. `xeyes &; sleep 2; xwininfo -root -tree | grep xeyes` -- where is the window placed? If absolute coords are `+-32730+-32709` or any value far outside the root window dimensions, the bug is WSLg-side (third-occurrence cause).

The fix is determined entirely by which step first produces a non-healthy result. Do not start editing files until the failing step is identified.

### Files touched by this fix

None in the repo. The fix is entirely Windows-side (WSLg/VS Code/host display).

# WSL2 build knobs on Windows: quickstart

There are two separate knobs that control how heavy a NOS3 build feels on
a Windows + WSL2 host:

1. A Linux-side CPU knob in this repo (`NUM_CPUS`). Controls how many cores
   the build container gets and how many parallel compile jobs run inside
   it.
2. A Windows-side memory/disk knob in `%USERPROFILE%\.wslconfig`. Controls
   how much RAM the entire WSL2 VM can take from Windows, and whether it
   gives RAM and disk back when you are done.

They are independent. Use both.

## Part A: the `NUM_CPUS` shortcut (Linux side, in this repo)

### What it does

`NUM_CPUS` is an environment variable read by the repo's build scripts. It
does two things at once with the same number:

- Caps the build container's CPU allotment with `docker run --cpus=N`.
- Passes `make -j N` into the container so the in-container compile runs
  that many jobs in parallel.

It does **not** cap memory. There is no `--memory=` flag in `env.sh` or
any script under `nos3/scripts/`. Memory on Windows is a `.wslconfig`
problem (Part B).

### Where it is defined

`nos3/scripts/env.sh:20`:

```bash
NUM_CPUS="${NUM_CPUS:-$( nproc )}"
```

If `NUM_CPUS` is unset, it defaults to all available cores (`nproc`).

`nos3/scripts/env.sh:54` then builds the Docker flag bundle:

```bash
DFLAGS_CPUS="$DFLAGS --cpus=$NUM_CPUS"
```

### Where it is consumed

Every build script that spawns the build container uses `DFLAGS_CPUS` and
`-j$NUM_CPUS` together. For example, `nos3/scripts/fsw/fsw_cfs_build.sh:32`:

```bash
$DFLAGS_CPUS -v $BASE_DIR:$BASE_DIR --name "nos_build_fsw" \
    -w $BASE_DIR $DBOX make -j$NUM_CPUS -e FLIGHT_SOFTWARE=cfs build-fsw
```

Same pattern in:

- `nos3/scripts/fsw/fsw_fprime_build.sh:31`
- `nos3/scripts/build_sim.sh:29`
- `nos3/scripts/gsw/build_cryptolib.sh:33`
- `nos3/scripts/cfg/prepare.sh:30`
- `nos3/scripts/debug.sh:12` (interactive shell)

### Usage

```bash
cd nos3
NUM_CPUS=6 make fsw        # Build FSW with 6 cores
NUM_CPUS=2 make            # Slow, gentle full build (fits in ~4 GB RAM)
make                       # Use nproc (all cores)
```

Rule of thumb: leave 2 cores free for VS Code, ELK, and simulators. On a
16-thread host, `NUM_CPUS=6` to `NUM_CPUS=8` is a good sweet spot.

## Part B: the Windows `.wslconfig` (memory side)

### Why you cannot find the file in File Explorer

You are not doing anything wrong. `.wslconfig` does not exist by default.
Windows does not create it; you create it.

Two further traps make it feel invisible even when you try:

- The leading dot means some older File Explorer views hide it.
- If "Hide extensions for known file types" is on (the default), the
  Notepad "Save as" dialog will silently write `.wslconfig.txt` and you
  will not see the `.txt` in Explorer. WSL will then ignore the file.

### Three ways to create it (pick one)

**Option 1 - PowerShell (recommended, cannot mess up the extension):**

Open PowerShell and run:

```powershell
notepad "$env:USERPROFILE\.wslconfig"
```

Notepad will say "Cannot find the file. Do you want to create a new file?"
Click Yes, paste the config below, File -> Save.

**Option 2 - File Explorer:**

1. In the address bar, type `%USERPROFILE%` and press Enter. This jumps
   to `C:\Users\<you>\`.
2. View -> Show -> File name extensions (turn it on).
3. Right-click -> New -> Text Document.
4. Rename `New Text Document.txt` to `.wslconfig` (no extension). Confirm
   the "Are you sure you want to change it?" prompt.
5. Open it with Notepad and paste the config below.

**Option 3 - From the VS Code / WSL terminal:**

Replace `<you>` with your Windows username:

```bash
code /mnt/c/Users/<you>/.wslconfig
```

Your WSL home (`~`) is not your Windows home, so `wslpath -w ~` will not
get you there. You need `/mnt/c/Users/<you>/` explicitly.

### The config to paste

```ini
[wsl2]
memory=10GB
swap=8GB
processors=16

[experimental]
autoMemoryReclaim=gradual
sparseVhd=true
```

### What each line does

- `memory=12GB` - ceiling on Linux VM RAM. Default is about 50% of host
  RAM. Size this to your host:
  - Host has 32 GB or more: `memory=12GB` is comfortable.
  - Host has 16 GB: use `memory=10GB`.
  - Host has 8 GB: use `memory=6GB` and drop `NUM_CPUS` to 2.
- `swap=8GB` - WSL2 swap file size. Absorbs brief spikes past `memory`
  without killing the build.
- `processors=16` - vCPU count exposed to the WSL2 VM. Cap at your host
  thread count. This is the VM-level ceiling; the repo's `NUM_CPUS` picks
  how many of those the build container actually uses.
- `autoMemoryReclaim=gradual` - returns Linux page cache to Windows over
  time. This is the line that fixes the "vmmem is stuck at 7 GB forever
  after my build finished" feeling.
- `sparseVhd=true` - lets the WSL ext4 VHD shrink on disk as you delete
  Docker images, volumes, and ELK indices. Without this, freeing space
  inside Linux does not free it for Windows.

### Apply the change

In Windows PowerShell (not inside WSL):

```powershell
wsl --shutdown
```

Wait 10 seconds, then reopen VS Code. The WSL2 VM boots with the new
limits. You do not need to reboot Windows.

## Part C: verify it worked

Inside WSL:

```bash
free -h
```

The `Mem:` total should now reflect your new ceiling (for `memory=12GB`
you should see about 12 GiB, not your whole host).

On Windows:

- `wsl --status` in PowerShell confirms the default distro and WSL version.
- Task Manager -> Details -> `Vmmem` (or `vmmemWSL` on newer Windows)
  shows the VM's current RAM usage. After a build, watch it gradually
  come back down thanks to `autoMemoryReclaim=gradual`.

## Part D: how the two knobs work together

- `.wslconfig` sets the host-level ceiling for the entire WSL2 VM: every
  container, every process, every editor running inside WSL shares it.
- `NUM_CPUS` sets the per-build CPU budget inside that ceiling.

Example tuning for a 32 GB, 16-thread Windows host:

- `.wslconfig`: `memory=12GB`, `swap=8GB`, `processors=16`
- Build invocation: `cd nos3 && NUM_CPUS=6 make fsw`

That leaves roughly 4 vCPUs and plenty of RAM for VS Code, the ELK stack,
and any running simulators while the FSW builds.

## Troubleshooting

- **File ended up as `.wslconfig.txt`.** Turn on "File name extensions"
  in Explorer, then rename. Or in Notepad, File -> Save As, set "Save as
  type" to "All Files", and use the literal name `.wslconfig`.
- **`wsl --shutdown` complains or nothing changes.** VS Code (or any
  terminal attached to WSL) holds the distro alive. Close all VS Code
  windows and WSL terminals, then retry.
- **`memory=` is ignored.** Your distro is on WSL1. Check with
  `wsl -l -v` in PowerShell. Convert with
  `wsl --set-version <distro> 2`.
- **`free -h` still shows the old ceiling.** The shutdown did not take.
  Confirm no WSL processes are running (`wsl --list --running`) and
  shutdown again.
- **Disk on C: did not shrink after `docker system prune`.** That is
  expected without `sparseVhd=true`. Add it, `wsl --shutdown`, then
  prune again.

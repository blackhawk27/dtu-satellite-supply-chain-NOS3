# Debugging notes: known errors and fixes

This folder is the engineering record for non-obvious failures hit while
bringing this DTU fork up on the VS Code devcontainer, and the fixes that
keep it running. Several of these fixes are load-bearing: reverting them
reintroduces a startup hang, a crash, or a dark telemetry pipeline. Where a
fix is load-bearing it is called out explicitly, with the condition that
reintroduces the failure.

Each entry follows the same shape: Symptom, Root cause, Fix, and (where
relevant) Verification. Commit hashes are given where the fix has landed.

## Index

- [fsw-build-and-toolchain.md](fsw-build-and-toolchain.md) - build-time rules
  (clean build is 30-40 min), parallelism and OOM, `make config` ordering,
  build output locations, ctest, and historical compile-error fixes.
- [cfs-app-startup-and-tables.md](cfs-app-startup-and-tables.md) - cFS app
  load order, schedule/definition table validation, the 64-bit CS pointer
  truncation crash, and the NOS-time bogus-tick startup stall.
- [sim-ipc-and-42-connectivity.md](sim-ipc-and-42-connectivity.md) - 42
  inter-process-comm (`Inp_IPC.txt`) ports that must stay OFF, prefix width,
  the simulator data-point parsers, and fortytwo socket connectivity.
- [telemetry-and-elk-pipeline.md](telemetry-and-elk-pipeline.md) - getting
  telemetry from the sims into Elasticsearch: log capture, MID dictionaries,
  dashboards, the decoded-downlink seed, and index lifecycle on `make stop`.
- [gui-x11-forwarding.md](gui-x11-forwarding.md) - X11 in the devcontainer,
  the `ci_launch.sh` DISPLAY guard, headless mode, and the cosmos-gui
  Launcher EPERM crash.
- [hardware-sim-telemetry.md](hardware-sim-telemetry.md) - component FSW
  fixes that affect telemetry fidelity (EPS I2C return handling, FSS/MAG
  command pipe limits).
- [attack-poc-integration.md](attack-poc-integration.md) - integrating the
  supply-chain proof-of-concept across the two testbeds (this fork vs Draco),
  including the cross-app symbol-resolution technique on Linux/glibc.

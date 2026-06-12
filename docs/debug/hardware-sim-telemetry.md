# Hardware component FSW fixes affecting telemetry

These are component flight-software fixes that affect telemetry fidelity
rather than startup. Both were found while validating Kibana dashboards.

## EPS: I2C transaction return value was silently discarded

- Symptom: EPS housekeeping reports zero or garbage values when the
  NOS-Engine I2C transaction fails or times out, but the FSW returns
  `OS_SUCCESS` unless the CRC of the (possibly garbage) read buffer happens
  to mismatch. EVS event 31 (`Request device HK reported error -1`) fires
  only intermittently rather than on every failed transaction.
- Root cause: in
  `nos3/components/generic_eps/fsw/shared/generic_eps_device.c`,
  `GENERIC_EPS_RequestHK` and `GENERIC_EPS_CommandDevice` called
  `i2c_master_transaction(...)` with the return value discarded. CSS does it
  correctly at `generic_css_device.c:22-24`, which was used as the template.
- Fix: capture the return value, fail fast on `!= OS_SUCCESS`, and keep the
  CRC check on the success path. `RequestHK` now returns `OS_ERROR` if I2C
  fails before CRC, and `CommandDevice` returns `OS_ERROR` if I2C fails; the
  already-correct CRC-mismatch path is retained.
- Verification: EPS HK fields (`eps_battery_mv` and the rest) hold non-zero
  values during steady state in Kibana, and EVS event 31 from EPS reflects
  only real transaction failures.

## FSS and MAG: default command-pipe MsgLim of 4 is too low

- Symptom: `CFE_SB 17: Msg Limit Err, MsgId 0x1921, pipe FSS_CMD_PIPE` and
  `MsgId 0x192b, pipe MAG_CMD_PIPE` fire every few seconds during runs that
  exercise the uplink path. Surplus commands are dropped silently; only
  event 17 surfaces it.
- Root cause: both apps subscribed via `CFE_SB_Subscribe` (no explicit
  `MsgLim`), which yields `CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT = 4` per MID.
  SCH publishes each REQ_HK at 1 Hz, but CI_LAB-forwarded uplink can burst
  command MIDs faster than the app's main loop drains them; even a 5-command
  burst overflows.
- Fix: switch both apps to `CFE_SB_SubscribeEx` on their CMD and REQ_HK MIDs,
  with `CFE_SB_DEFAULT_QOS` and `MsgLim = <APP>_PIPE_DEPTH` (32). This
  mirrors the existing in-repo pattern in
  `nos3/components/mgr/fsw/cfs/src/mgr_app.c:125-143`, the one DTU app that
  already uses the Ex variant and does not overflow.
- Verification: a search for `evs_event_id: 17 AND evs_message: ("0x1921" OR
  "0x192b")` returns zero hits after a 5-minute soak that includes uplink
  bursts.

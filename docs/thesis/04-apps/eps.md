# 04. generic_eps - Electrical Power Subsystem

This document is the worked example for every other app deep-dive
in this directory. It applies the per-app template named in the
plan: real-world hardware, cFS app overview, simulator half,
function-level walkthrough of the headline computation, ELK
extraction, attack-surface relevance, divergence vs upstream,
and verification.

The headline function is `update_battery_values` on the simulator
side, the function that computes the variable surfaced as
`eps_power_used_w` in Kibana. The user pointed at it explicitly
because it is the thread that connects orbital geometry, an
in-process power-load model, the live SB message-rate signal, and
the operator's dashboard.

## 1. Real-world hardware

The Electrical Power Subsystem on a small satellite collects
solar input from body-mounted panels, charges a battery,
regulates two or three secondary voltage rails (3.3 V, 5.0 V,
12.0 V), and switches dedicated load lines on or off. The EO1
mission's reference design sources its panel power numbers from
GTOSat (a CubeSat-class heritage point) and adapts them to a
~26 W full-sun budget.

Mission-side concerns the simulator models:

- **Cosine effect**: the per-face panel output scales with the
  cosine of the angle to the sun.
- **Eclipse**: when the spacecraft is in shadow, panel input
  drops to zero.
- **Charging hysteresis**: the battery should stop charging when
  full (to avoid overvoltage) and resume below a configurable
  threshold so SOC oscillation across an orbit stays visible
  on the dashboard.
- **Switched loads**: each cFS app that draws power maps to one
  or more load lines whose current draw depends on the app's
  active mode.

## 2. App overview (cFS side)

The flight-software half lives at
`nos3/components/generic_eps/fsw/cfs/src/`:

- `generic_eps_app.c`: app entry point and main loop.
- `generic_eps_app.h`, `generic_eps_msg.h`: header, message
  layouts.
- `generic_eps_events.h`: event ID enumeration.
- `generic_eps_version.h`: version constants.

Key MIDs (from `generic_eps_msg.h` via the component-base macros
described in
[`../03-communication/02-fsw-software-bus.md`](../03-communication/02-fsw-software-bus.md)):

- Command: `0x191A` (topic `0x1A`).
- Housekeeping telemetry: `0x091A`.

Pipes:

- A command pipe subscribed to `GENERIC_EPS_CMD_MID`.
- A wakeup subscription to the SCH 1 Hz tick.

On wakeup, the app reads the I2C bus to query the simulator,
unpacks the simulator's response into the HK packet, and
publishes the HK packet on `0x091A`. The HK aggregator and
Software-Bus capture both consume that publication.

## 3. Simulator half

The simulator lives at
`nos3/components/generic_eps/sim/`:

- `inc/generic_eps_hardware_model.hpp`: class declaration
  (174 lines as of the slice 1 baseline).
- `src/generic_eps_hardware_model.cpp`: implementation
  (994 lines).

The class `Generic_epsHardwareModel` inherits from the shared
`SimIHardwareModel` base and is registered with NOS Engine on
startup. Its private state captures the testbed's power model:

- 8 switch lines (`_switch[8]`), each an `EPS_Rail` struct with
  voltage / current / status / temperature / battery-watthr
  fields.
- 5 buses (`_bus[5]`), indexed 0=battery, 1=3.3 V, 2=5.0 V,
  3=12 V, 4=solar array.
- A per-app load model (`_app_loads`) consisting of `AppLoad`
  records. Each record names an app, holds its MID set, the
  per-mode wattage table, and a rolling deque of recent SB
  message timestamps used for rate inference.
- Charging hysteresis controls: `_max_battery`,
  `_charge_resume_frac`, `_charge_stop_frac`,
  `_solar_array_inhibit`.

Two operations are scheduled by the simulator main loop:

- The NOS Engine time-tick callback bound at line 242 of the
  cpp:
  `_time_bus->add_time_tick_callback(std::bind(&Generic_epsHardwareModel::update_battery_values, this));`
  This is the per-tick power balance; it is the headline
  function (section 5).
- A background thread `god_view_follower_loop` started at line
  234 that tails `attack_logs/cfs_god_view.json`, parses each
  new line, and pushes the timestamp into the matching
  `AppLoad` deque. This is what gives the per-app rate inference
  its raw signal.

## 4. The simulator-side ground-truth read

Every per-tick computation begins by pulling the latest sim
data point through the data provider:

```
auto data_point = ... _generic_eps_dp->get_data_point();
```

The provider in the EO1 mission is the EPS-specific data
provider (registered in the simulator XML); it carries:

- `get_in_sun()`: integer 0 / 1 from 42's eclipse model.
- `get_sun_vector_x() / y() / z()`: the body-frame sun vector
  components.

The function then projects the sun vector onto each face. The
positive components feed the +X / +Y / -Z faces; the negated
negative components feed -X / -Y. Z+ has no panel in the EO1
spacecraft body, by design.

## 5. Headline walkthrough: `update_battery_values`

`update_battery_values` is defined at
`nos3/components/generic_eps/sim/src/generic_eps_hardware_model.cpp:646`.
Walk it line by line.

> **Post-thesis update (2026-05-03):** commit `1b441712 fix(eps/sim): add battery hysteresis band and tune capacity for visible eclipse drop` added a 90 - 100 % SOC hysteresis band so the eclipse-edge SOC oscillation that produced flickering on the dashboard's Battery SOC vs Eclipse panel is suppressed. Battery capacity was also re-tuned so the eclipse drop is visibly distinguishable from sensor noise on the same panel. Outside the hysteresis band, mode-shutoff timing is unchanged from the thesis baseline.

### 5.1 Sun-vector projection (lines 646-657)

```
boost::shared_ptr<Generic_epsDataPoint> data_point = ...;
int8_t in_sun = (data_point->get_in_sun()) ? 1 : 0;
double svb_X      = max(get_sun_vector_x(),   0) * in_sun;
double svb_minusX = max(-get_sun_vector_x(),  0) * in_sun;
double svb_Y      = max(get_sun_vector_y(),   0) * in_sun;
double svb_minusY = max(-get_sun_vector_y(),  0) * in_sun;
double svb_minusZ = max(-get_sun_vector_z(),  0) * in_sun;
```

Each `svb_*` carries the cosine of the angle between the panel
normal and the sun vector, clamped to zero if the angle is
obtuse, and nulled by `in_sun` during eclipse. The simulator
deliberately omits a +Z panel; the EO1 spacecraft mounts no
panel on the +Z face.

### 5.2 Per-app draw: `compute_p_out_from_apps` (line 670)

```
double p_out = compute_p_out_from_apps();
```

This is the testbed's mode-aware load model. The function lives
at line 839 and does two things in one pass over `_app_loads`:

1. For each loaded app, pop expired timestamps from the rolling
   deque (the cutoff is `now - _activity_window_s`). The
   remaining deque size, divided by `_activity_window_s`,
   gives an instantaneous SB message rate in Hz for that app.
2. Walk the threshold ladder via `mode_for_rate` (line 827).
   The ladder is configured in JSON; the first rung whose
   `max_rate_hz` exceeds the observed rate wins, otherwise the
   final has_max=false rung is the fallthrough mode. The
   selected mode names a per-app wattage in `mode_watts`; that
   watt figure goes into `p`.
3. Multiply the sum by a global TM-rate multiplier
   (`_tm_rate_mult`); this lets a global "high downlink rate"
   knob scale every app's draw at once.

If the load model is empty (no JSON), `p_out` is zero and the
sim falls back to a constant draw. If the god-view follower has
not yet attached or the rolling window has not warmed up, every
app uses its `default_mode`. The warm-up gate prevents a fresh
sim start from briefly reporting all apps as `LOW_RATE` mode
during the first few seconds.

The variable name on the wire (the Logstash field that ends up
in Kibana) is `eps_power_used_w`. It is the value of `p_out`
emitted in the EVS log line at line 724:
`sim_logger->debug("Total power used is %f", p_out);`.

### 5.3 Charging hysteresis (lines 676-683)

```
if (_battery_watthrs >= _charge_stop_frac * _max_battery)
    _solar_array_inhibit = 0;       // disconnect panels
if (_battery_watthrs <= _charge_resume_frac * _max_battery)
    _solar_array_inhibit = 1;       // reconnect panels
```

Two thresholds, separated by a hysteresis band. Default values
in the EO1 spacecraft config make the resume threshold 90 % of
maximum, which keeps the SOC visibly oscillating as the
spacecraft transitions between sun and eclipse. A narrower band
clamps SOC to ~100 % and removes the orbital-period signature
from the dashboard.

### 5.4 Solar input: `p_in` (line 685)

```
double p_in = (
      (_power_per_main_panel  * svb_X)      * _posX_Panel_Inhibit
    + (_power_per_main_panel  * svb_minusX) * _negX_Panel_Inhibit
    + (_power_per_main_panel  * svb_Y)      * _posY_Panel_Inhibit
    + (_power_per_main_panel  * svb_minusY) * _negY_Panel_Inhibit
    + (_power_per_small_panel * svb_minusZ) * _negZ_Panel_Inhibit
) * _solar_array_inhibit;
```

Each inhibit is a 0 / 1 byte set by ground command; an operator
can disable a face. The sum is then gated by the array inhibit
from section 5.3. The `_power_per_main_panel` constant defaults
to 4.485 W, sourced from the GTOSat reference (line 93 of the
cpp documents the source).

### 5.5 Battery-charge integration (lines 687-695)

```
double delta_p =
    (_sim_microseconds_per_tick / 1e6) * ((p_in - p_out) + _charge_rate_modifer);
_battery_watthrs += delta_p / 3600;       // s -> hours
clamp(_battery_watthrs, 0, _max_battery);
```

`delta_p` is energy in watt-seconds across one tick; the divide
by 3600 converts to watt-hours. The `_charge_rate_modifer`
field is a ground-commandable bias that lets the operator
inject a delta into the integration without changing the
physics; this is used in attack scenarios that simulate a
saboteur's override.

The hard clamp at the upper bound prevents drift above
`_max_battery` (the inhibit guards in section 5.3 only fire
after the next sample crosses the threshold; without the clamp,
SOC tends to read 100.001 % after a long sun arc).

### 5.6 Bus-voltage scaling (lines 701-715)

Battery voltage scales linearly with SOC across +/- 5 % of
nominal:

```
double batt_min_voltage = 0.95 * _nominal_batt_voltage;
double batt_diff        = 0.10 * _nominal_batt_voltage;
double soc_frac         = _battery_watthrs / _max_battery;
_bus[0]._voltage        = 1000 * (batt_min_voltage + batt_diff * soc_frac);
```

The 3.3 V, 5.0 V, and 12.0 V rails ride live, scaled +/- 2 %
by SOC fraction:

```
_bus[1]._voltage = 1000 * (3.3 * (0.98 + 0.04 * soc_frac));
_bus[2]._voltage = 1000 * (5.0 * (0.98 + 0.04 * soc_frac));
_bus[3]._voltage = 1000 * (12.0 * (0.98 + 0.04 * soc_frac));
```

This is the change that took the previously dead `BusXVoltage`
fields in `GENERIC_EPS_Device_HK_tlm_t` and gave them real
motion. Before this change those fields were zero-initialised
and never touched.

### 5.7 EVS debug emission (lines 720-737)

The function ends by writing per-metric debug lines and a
single composite tick-summary line:

```
sim_logger->debug(
  "EPS_TLM solar=%f used=%f balance=%f wh=%f mv=%i soc=%f sun=%d",
  p_in, p_out, power_balance, _battery_watthrs,
  _bus[0]._voltage, soc_pct, _solar_array_inhibit);
```

The composite line is the Logstash hook. The per-metric lines
are kept for human-readable log inspection.

## 6. ELK extraction

The Logstash filter at `nos3/elk/logstash.conf` extracts the
following numeric fields from the EVS lines above:

- `eps_solar_power_w` from `solar=`.
- `eps_power_used_w` from `used=` (this is the headline
  field).
- `eps_battery_wh` from `wh=`.
- `eps_battery_mv` from `mv=`.
- `eps_solar_array_enabled` from `sun=`.

The composite line approach means every joint aggregation
(power balance, SOC vs panel input, etc.) hits a single
document in Elasticsearch.

The Kibana panels that consume these fields:

- "EPS Power Balance" on `nos3-std-telemetry-overview`.
- "Battery SOC vs Eclipse" on `nos3-std-telemetry-overview`.
- "EPS HK rate vs spoofed HK rate" on
  `nos3-std-mission-validation` (this is the panel that visualises
  the noisy_app attack against EPS).

## 7. Attack-surface relevance

The EPS app sits at three attack-relevant positions in the
trust-boundary overlay (figure F8):

- **Component supply (FSW <-> sim bus)**: a tampered
  `generic_eps_sim` binary can publish forged battery state.
  The FSW would have no way to detect the forgery on the I2C
  bus alone; the cross-check is the orbital-geometry signature
  (eclipse should depress SOC) on the dashboard.
- **Component supply (Software Bus injection)**: `noisy_app`
  publishes spoofed `0x091A` packets directly on the SB.
  Because the SB does not authenticate publishers, the HK
  aggregator picks up both the legitimate EPS packet and the
  spoofed one. The "EPS HK rate vs spoofed HK rate" panel is
  the detection surface.
- **Operations layer**: a tampered Logstash filter can drop
  the EPS composite line so the dashboard reads zero even
  while the sim publishes correctly. The drop-rule catalogue
  at `docs/noise-filters.md` lists the legitimate suppressions
  and is the baseline for divergence.

The thesis chapter on supply-chain attack chains explicitly
exercises each of these three surfaces against EPS.

## 8. DTU divergence vs upstream

Relative to the import baseline at commit `55ad2148`:

- The mode-based per-app load model is a DTU addition. It
  introduces the `AppLoad` struct, the threshold ladder, the
  `god_view_follower_loop` background thread, and the JSON
  load-model loader. The reference for the design of this is
  `debug/EPS_DESIGN.md`.
- The hysteresis band (`_charge_resume_frac`,
  `_charge_stop_frac`) replaces an upstream all-or-nothing
  guard. The wider band is what makes orbital SOC visible on
  the dashboard.
- The bus-voltage scaling at lines 713-715 is a DTU addition;
  upstream's `BusXVoltage` fields were zero-init.
- The composite EVS tick-summary line at lines 733-736 is a DTU
  addition driven by the ELK ingest contract.
- The hard clamp at lines 694-695 was added because the inhibit
  guards alone are not sufficient for a steady-state 100 %
  reading.

The corresponding cFS app (`generic_eps_app.c`) was not
substantially changed from upstream other than to publish the
new HK fields.

## 9. Verification

Once the testbed is launched (`make launch && make start-elk`):

- **Live HK tracking**: the COSMOS UI should show the
  GENERIC_EPS HK packet updating at 1 Hz. The
  `Battery_Wh_Remaining`, `Bus0Voltage` (battery mV),
  and `BusXVoltage` (3.3, 5.0, 12.0 mV) fields all change
  visibly on a sun arc.
- **Logstash extraction**: in Kibana, query
  `event.dataset:"system_log" AND eps_power_used_w:*` against
  `nos3-telemetry-*`. Documents return at 1 Hz with the field
  populated.
- **Eclipse signature**: panel "Battery SOC vs Eclipse" on
  `nos3-std-telemetry-overview` shows SOC oscillating across
  the 90-100 % band as the orbit rotates.
- **noisy_app attack signal**: while the noisy_app attack runs
  (see `nos3/scripts/attack/attack_noisy_trigger.py`), the
  spoofed-HK-rate panel on `nos3-std-mission-validation` shows
  the second `0x091A` source.

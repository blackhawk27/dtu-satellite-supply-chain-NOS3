# Boundary 2: the cFS Software Bus

The Software Bus is the in-process publish-subscribe message bus
that every cFS app speaks. It is the central trust boundary in this
testbed: the supply-chain threat model assumes the attacker has a
cFS app loaded and therefore has full access to it. Understanding
how the Software Bus works (and what it does not check) is what
makes the threat model concrete.

## Wire-level model

Despite the name, the Software Bus does not have a wire. It is
shared memory inside the cFS process, mediated by the `cfe_sb`
service. A publisher calls `CFE_SB_TransmitMsg` with a buffer; the
service looks up which subscribers have registered for the buffer's
message identifier (MID), copies the buffer into each subscriber's
input pipe, and returns. Subscribers later call `CFE_SB_ReceiveBuffer`
on their pipe to read.

Messages are CCSDS Space Packet Protocol primary header plus
payload. The two fields that matter at this boundary are:

- **The message identifier (MID).** A 16-bit value (in this fork's
  cFE layout) that is the *only* thing the Software Bus uses to
  route a packet. Commands occupy the `0x1XXX` range; telemetry
  occupies `0x0XXX`. The per-component allocations are summarised
  in [../09-glossary.md](../09-glossary.md) and exhaustively
  enumerated by the MID-registry YAMLs under `cfg/build/elk/`.
- **The function code.** A 7-bit value inside the CCSDS secondary
  header on command packets that selects which command code the
  receiver should execute. The function code only applies to
  command MIDs; telemetry MIDs ignore it.

Subscribers register interest with `CFE_SB_Subscribe(MID, PipeId)`.
The bus maintains a routing table from MID to list-of-pipes; one
MID can fan out to many subscribers. The same MID can have many
publishers; the bus does not record which task originated a given
message. Figure F3 in
[../08-figures/figures.md](../08-figures/figures.md) draws the
topology: many publishers, one routing table keyed on MID, many
subscribers per MID, no provenance arrow anywhere on the diagram.

## What the bus does not check

The bus performs no provenance, identity, or capability check on a
publisher. The full enumeration:

- **No identity binding on publish.** Any task that holds a valid
  `CFE_SB_Buffer` can publish under any MID. There is no per-task
  ACL, no MID-to-publisher map. `noisy_app` publishes EPS-range
  MIDs purely by writing them into the header.
- **No identity binding on subscribe.** Likewise, any task can
  subscribe to any MID. `noisy_app` can subscribe to the GSW
  command stream and harvest it.
- **No signature, no MAC, no sequence-counter validation.** The
  CCSDS primary header has a per-MID sequence counter that the bus
  increments, but it does not verify, and a forged publisher can
  write whatever sequence value it likes.
- **No rate limiting.** A publisher can issue messages as fast as
  it can call the API. The pipe-overflow event documented in
  [04-apps/noisy_app.md](04-apps/noisy_app.md) is the result of
  exactly this: `noisy_app` fills a subscriber's pipe faster than
  the subscriber drains it, the bus emits the `SB_PIPE_OVERFLOW`
  event, and the `noisy_app_spam_target` tag is what
  `nos3/elk/logstash.conf` lines around 155 and 347 use to surface
  the symptom in Kibana.

This is not a flaw in cFS. The bus is designed on the assumption
that everything inside the cFS process is part of the spacecraft
software and is co-trusted. The supply-chain threat model attacks
exactly that assumption.

## Routing and visibility

Two routing facts shape the supply-chain experiments:

- **The bus is the boundary, not the radio.** `to_lab` is just one
  more subscriber. The MIDs that leave the spacecraft are exactly
  the MIDs `nos3/cfg/nos3_defs/tables/to_lab_sub.c` enumerates
  (one `CFE_SB_Subscribe` call per MID). Anything published by an attacker on a MID `to_lab` does
  not subscribe to is invisible to the ground; anything published
  on a MID `to_lab` does subscribe to looks identical to the
  legitimate publisher's traffic. The DTU `god_view_capture.py`
  observer exists to break this asymmetry from the *researcher's*
  side, not the operator's.
- **EVS is the only side channel.** When a cFS app calls
  `CFE_EVS_SendEvent`, the event service publishes the event on
  the bus and `to_lab` ships it to ground. `cfs_evs_capture.sh`
  also tails the EVS stream into `omni_logs/cfs_evs.log`. EVS
  events therefore appear twice in Kibana: once as a
  `software_bus` document (the bus message) and once as a
  `system_log` document (the captured log line). This duplication
  is intentional; it gives the analyst two independent views of the
  same event for forensic cross-checking.

## How `cfs_god_view.json` is produced

The `god_view_capture.py` script binds to a UDP socket and
subscribes to the same TO_LAB telemetry stream that COSMOS does,
but it logs *every* MID rather than only the curated set the
operator cares about. The set of MIDs it sees is therefore the set
of MIDs in `to_lab_sub.c`. This is important to record because:

- The "god view" is god-of-TO_LAB, not god-of-the-bus. A cFS app
  that publishes a MID not in `to_lab_sub.c` is invisible to
  `god_view_capture.py`. This is why
  [04-apps/blackboard.md](../04-apps/blackboard.md) records the
  exact subscription list as a load-bearing detail of this fork:
  if the list shrinks, attack-signature coverage in Kibana shrinks
  with it.
- A widening of `to_lab_sub.c` is therefore an observability
  change, not just a build change. The customisation list in
  [../07-customizations-vs-upstream.md](../07-customizations-vs-upstream.md)
  enumerates the divergences in that file relative to the import
  baseline.

## Bus health as telemetry

Three bus-level metrics are surfaced through Logstash to Kibana:

- The `sb_pipe_overflow` tag (set when EVS emits
  `SB_PIPE_OVERFLOW`) marks each instance of a subscriber's pipe
  filling up. Under the supply-chain threat model this is a strong
  indicator that a malicious publisher is spamming a high-rate
  MID at a subscriber that cannot keep up.
- The `sequence` field on every `software_bus` document is the
  CCSDS sequence counter at the time the message was captured.
  Gaps in this counter for a given MID are observable in Kibana
  by sorting on `msg_id` and watching `sequence`; a malicious
  publisher that injects a single forged packet under an
  otherwise-legitimate MID produces a single gap.
- The `function_code` field, captured for command MIDs, makes it
  possible to ask Kibana "which command codes were issued under
  this MID, and when". The `noisy_app` attack-trigger tagging
  in `logstash.conf` (lines 160-170) is built on this.

## Deviation from upstream

The Software Bus implementation in `cfe_sb` is unchanged from
upstream cFE. What this fork changes is the observability of the
bus: the introduction of `god_view_capture.py`, the EVS tail, and
the Logstash tagging that surfaces overflow, sequence, and function
code as searchable Kibana fields. These changes do not alter the
trust boundary; they make the boundary inspectable after the fact.
The trust model itself is intentionally preserved so that attack
experiments are realistic against a deployed cFS.

# sbn (Software Bus Network)

- **Source:** `nos3/fsw/apps/sbn/`
- **Loaded by:** `cfe_es_startup.scr` line 25 (priority 63, stack 32768)
- **Pipe depth:** `SBN_PEER_PIPE_DEPTH = 32`, `SBN_SUB_PIPE_DEPTH = 32`
- **MIDs:** uses peer-table-driven dynamic MIDs; HK MIDs per peer
- **Tables:** peer table (which remote nodes), subscription table (which MIDs forward)

## Role

Bridges multiple cFS instances into a single logical SB. In this build, only `cpu1` is loaded, so SBN is essentially idle; it remains in the startup script for multi-cpu mission profiles.

## Silent-failure modes

- A peer that drops out is reported in HK but the local SB sees no failure.
- Subscription-table mismatch between peers silently drops bridged MIDs.

## Attack-surface notes

- Idle in this build, but if SBN is enabled across multiple containers, the peer connection is an unauthenticated TCP socket by default; pin to localhost or VPN.
- Subscription-table tampering on either end is enough to bridge an attacker MID into the operational stream.

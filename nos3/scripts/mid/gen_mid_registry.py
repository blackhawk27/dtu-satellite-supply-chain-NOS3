#!/usr/bin/env python3
"""
gen_mid_registry.py - Build a single source of truth for CCSDS Message IDs.

Walks the FSW source headers (*_topicids.h, *_msgids.h, *_msgid_values.h),
resolves every <NAME>_MID symbol to its literal 16-bit value the same way
the C preprocessor does, then emits:

  cfg/build/mid_registry.json
      One JSON object per MID - {hex_str: {name, layer, subsystem}}.
      Consumed by scripts/god_view_capture.py.

  cfg/build/elk/mid_to_name.yml
  cfg/build/elk/mid_to_layer.yml
  cfg/build/elk/mid_to_subsystem.yml
      Flat hex -> string maps for the Logstash `translate` filter
      (dictionary_path option). Mounted into the Logstash container.

Run from `make config`. Re-running is idempotent.

Layer / subsystem assignment is driven by scripts/mid/mid_groupings.yaml
(name-prefix -> {layer, subsystem}); MIDs with no matching prefix get
{layer: UNKNOWN, subsystem: UNKNOWN}.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
NOS3 = REPO_ROOT / "nos3"
OUT_REGISTRY = NOS3 / "cfg" / "build" / "mid_registry.json"
OUT_ELK_DIR = NOS3 / "cfg" / "build" / "elk"
GROUPINGS_YAML = Path(__file__).with_name("mid_groupings.yaml")

# Skip these path fragments - duplicates / wrong toolchain / generated copies.
SKIP_FRAGMENTS = ("/build/", "/fprime/")

# Header-bearing directories. Glob patterns relative to NOS3 - we walk only
# these subtrees to avoid traversing huge unrelated areas like gsw/yamcs.
HEADER_GLOBS = (
    "components/*/fsw/cfs/platform_inc",
    "fsw/apps/*/config",
    "fsw/apps/*/fsw/inc",
    "fsw/apps/*/fsw/platform_inc",
    "fsw/cfe/modules/*/config",
    "fsw/cfe/modules/*/fsw/inc",
)


def header_files(suffix: str) -> list[Path]:
    """Gather every *_<suffix> file across the known header directories."""
    out: list[Path] = []
    for pattern in HEADER_GLOBS:
        for d in NOS3.glob(pattern):
            if not d.is_dir() or not included_path(d):
                continue
            out.extend(p for p in d.glob(f"*_{suffix}") if included_path(p))
    return out

TOPIC_RE = re.compile(
    r"^\s*#define\s+(\w+_TOPICID)\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)", re.M
)
DEFAULT_TOPIC_RE = re.compile(
    r"^\s*#define\s+DEFAULT_(\w+_TOPICID)\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)", re.M
)
LITERAL_MID_RE = re.compile(
    r"^\s*#define\s+(\w+_MID(?:_\w+)*)\s+\(?\s*(0x[0-9A-Fa-f]+)\b", re.M
)
DIRECT_MIDV_RE = re.compile(
    r"^\s*#define\s+(\w+_MID(?:_\w+)*)\s+CFE_PLATFORM_(COMPONENT_)?(CMD|TLM)"
    r"_TOPICID_TO_MIDV\(\s*(\w+)\s*\)",
    re.M,
)
# Two wrapper-use styles co-exist in the codebase:
#   heritage:  CFE_PLATFORM_<APP>_<DIR>_MIDVAL(<SUB>)   (ES, SC, HS, HK, CS, DS, MM, MD, ...)
#   alt:       <APP>_<DIR>_PLATFORM_MIDVAL(<SUB>)       (LC, FM)
MIDVAL_USE_HERITAGE_RE = re.compile(
    r"^\s*#define\s+(\w+_MID(?:_\w+)*)\s+CFE_PLATFORM_(\w+?)_(CMD|TLM)"
    r"_MIDVAL\(\s*(\w+)\s*\)",
    re.M,
)
MIDVAL_USE_ALT_RE = re.compile(
    r"^\s*#define\s+(\w+_MID(?:_\w+)*)\s+(\w+?)_(CMD|TLM)_PLATFORM_MIDVAL"
    r"\(\s*(\w+)\s*\)",
    re.M,
)
MIDVAL_DEF_HERITAGE_RE = re.compile(
    r"^\s*#define\s+CFE_PLATFORM_(\w+?)_(CMD|TLM)_MIDVAL\(\w+\)"
    r"\s+CFE_PLATFORM_(CMD|TLM)_TOPICID_TO_MIDV"
    r"\(\s*(CFE_MISSION_\w+?)_##\w+##_TOPICID\s*\)",
    re.M,
)
MIDVAL_DEF_ALT_RE = re.compile(
    r"^\s*#define\s+(\w+?)_(CMD|TLM)_PLATFORM_MIDVAL\(\w+\)"
    r"\s+CFE_PLATFORM_(CMD|TLM)_TOPICID_TO_MIDV"
    r"\(\s*(\w+?)_##\w+##_TOPICID\s*\)",
    re.M,
)
# MD (and potentially others) use a literal-table style instead of topic
# lookup: each SUB has its own `CFE_PLATFORM_<APP>_<DIR>_MIDVAL_<SUB>` macro
# defined as a raw hex value. The wrapper `CFE_PLATFORM_<APP>_<DIR>_MIDVAL(x)`
# is `CFE_PLATFORM_<APP>_<DIR>_MIDVAL_##x` - a token-paste, not a topic lookup.
LITERAL_MIDVAL_RE = re.compile(
    r"^\s*#define\s+CFE_PLATFORM_(\w+?)_(CMD|TLM)_MIDVAL_(\w+)"
    r"\s+\(?\s*(0x[0-9A-Fa-f]+)\b",
    re.M,
)
ALIAS_MID_RE = re.compile(
    r"^\s*#define\s+(\w+_MID(?:_\w+)*)\s+(\w+_MID(?:_\w+)*)\s*$", re.M
)
BACKSLASH_CONT_RE = re.compile(r"\\\n\s*")
BASE_MASK_RE = re.compile(
    r"#define\s+CFE_PLATFORM_(COMPONENT_)?(CMD|TLM)_TOPICID_TO_MIDV"
    r"\(\w+\)\s+\(\s*(0x[0-9A-Fa-f]+)",
    re.M,
)
# This cFE baseline defines core/heritage MIDs with the classic additive form
# instead of the Draco-era TOPICID_TO_MIDV masks, e.g.
#   #define CFE_PLATFORM_TLM_MID_BASE 0x0800
#   #define CFE_MISSION_ES_HK_TLM_MSG 0
#   #define CFE_ES_HK_TLM_MID  CFE_PLATFORM_TLM_MID_BASE + CFE_MISSION_ES_HK_TLM_MSG
ADDITIVE_BASE_RE = re.compile(
    r"^\s*#define\s+(CFE_PLATFORM_(?:CMD|TLM)_MID_BASE(?:_GLOB)?)"
    r"\s+\(?\s*(0x[0-9A-Fa-f]+)\b",
    re.M,
)
MISSION_MSG_RE = re.compile(
    r"^\s*#define\s+(CFE_MISSION_\w+_MSG)\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)\b",
    re.M,
)
ADDITIVE_MID_RE = re.compile(
    r"^\s*#define\s+(\w+_MID(?:_\w+)*)\s+"
    r"(CFE_PLATFORM_(?:CMD|TLM)_MID_BASE(?:_GLOB)?)\s*\+\s*(CFE_MISSION_\w+_MSG)\b",
    re.M,
)


def included_path(p: Path) -> bool:
    s = str(p)
    return not any(frag in s for frag in SKIP_FRAGMENTS)


def read_text(p: Path) -> str:
    try:
        raw = p.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    # Collapse C line continuations so multi-line #defines match single-line REs.
    return BACKSLASH_CONT_RE.sub(" ", raw)


def collect_bases() -> dict[tuple[str, str], int]:
    """Read the four base masks from the mapping headers."""
    bases: dict[tuple[str, str], int] = {}
    for h in (
        NOS3 / "cfg" / "nos3_defs" / "cfe_msgid_api.h",
        NOS3 / "cfg" / "nos3_defs" / "nos3_component_msgid_mapping.h",
    ):
        for m in BASE_MASK_RE.finditer(read_text(h)):
            scope = "COMPONENT" if m.group(1) else "HERITAGE"
            bases[(scope, m.group(2))] = int(m.group(3), 16)
    # The Draco-era CFE_PLATFORM_*_TOPICID_TO_MIDV base masks are absent on
    # this cFE baseline: every MID is defined as a literal hex value (or a
    # token-paste MIDVAL), so resolve_mids() resolves them without the bases.
    # Warn if the expected four are not all present, but do not fail - the
    # literal path still produces the full registry.
    if len(bases) != 4:
        print(
            f"  note: {len(bases)}/4 TOPICID_TO_MIDV base masks found; "
            f"resolving MIDs from literal definitions.",
            file=sys.stderr,
        )
    return bases


def collect_additive() -> tuple[dict[str, int], dict[str, int]]:
    """Gather the additive MID bases (CFE_PLATFORM_*_MID_BASE) and the
    CFE_MISSION_*_MSG offsets used by the classic 'BASE + offset' MID defines
    on this cFE baseline."""
    add_bases: dict[str, int] = {}
    offsets: dict[str, int] = {}
    for h in (header_files("msgids.h")
              + header_files("topicids.h")
              + header_files("msgid_values.h")):
        text = read_text(h)
        for m in ADDITIVE_BASE_RE.finditer(text):
            add_bases.setdefault(m.group(1), int(m.group(2), 16))
        for m in MISSION_MSG_RE.finditer(text):
            offsets.setdefault(m.group(1), int(m.group(2), 0))
    return add_bases, offsets


def collect_topics() -> dict[str, int]:
    """Map every topic-ID symbol to its numeric value."""
    topics: dict[str, int] = {}
    for h in header_files("topicids.h"):
        text = read_text(h)
        for m in TOPIC_RE.finditer(text):
            topics[m.group(1)] = int(m.group(2), 0)
        for m in DEFAULT_TOPIC_RE.finditer(text):
            topics.setdefault(m.group(1), int(m.group(2), 0))
    return topics


def collect_midval_wrappers() -> tuple[
    dict[tuple[str, str, str], tuple[str, str]],
    dict[tuple[str, str, str], int],
]:
    """
    cFS apps wrap topic lookup via per-app MIDVAL macros.

    Three naming styles live side-by-side in the tree:

        heritage:  CFE_PLATFORM_SC_CMD_MIDVAL(x)
                   -> CFE_PLATFORM_CMD_TOPICID_TO_MIDV(CFE_MISSION_SC_##x##_TOPICID)

        alt:       LC_CMD_PLATFORM_MIDVAL(x)
                   -> CFE_PLATFORM_CMD_TOPICID_TO_MIDV(LC_MISSION_##x##_TOPICID)

        literal:   CFE_PLATFORM_MD_CMD_MIDVAL_CMD  0x1890
                   CFE_PLATFORM_MD_CMD_MIDVAL(x)  CFE_PLATFORM_MD_CMD_MIDVAL_##x

    Returns:
        topic_wrappers: {(style, APP, DIR): (base_dir, topic_prefix)} for the
            topic-lookup styles (heritage / alt).
        literal_table:  {(APP, DIR, SUB): hex_value} for the token-paste style.
    """
    topic_wrappers: dict[tuple[str, str, str], tuple[str, str]] = {}
    literal_table: dict[tuple[str, str, str], int] = {}
    for h in header_files("msgid_values.h") + header_files("msgids.h"):
        text = read_text(h)
        for m in MIDVAL_DEF_HERITAGE_RE.finditer(text):
            topic_wrappers[("heritage", m.group(1), m.group(2))] = (
                m.group(3),
                m.group(4),
            )
        for m in MIDVAL_DEF_ALT_RE.finditer(text):
            topic_wrappers[("alt", m.group(1), m.group(2))] = (
                m.group(3),
                m.group(4),
            )
        for m in LITERAL_MIDVAL_RE.finditer(text):
            literal_table[(m.group(1), m.group(2), m.group(3))] = int(
                m.group(4), 16
            )
    return topic_wrappers, literal_table


def resolve_mids(
    topics: dict[str, int],
    bases: dict[tuple[str, str], int],
    topic_wrappers: dict[tuple[str, str, str], tuple[str, str]],
    literal_table: dict[tuple[str, str, str], int],
    add_bases: dict[str, int],
    offsets: dict[str, int],
) -> dict[str, int]:
    """Resolve every <NAME>_MID symbol to its literal 16-bit value."""
    mids: dict[str, int] = {}
    aliases: dict[str, str] = {}

    for h in header_files("msgids.h") + header_files("msgid_values.h"):
        text = read_text(h)

        for m in LITERAL_MID_RE.finditer(text):
            name = m.group(1)
            # Skip internal MIDVAL table constants (see LITERAL_MIDVAL_RE).
            # They end with _MID_<SUFFIX> but are never referenced as MIDs.
            if "_MIDVAL_" in name or name.startswith("CFE_PLATFORM_"):
                continue
            mids.setdefault(name, int(m.group(2), 16))

        # Classic additive form: <MID> = CFE_PLATFORM_*_MID_BASE + CFE_MISSION_*_MSG
        for m in ADDITIVE_MID_RE.finditer(text):
            base = add_bases.get(m.group(2))
            off = offsets.get(m.group(3))
            if base is not None and off is not None:
                mids.setdefault(m.group(1), base + off)

        for m in DIRECT_MIDV_RE.finditer(text):
            scope = "COMPONENT" if m.group(2) else "HERITAGE"
            topic_val = topics.get(m.group(4))
            base = bases.get((scope, m.group(3)))
            if topic_val is not None and base is not None:
                mids.setdefault(m.group(1), base | topic_val)

        for use_re, style in ((MIDVAL_USE_HERITAGE_RE, "heritage"),
                              (MIDVAL_USE_ALT_RE, "alt")):
            for m in use_re.finditer(text):
                app, direction, sub = m.group(2), m.group(3), m.group(4)
                wrap = topic_wrappers.get((style, app, direction))
                if wrap is not None:
                    base_dir, topic_prefix = wrap
                    topic_val = topics.get(f"{topic_prefix}_{sub}_TOPICID")
                    base = bases.get(("HERITAGE", base_dir))
                    if topic_val is not None and base is not None:
                        mids.setdefault(m.group(1), base | topic_val)
                        continue
                literal = literal_table.get((app, direction, sub))
                if literal is not None:
                    mids.setdefault(m.group(1), literal)

        for m in ALIAS_MID_RE.finditer(text):
            aliases.setdefault(m.group(1), m.group(2))

    # Resolve aliases - at most a few hops in practice.
    for _ in range(4):
        progress = False
        for alias, target in list(aliases.items()):
            if alias in mids:
                continue
            if target in mids:
                mids[alias] = mids[target]
                progress = True
        if not progress:
            break

    return mids


def load_groupings() -> list[dict]:
    """
    Parse a tiny YAML subset (no PyYAML dependency on the host build):
      - prefix: SOMETHING_
        layer: COMPONENT
        subsystem: EPS
    """
    if not GROUPINGS_YAML.exists():
        return []
    rules: list[dict] = []
    cur: dict = {}
    for raw in GROUPINGS_YAML.read_text().splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        if line.lstrip().startswith("- "):
            if cur:
                rules.append(cur)
            cur = {}
            line = line.lstrip()[2:]
        if ":" in line:
            k, _, v = line.lstrip().partition(":")
            cur[k.strip()] = v.strip().strip('"').strip("'")
    if cur:
        rules.append(cur)
    return rules


def classify(name: str, rules: list[dict]) -> tuple[str, str]:
    """Apply the longest matching prefix rule."""
    best: dict | None = None
    for rule in rules:
        prefix = rule.get("prefix", "")
        if not prefix or not name.startswith(prefix):
            continue
        if best is None or len(prefix) > len(best["prefix"]):
            best = rule
    if best is None:
        return ("UNKNOWN", "UNKNOWN")
    return (best.get("layer", "UNKNOWN"), best.get("subsystem", "UNKNOWN"))


def write_yaml_dict(path: Path, data: dict[str, str]) -> None:
    """
    Logstash's translate filter reads YAML dicts as `"key": "value"` lines.
    The file format is intentionally minimal so we don't depend on PyYAML.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as fh:
        fh.write("# Generated by scripts/mid/gen_mid_registry.py - do not edit.\n")
        for k in sorted(data):
            v = data[k].replace('"', '\\"')
            fh.write(f'"{k}": "{v}"\n')


def main() -> int:
    bases = collect_bases()
    topics = collect_topics()
    add_bases, offsets = collect_additive()
    topic_wrappers, literal_table = collect_midval_wrappers()
    mids = resolve_mids(topics, bases, topic_wrappers, literal_table,
                        add_bases, offsets)
    rules = load_groupings()

    by_hex: dict[str, dict[str, str]] = {}
    for name, value in sorted(mids.items()):
        layer, subsystem = classify(name, rules)
        hex_key = f"0x{value:04X}"
        existing = by_hex.get(hex_key)
        if existing is None or _name_score(name) > _name_score(existing["name"]):
            by_hex[hex_key] = {
                "name": name,
                "layer": layer,
                "subsystem": subsystem,
            }

    OUT_REGISTRY.parent.mkdir(parents=True, exist_ok=True)
    OUT_REGISTRY.write_text(
        json.dumps(by_hex, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    write_yaml_dict(OUT_ELK_DIR / "mid_to_name.yml",
                    {k: v["name"] for k, v in by_hex.items()})
    write_yaml_dict(OUT_ELK_DIR / "mid_to_layer.yml",
                    {k: v["layer"] for k, v in by_hex.items()})
    write_yaml_dict(OUT_ELK_DIR / "mid_to_subsystem.yml",
                    {k: v["subsystem"] for k, v in by_hex.items()})

    print(
        f"  generated: cfg/build/mid_registry.json  "
        f"({len(by_hex)} MIDs, {len(topics)} topics)"
    )
    return 0


def _name_score(name: str) -> int:
    """
    Two C symbols can resolve to the same MID (e.g. SAMPLE_APP_CMD_MID is an
    alias of SAMPLE_CMD_MID). Prefer the canonical name: the longest one
    that does NOT have an _APP_ infix wins, otherwise pick the longer one.
    """
    return (0 if "_APP_" in name else 1, len(name))


if __name__ == "__main__":
    sys.exit(main())

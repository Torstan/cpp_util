#!/usr/bin/env python3
"""Generate a self-contained HTML report for block tree benchmark rows."""

import argparse
import csv
import datetime as _datetime
import html
import io
import math
import os
import subprocess
import sys


EXPECTED_IMPLEMENTATIONS = ("immutable_tree", "block_tree_2048", "block_tree_4096")
EXPECTED_PATTERNS = ("sorted", "random")
EXPECTED_SIZES = (1, 10, 100, 1000, 10000, 100000)
EXPECTED_KEY_BYTES = (32, 64)
EXPECTED_VALUE_BYTES = (64, 128, 256, 1024)

REQUIRED_CASE_FIELDS = {
    "name",
    "pattern",
    "size",
    "key_bytes",
    "value_bytes",
    "height",
    "build_us",
    "hit_contains_us",
    "miss_contains_us",
    "to_vector_us",
    "allocated_delta",
    "active_delta",
    "resident_delta",
}

INT_FIELDS = {
    "size",
    "key_bytes",
    "value_bytes",
    "height",
    "build_us",
    "hit_contains_us",
    "miss_contains_us",
    "to_vector_us",
    "allocated_delta",
    "active_delta",
    "resident_delta",
}

OPTIONAL_INT_FIELDS = {
    "nodes",
    "zip_lists",
    "entries",
    "entry_capacity",
    "min_block_count",
}

OPTIONAL_FLOAT_FIELDS = {
    "avg_fill",
}

IMPLEMENTATION_ORDER = {
    "immutable_tree": 0,
    "block_tree_2048": 1,
    "block_tree_4096": 2,
}

DEFAULT_OUTPUT = "reports/immutable_tree_vs_block_tree.html"

CHART_METRICS = (
    ("build_us", "Build time", "us"),
    ("hit_contains_us", "Contains hit", "us"),
    ("miss_contains_us", "Contains miss", "us"),
    ("to_vector_us", "ToVector", "us"),
    ("allocated_delta", "Allocated memory", "bytes"),
    ("active_delta", "Active memory", "bytes"),
    ("resident_delta", "Resident memory", "bytes"),
)

SERIES_COLORS = {
    "immutable_tree": "#2563eb",
    "block_tree_2048": "#dc2626",
    "block_tree_4096": "#059669",
}


class ReportError(ValueError):
    pass


def parse_line(line):
    """Parse one benchmark row as (row_type, fields)."""
    stripped = line.strip()
    if not stripped:
        raise ReportError("empty row")

    try:
        tokens = next(csv.reader([stripped]))
    except csv.Error as exc:
        raise ReportError(f"invalid CSV row: {exc}") from exc

    if not tokens or not tokens[0].strip():
        raise ReportError("missing row type")

    row_type = tokens[0].strip()
    fields = {}
    for token in tokens[1:]:
        if "=" not in token:
            raise ReportError(f"field lacks key=value form: {token!r}")
        key, value = token.split("=", 1)
        key = key.strip()
        if not key:
            raise ReportError(f"empty field key in row: {stripped!r}")
        fields[key] = value.strip()
    return row_type, fields


def _coerce_case(row, source):
    missing = sorted(REQUIRED_CASE_FIELDS - set(row))
    if missing:
        raise ReportError(f"{source}: case row missing fields: {', '.join(missing)}")

    coerced = dict(row)
    for field in INT_FIELDS:
        try:
            coerced[field] = int(coerced[field], 10)
        except ValueError as exc:
            raise ReportError(f"{source}: field {field} must be an integer") from exc
    for field in ("allocated_delta", "active_delta", "resident_delta"):
        if coerced[field] < 0:
            raise ReportError(f"{source}: field {field} must be non-negative")
    for field in OPTIONAL_INT_FIELDS:
        if field not in coerced or coerced[field] == "":
            continue
        try:
            coerced[field] = int(coerced[field], 10)
        except ValueError as exc:
            raise ReportError(f"{source}: field {field} must be an integer") from exc
    for field in OPTIONAL_FLOAT_FIELDS:
        if field not in coerced or coerced[field] == "":
            continue
        try:
            coerced[field] = float(coerced[field])
        except ValueError as exc:
            raise ReportError(f"{source}: field {field} must be a float") from exc
    return coerced


def expected_case_keys():
    return {
        (name, pattern, size, key_bytes, value_bytes)
        for name in EXPECTED_IMPLEMENTATIONS
        for pattern in EXPECTED_PATTERNS
        for size in EXPECTED_SIZES
        for key_bytes in EXPECTED_KEY_BYTES
        for value_bytes in EXPECTED_VALUE_BYTES
    }


def validate_complete_matrix(cases, source):
    expected = expected_case_keys()
    seen = {}
    for row in cases:
        key = (
            row["name"],
            row["pattern"],
            row["size"],
            row["key_bytes"],
            row["value_bytes"],
        )
        seen[key] = seen.get(key, 0) + 1

    duplicates = sorted(key for key, count in seen.items() if count > 1)
    if duplicates:
        raise ReportError(f"{source}: duplicate case rows found, first duplicate: {duplicates[0]}")

    actual = set(seen)
    missing = sorted(expected - actual)
    if missing:
        raise ReportError(
            f"{source}: incomplete benchmark matrix, missing {len(missing)} case rows; "
            f"first missing: {missing[0]}"
        )

    extra = sorted(actual - expected)
    if extra:
        raise ReportError(
            f"{source}: unexpected benchmark matrix rows: {len(extra)}; first extra: {extra[0]}"
        )


def _read_rows_from_iter(lines, source, require_complete_matrix=True):
    env = {}
    cases = []
    for line_no, raw_line in enumerate(lines, 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        row_type, fields = parse_line(line)
        where = f"{source}:{line_no}"
        if row_type == "env":
            env.update(fields)
        elif row_type == "case":
            cases.append(_coerce_case(fields, where))
        else:
            raise ReportError(f"{where}: unsupported row type {row_type!r}")

    if not cases:
        raise ReportError(f"{source}: no case rows found")
    if require_complete_matrix:
        validate_complete_matrix(cases, source)
    return env, cases


def read_rows(path):
    """Read env and case rows from a file path or '-' for stdin."""
    if path == "-":
        return _read_rows_from_iter(sys.stdin, "<stdin>")
    with open(path, "r", encoding="utf-8", newline="") as handle:
        return _read_rows_from_iter(handle, path)


def fmt_int(value):
    return f"{int(value):,}"


def fmt_us(value):
    value = int(value)
    if abs(value) >= 1_000_000:
        return f"{value / 1_000_000:.2f} s"
    if abs(value) >= 1_000:
        return f"{value / 1_000:.2f} ms"
    return f"{value:,} us"


def fmt_bytes(value):
    value = int(value)
    sign = "-" if value < 0 else ""
    amount = abs(value)
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    unit_index = 0
    scaled = float(amount)
    while scaled >= 1024.0 and unit_index < len(units) - 1:
        scaled /= 1024.0
        unit_index += 1
    if unit_index == 0:
        return f"{sign}{amount:,} B"
    return f"{sign}{scaled:.2f} {units[unit_index]}"


def fmt_bytes_per_entry(bytes_value, entries):
    entries = int(entries)
    if entries == 0:
        return "n/a"
    return f"{int(bytes_value) / entries:.2f} B/entry"


def fmt_float(value, digits=2):
    return f"{float(value):.{digits}f}"


def fmt_metric_value(field, value):
    if field.endswith("_us"):
        return fmt_us(value)
    if field.endswith("_delta"):
        return fmt_bytes(value)
    return fmt_int(value)


def sort_key(row):
    return (
        row["pattern"],
        row["key_bytes"],
        row["value_bytes"],
        row["size"],
        IMPLEMENTATION_ORDER.get(row["name"], 99),
        row["name"],
    )


def group_cases(cases):
    groups = {}
    for row in sorted(cases, key=sort_key):
        key = (row["pattern"], row["key_bytes"], row["value_bytes"])
        groups.setdefault(key, []).append(row)
    return groups


def _ratio(candidate, baseline, field):
    base = baseline[field]
    if base <= 0 or candidate[field] < 0:
        return None
    return candidate[field] / base


def _ratio_text(candidate, baseline, field, lower_is_better=True):
    ratio = _ratio(candidate, baseline, field)
    if ratio is None:
        return "n/a"
    percent = (ratio - 1.0) * 100.0
    if abs(percent) < 0.05:
        return "same"
    if lower_is_better:
        word = "faster" if percent < 0 else "slower"
    else:
        word = "smaller" if percent < 0 else "larger"
    return f"{abs(percent):.1f}% {word}"


def _ratio_value(candidate, baseline, field):
    ratio = _ratio(candidate, baseline, field)
    if ratio is None:
        return "not comparable"
    return f"{ratio:.2f}x"


def build_observations(cases):
    observations = []
    for (pattern, key_bytes, value_bytes), rows in group_cases(cases).items():
        largest_size = max(row["size"] for row in rows)
        rows_at_size = {
            row["name"]: row
            for row in rows
            if row["size"] == largest_size
        }
        baseline = rows_at_size.get("immutable_tree")
        if not baseline:
            continue
        for name in ("block_tree_2048", "block_tree_4096"):
            candidate = rows_at_size.get(name)
            if not candidate:
                continue
            observations.append(
                {
                    "pattern": pattern,
                    "key_bytes": key_bytes,
                    "value_bytes": value_bytes,
                    "size": largest_size,
                    "name": name,
                    "build": _ratio_text(candidate, baseline, "build_us"),
                    "hit": _ratio_text(candidate, baseline, "hit_contains_us"),
                    "miss": _ratio_text(candidate, baseline, "miss_contains_us"),
                    "vector": _ratio_text(candidate, baseline, "to_vector_us"),
                    "allocated": _ratio_text(
                        candidate, baseline, "allocated_delta", lower_is_better=False
                    ),
                }
            )
    return observations


def summary_rows(cases):
    rows = []
    index = {
        (row["pattern"], row["key_bytes"], row["value_bytes"], row["size"], row["name"]): row
        for row in cases
    }
    for pattern in EXPECTED_PATTERNS:
        for key_bytes in EXPECTED_KEY_BYTES:
            for value_bytes in EXPECTED_VALUE_BYTES:
                size = max(EXPECTED_SIZES)
                baseline = index.get((pattern, key_bytes, value_bytes, size, "immutable_tree"))
                if baseline is None:
                    continue
                for name in ("block_tree_2048", "block_tree_4096"):
                    row = index.get((pattern, key_bytes, value_bytes, size, name))
                    if row is None:
                        continue
                    rows.append(
                        [
                            pattern,
                            _n(fmt_int(key_bytes)),
                            _n(fmt_int(value_bytes)),
                            name,
                            _n(fmt_int(size)),
                            _n(_ratio_value(row, baseline, "build_us")),
                            _n(_ratio_value(row, baseline, "hit_contains_us")),
                            _n(_ratio_value(row, baseline, "miss_contains_us")),
                            _n(_ratio_value(row, baseline, "to_vector_us")),
                            _n(_ratio_value(row, baseline, "allocated_delta")),
                        ]
                    )
    return rows


def _html_escape(value):
    return html.escape(str(value), quote=True)


def _td(value, cls=None):
    attr = f' class="{cls}"' if cls else ""
    return f"<td{attr}>{_html_escape(value)}</td>"


def _th(value):
    return f"<th>{_html_escape(value)}</th>"


def _table(headers, rows):
    header_html = "".join(_th(header) for header in headers)
    body = []
    for row in rows:
        body.append("<tr>" + "".join(_td(value, "num" if isinstance(value, Numeric) else None) for value in row) + "</tr>")
    return (
        "<table>\n<thead><tr>"
        + header_html
        + "</tr></thead>\n<tbody>\n"
        + "\n".join(body)
        + "\n</tbody>\n</table>"
    )


class Numeric(str):
    pass


def _n(value):
    return Numeric(value)


def _optional_int(row, field):
    value = row.get(field)
    if value == "" or value is None:
        return "not applicable"
    return _n(fmt_int(value))


def _optional_float(row, field):
    value = row.get(field)
    if value == "" or value is None:
        return "not applicable"
    return _n(fmt_float(value))


def performance_rows(cases):
    rows = []
    for row in sorted(cases, key=sort_key):
        rows.append(
            [
                row["name"],
                _n(fmt_int(row["key_bytes"])),
                _n(fmt_int(row["value_bytes"])),
                _n(fmt_int(row["size"])),
                _n(fmt_us(row["build_us"])),
                _n(fmt_us(row["hit_contains_us"])),
                _n(fmt_us(row["miss_contains_us"])),
                _n(fmt_us(row["to_vector_us"])),
            ]
        )
    return rows


def memory_rows(cases):
    rows = []
    for row in sorted(cases, key=sort_key):
        rows.append(
            [
                row["name"],
                _n(fmt_int(row["key_bytes"])),
                _n(fmt_int(row["value_bytes"])),
                _n(fmt_int(row["size"])),
                _n(fmt_bytes(row["allocated_delta"])),
                _n(fmt_bytes_per_entry(row["allocated_delta"], row["size"])),
                _n(fmt_bytes(row["active_delta"])),
                _n(fmt_bytes(row["resident_delta"])),
            ]
        )
    return rows


def structure_rows(cases):
    rows = []
    for row in sorted(cases, key=sort_key):
        rows.append(
            [
                row["name"],
                _n(fmt_int(row["key_bytes"])),
                _n(fmt_int(row["value_bytes"])),
                _n(fmt_int(row["size"])),
                row["pattern"],
                _n(fmt_int(row["height"])),
                _optional_int(row, "nodes"),
                _optional_int(row, "zip_lists"),
                _optional_int(row, "entries"),
                _optional_int(row, "entry_capacity"),
                _optional_float(row, "avg_fill"),
                _optional_int(row, "min_block_count"),
            ]
        )
    return rows


def _series_for_group(rows, field):
    by_name = {name: [] for name in EXPECTED_IMPLEMENTATIONS}
    for row in rows:
        by_name[row["name"]].append((row["size"], row[field]))
    for values in by_name.values():
        values.sort()
    return by_name


def _chart_svg(rows, field, title, unit):
    series = _series_for_group(rows, field)
    values = [value for points in series.values() for _, value in points]
    if not values:
        return ""

    width = 620
    height = 300
    left = 64
    right = 24
    top = 34
    bottom = 56
    min_x = math.log10(min(EXPECTED_SIZES))
    max_x = math.log10(max(EXPECTED_SIZES))
    max_y = max(values)
    if max_y <= 0:
        max_y = 1

    def x_pos(size):
        if max_x == min_x:
            return left
        return left + (math.log10(size) - min_x) * (width - left - right) / (max_x - min_x)

    def y_pos(value):
        return top + (max_y - value) * (height - top - bottom) / max_y

    parts = [
        f'<svg class="chart-svg" viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="{_html_escape(title)}">',
        f'<text x="{left}" y="20" class="chart-title">{_html_escape(title)}</text>',
        f'<line x1="{left}" y1="{height - bottom}" x2="{width - right}" '
        f'y2="{height - bottom}" class="axis"/>',
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height - bottom}" class="axis"/>',
    ]

    for size in EXPECTED_SIZES:
        x = x_pos(size)
        parts.append(
            f'<line x1="{x:.1f}" y1="{height - bottom}" x2="{x:.1f}" '
            f'y2="{height - bottom + 4}" class="tick"/>'
        )
        parts.append(
            f'<text x="{x:.1f}" y="{height - bottom + 18}" class="axis-label" '
            f'text-anchor="middle">{fmt_int(size)}</text>'
        )

    for fraction in (0.0, 0.25, 0.5, 0.75, 1.0):
        value = max_y * fraction
        y = y_pos(value)
        parts.append(
            f'<line x1="{left}" y1="{y:.1f}" x2="{width - right}" y2="{y:.1f}" '
            f'class="grid"/>'
        )
        parts.append(
            f'<text x="{left - 8}" y="{y + 4:.1f}" class="axis-label" '
            f'text-anchor="end">{_html_escape(fmt_metric_value(field, int(value)))}</text>'
        )

    legend_x = left
    for name in EXPECTED_IMPLEMENTATIONS:
        color = SERIES_COLORS[name]
        points = series[name]
        if points:
            polyline = " ".join(f"{x_pos(size):.1f},{y_pos(value):.1f}" for size, value in points)
            parts.append(
                f'<polyline points="{polyline}" fill="none" stroke="{color}" '
                f'stroke-width="2.5"/>'
            )
            for size, value in points:
                parts.append(
                    f'<circle cx="{x_pos(size):.1f}" cy="{y_pos(value):.1f}" r="3" '
                    f'fill="{color}"><title>{_html_escape(name)} {fmt_int(size)}: '
                    f'{_html_escape(fmt_metric_value(field, value))}</title></circle>'
                )
        parts.append(
            f'<rect x="{legend_x}" y="{height - 24}" width="10" height="10" fill="{color}"/>'
        )
        parts.append(
            f'<text x="{legend_x + 14}" y="{height - 15}" class="legend">'
            f'{_html_escape(name)}</text>'
        )
        legend_x += 150

    parts.append(f'<text x="{width - right}" y="{height - 34}" class="axis-label" text-anchor="end">entries, log scale</text>')
    parts.append(f'<text x="{left}" y="{top - 8}" class="axis-label">{_html_escape(unit)}</text>')
    parts.append("</svg>")
    return "\n".join(parts)


def _chart_group_html(pattern, key_bytes, value_bytes, rows):
    charts = []
    for field, label, unit in CHART_METRICS:
        charts.append(
            '<div class="chart-card">'
            + _chart_svg(rows, field, label, unit)
            + "</div>"
        )
    heading = (
        f"{pattern} inserts, {fmt_int(key_bytes)}-byte keys, "
        f"{fmt_int(value_bytes)}-byte values"
    )
    return (
        f"<section class=\"chart-group\"><h3>{_html_escape(heading)}</h3>"
        f"<div class=\"chart-grid\">{''.join(charts)}</div></section>"
    )


def charts_html(cases):
    sections = []
    for (pattern, key_bytes, value_bytes), rows in group_cases(cases).items():
        sections.append(_chart_group_html(pattern, key_bytes, value_bytes, rows))
    return "\n".join(sections)


def metadata(env, cases, args, input_label):
    values = {
        "generated": _datetime.datetime.now(_datetime.timezone.utc)
        .astimezone()
        .isoformat(timespec="seconds"),
        "input": input_label,
        "benchmark command": args.bench_command,
        "jemalloc path": args.jemalloc_path,
    }
    for key in ("allocator", "key_type", "value_type"):
        if key in env:
            values[key.replace("_", " ")] = env[key]
    values["key byte lengths"] = ", ".join(fmt_int(v) for v in sorted({r["key_bytes"] for r in cases}))
    values["value byte lengths"] = ", ".join(fmt_int(v) for v in sorted({r["value_bytes"] for r in cases}))
    values["sizes"] = ", ".join(fmt_int(v) for v in sorted({r["size"] for r in cases}))
    values["patterns"] = ", ".join(sorted({r["pattern"] for r in cases}))
    return values


def _metadata_html(items):
    rows = []
    for key, value in items.items():
        rows.append(f"<tr><th>{_html_escape(key.title())}</th><td>{_html_escape(value)}</td></tr>")
    return "<table class=\"metadata\"><tbody>\n" + "\n".join(rows) + "\n</tbody></table>"


def _comparison_phrase(label, comparison):
    if comparison == "same":
        return f"{label} same as immutable_tree"
    if comparison == "n/a":
        return f"{label} not comparable with immutable_tree"
    return f"{label} {comparison} than immutable_tree"


def _observations_html(observations):
    if not observations:
        return "<p>No complete immutable_tree/block_tree comparison groups were found.</p>"
    items = []
    for obs in observations:
        comparisons = [
            _comparison_phrase("build", obs["build"]),
            _comparison_phrase("hit", obs["hit"]),
            _comparison_phrase("miss", obs["miss"]),
            _comparison_phrase("to_vector", obs["vector"]),
            _comparison_phrase("allocated", obs["allocated"]),
        ]
        items.append(
            "<li>"
            + _html_escape(
                f"{obs['pattern']} keys={obs['key_bytes']} values={obs['value_bytes']} "
                f"size={obs['size']}: {obs['name']} {', '.join(comparisons)}."
            )
            + "</li>"
        )
    return "<ul>\n" + "\n".join(items) + "\n</ul>"


def interpretation_html():
    items = [
        "The string workload measures std::string containers, not inline byte arrays. "
        "For the requested 32/64-byte keys and 64/128/256/1024-byte values, string "
        "character buffers are heap allocated and dominate jemalloc allocated memory.",
        "ImmutableBlockTree reduces AVL node count and groups many string objects per "
        "node, but each entry still owns separate key/value character buffers. Memory "
        "savings are therefore most visible when payloads are small, and become small "
        "relative to payload size for 1024-byte values.",
        "Block structure fields are marked not applicable for ImmutableTree because it "
        "does not use zip-list blocks.",
    ]
    return "<ul>\n" + "\n".join(f"<li>{_html_escape(item)}</li>" for item in items) + "\n</ul>"


def render_html(env, cases, args, input_label):
    title = "Immutable Tree vs Block Tree Benchmark Report"
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{_html_escape(title)}</title>
<style>
:root {{
  color-scheme: light;
  --bg: #f8fafc;
  --panel: #ffffff;
  --text: #1f2937;
  --muted: #64748b;
  --line: #cbd5e1;
  --head: #e2e8f0;
  --accent: #0f766e;
}}
* {{ box-sizing: border-box; }}
body {{
  margin: 0;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  background: var(--bg);
  color: var(--text);
  line-height: 1.45;
}}
main {{ max-width: 1280px; margin: 0 auto; padding: 32px 24px 48px; }}
h1 {{ margin: 0 0 8px; font-size: 30px; font-weight: 700; }}
h2 {{ margin: 32px 0 12px; font-size: 20px; }}
p {{ color: var(--muted); margin: 0 0 16px; }}
.panel {{
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 8px;
  padding: 16px;
  overflow-x: auto;
}}
.note {{
  background: #ecfeff;
  border: 1px solid #a5f3fc;
  border-radius: 8px;
  color: #164e63;
  padding: 12px 14px;
}}
.chart-group {{
  margin: 18px 0 30px;
}}
.chart-grid {{
  display: grid;
  gap: 14px;
  grid-template-columns: repeat(auto-fit, minmax(420px, 1fr));
}}
.chart-card {{
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 8px;
  padding: 10px;
  overflow-x: auto;
}}
.chart-svg {{
  display: block;
  height: auto;
  min-width: 560px;
  width: 100%;
}}
.axis, .tick {{ stroke: #64748b; stroke-width: 1; }}
.grid {{ stroke: #e2e8f0; stroke-width: 1; }}
.axis-label, .legend {{ fill: #475569; font-size: 10px; }}
.chart-title {{ fill: #1f2937; font-size: 14px; font-weight: 700; }}
table {{ border-collapse: collapse; width: 100%; min-width: 860px; }}
th, td {{ border-bottom: 1px solid var(--line); padding: 8px 10px; text-align: left; white-space: nowrap; }}
thead th {{ background: var(--head); position: sticky; top: 0; }}
tbody tr:hover {{ background: #f1f5f9; }}
.num {{ text-align: right; font-variant-numeric: tabular-nums; }}
.metadata {{ min-width: 0; }}
.metadata th {{ width: 220px; color: var(--muted); }}
ul {{ margin: 0; padding-left: 22px; }}
li {{ margin: 6px 0; }}
.tag {{ color: var(--accent); font-weight: 600; }}
</style>
</head>
<body>
<main>
<h1>{_html_escape(title)}</h1>
<p>Self-contained report generated from line-oriented benchmark rows.</p>
<p class="note">Memory charts use jemalloc deltas from one fresh benchmark process per case. Block-only structural fields are marked "not applicable" for ImmutableTree because it does not store zip-list blocks.</p>

<h2>Metadata</h2>
<div class="panel">
{_metadata_html(metadata(env, cases, args, input_label))}
</div>

<h2>Observations</h2>
<div class="panel">
{_observations_html(build_observations(cases))}
</div>

<h2>Interpretation Notes</h2>
<div class="panel">
{interpretation_html()}
</div>

<h2>Largest-Size Ratios</h2>
<div class="panel">
{_table(["Pattern", "Key bytes", "Value bytes", "Implementation", "Size", "Build", "Hit contains", "Miss contains", "To vector", "Allocated"], summary_rows(cases))}
</div>

<h2>Charts</h2>
{charts_html(cases)}

<h2>Performance</h2>
<div class="panel">
{_table(["Implementation", "Key bytes", "Value bytes", "Size", "Build", "Hit contains", "Miss contains", "To vector"], performance_rows(cases))}
</div>

<h2>jemalloc Memory</h2>
<div class="panel">
{_table(["Implementation", "Key bytes", "Value bytes", "Size", "Allocated delta", "Allocated / entry", "Active delta", "Resident delta"], memory_rows(cases))}
</div>

<h2>Block Structure</h2>
<div class="panel">
{_table(["Implementation", "Key bytes", "Value bytes", "Size", "Pattern", "Height", "Nodes", "Zip lists", "Entries", "Entry capacity", "Average fill", "Minimum block count"], structure_rows(cases))}
</div>
</main>
</body>
</html>
"""


def _run_benchmark(command):
    output = []
    env = subprocess.run(
        f"{command} --env",
        shell=True,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    output.append(env.stdout)
    for name in EXPECTED_IMPLEMENTATIONS:
        for pattern in EXPECTED_PATTERNS:
            for key_bytes in EXPECTED_KEY_BYTES:
                for value_bytes in EXPECTED_VALUE_BYTES:
                    for size in EXPECTED_SIZES:
                        completed = subprocess.run(
                            f"{command} --name {name} --pattern {pattern} --size {size} "
                            f"--key-bytes {key_bytes} --value-bytes {value_bytes}",
                            shell=True,
                            check=True,
                            text=True,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                        )
                        output.append(completed.stdout)
    return _read_rows_from_iter(io.StringIO("".join(output)), command)


def write_report(output_path, html_text):
    directory = os.path.dirname(output_path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(html_text)


def self_test():
    row_type, fields = parse_line(
        "case,name=immutable_tree,pattern=random,size=1000,key_bytes=32,value_bytes=64,"
        "height=10,build_us=1234,hit_contains_us=56,miss_contains_us=78,"
        "to_vector_us=900,allocated_delta=4096,active_delta=8192,resident_delta=16384"
    )
    assert row_type == "case"
    assert fields["key_bytes"] == "32"
    assert fields["value_bytes"] == "64"
    env, cases = _read_rows_from_iter(
        [
            "env,allocator=jemalloc,key_type=string,value_type=string\n",
            "case,name=immutable_tree,pattern=random,size=1000,key_bytes=32,value_bytes=64,"
            "height=10,build_us=1234,hit_contains_us=56,miss_contains_us=78,"
            "to_vector_us=900,allocated_delta=4096,active_delta=8192,resident_delta=16384\n",
            "case,name=block_tree_2048,pattern=random,size=1000,key_bytes=32,value_bytes=64,"
            "height=5,build_us=1234,hit_contains_us=56,miss_contains_us=78,"
            "to_vector_us=900,allocated_delta=4096,active_delta=8192,resident_delta=16384,"
            "nodes=7,zip_lists=6,entries=1000,entry_capacity=1200,avg_fill=0.8333,"
            "min_block_count=1\n",
        ],
        "<self-test>",
        require_complete_matrix=False,
    )
    assert env["allocator"] == "jemalloc"
    assert cases[0]["key_bytes"] == 32
    assert cases[0]["value_bytes"] == 64
    assert cases[1]["nodes"] == 7
    assert cases[1]["avg_fill"] == 0.8333
    assert fmt_int(1234567) == "1,234,567"
    assert fmt_us(1234) == "1.23 ms"
    assert fmt_bytes(2048) == "2.00 KiB"
    assert fmt_bytes_per_entry(4096, 32) == "128.00 B/entry"
    assert fmt_float(1.23456) == "1.23"
    args = argparse.Namespace(
        bench_command="bench",
        jemalloc_path="jemalloc",
    )
    html_text = render_html(env, cases, args, "<self-test>")
    assert "Zip lists" in html_text
    assert "Entry capacity" in html_text
    assert "Average fill" in html_text
    assert "Minimum block count" in html_text
    assert "same as immutable_tree" in html_text
    assert "same than immutable_tree" not in html_text
    assert "Charts" in html_text
    assert "chart-svg" in html_text
    assert "not applicable" in html_text
    try:
        validate_complete_matrix(cases, "<self-test>")
        raise AssertionError("partial matrix validation unexpectedly passed")
    except ReportError as exc:
        assert "incomplete benchmark matrix" in str(exc)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Generate an HTML report from block tree benchmark rows."
    )
    parser.add_argument("--input", help="benchmark row file to read, or '-' for stdin")
    parser.add_argument("--output", default=DEFAULT_OUTPUT, help=f"HTML output path (default: {DEFAULT_OUTPUT})")
    parser.add_argument(
        "--bench-command",
        default="./build/bench_block_tree_string_report_jemalloc",
        help="benchmark command to run when --input is omitted",
    )
    parser.add_argument(
        "--jemalloc-path",
        default="../thirdparty/jemalloc",
        help="jemalloc path recorded in report metadata",
    )
    parser.add_argument("--self-test", action="store_true", help="run internal checks and exit")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    if args.self_test:
        self_test()
        return 0

    try:
        if args.input:
            env, cases = read_rows(args.input)
            input_label = args.input
        else:
            env, cases = _run_benchmark(args.bench_command)
            input_label = f"command: {args.bench_command}"
        write_report(args.output, render_html(env, cases, args, input_label))
        print(f"wrote {args.output}")
    except subprocess.CalledProcessError as exc:
        stderr = (exc.stderr or "").strip()
        if stderr:
            print(f"error: {exc}\n{stderr}", file=sys.stderr)
        else:
            print(f"error: {exc}", file=sys.stderr)
        return 1
    except (OSError, ReportError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

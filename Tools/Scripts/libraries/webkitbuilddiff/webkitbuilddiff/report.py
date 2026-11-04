# Copyright (C) 2026 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Render a :class:`~webkitbuilddiff.diff.DiffResult` as text, JSON, or HTML.

Convention throughout the report: ``-`` marks something present only in build
**A**, ``+`` marks something present only in build **B**. The two builds are
named by ``DiffResult.a_label`` / ``b_label`` (``Xcode``/``Ninja`` by default,
or whatever the caller passed for a same-system comparison).
"""
from __future__ import annotations

import dataclasses
import html
import json
from enum import Enum
from pathlib import Path
from typing import Any, Optional, cast

from webkitbuilddiff.argnorm import Summary
from webkitbuilddiff.diff import (
    ArgCategoryDiff, CodesignDiff, DiffResult, LanguageCompilerDiff, SymbolDiff,
)

# Cap how many individual items are listed per section unless verbose.
_PREVIEW_LIMIT = 20

# Cap for lists embedded in the HTML report unless verbose (kept higher than the
# text preview since the page is scrollable and filterable).
_HTML_ITEM_CAP = 200


def _preview(items: list[str], verbose: bool, sign: str) -> list[str]:
    ordered = sorted(items)
    shown = ordered if verbose else ordered[:_PREVIEW_LIMIT]
    lines = [f'      {sign} {item}' for item in shown]
    if not verbose and len(ordered) > _PREVIEW_LIMIT:
        lines.append(f'      ... {len(ordered) - _PREVIEW_LIMIT} more '
                     f'(use -v to list all)')
    return lines


def _category_lines(categories: list[ArgCategoryDiff], verbose: bool,
                    indent: str = '    ') -> list[str]:
    lines: list[str] = []
    for category in categories:
        lines.append(f'{indent}{category.category}:')
        lines += _preview(list(category.only_a), verbose, '-')
        lines += _preview(list(category.only_b), verbose, '+')
    return lines


def _summary_fields(a: Optional[Summary], b: Optional[Summary]
                    ) -> list[tuple[str, object, object, bool]]:
    """Zip two summaries into ``(field, a_value, b_value, differs)``."""
    ad = a.as_dict() if a else {}
    bd = b.as_dict() if b else {}
    keys = list(ad) if ad else list(bd)
    rows: list[tuple[str, object, object, bool]] = []
    for key in keys:
        av = _fmt_summary(ad.get(key, '-'))
        bv = _fmt_summary(bd.get(key, '-'))
        rows.append((key, av, bv, av != bv))
    return rows


def _fmt_summary(value: object) -> object:
    if isinstance(value, list):
        return ','.join(str(v) for v in value) or '-'
    return value


def _summary_lines(a: Optional[Summary], b: Optional[Summary],
                   a_label: str, b_label: str,
                   indent: str = '    ') -> list[str]:
    rows = _summary_fields(a, b)
    if not rows:
        return []
    lines = [f'{indent}{"":26} {a_label:>14} {b_label:>14}']
    for field_name, av, bv, differs in rows:
        marker = ' *' if differs else ''
        lines.append(f'{indent}{field_name:26} {str(av):>14} {str(bv):>14}'
                     f'{marker}')
    return lines


def _symbol_line(symbol: SymbolDiff) -> str:
    if symbol.error:
        return f'  [{symbol.identity}] error: {symbol.error}'
    return (f'  [{symbol.identity}] a={symbol.a_count} '
            f'b={symbol.b_count} common={symbol.common_count} | '
            f'-{len(symbol.only_a)} +{len(symbol.only_b)}')


def _codesign_lines(cs: CodesignDiff, verbose: bool) -> list[str]:
    if cs.error:
        return [f'  [{cs.identity}] error: {cs.error}']
    lines = [f'  [{cs.identity}]']
    a, b = cs.a, cs.b
    assert a and b
    if cs.identifier_differs():
        lines.append(f'      identifier: - {a.identifier}  + {b.identifier}')
    if cs.authority_differs():
        lines.append(f'      authority:  - {a.authority or "ad-hoc"}  '
                     f'+ {b.authority or "ad-hoc"}')
    if cs.flags_differ():
        lines.append(f'      flags:      - {a.flags}  + {b.flags}')
    if cs.entitlements_only_a:
        lines += _preview(list(cs.entitlements_only_a), verbose,
                          '- entitlement')
    if cs.entitlements_only_b:
        lines += _preview(list(cs.entitlements_only_b), verbose,
                          '+ entitlement')
    if cs.entitlement_value_differs:
        for key in sorted(cs.entitlement_value_differs):
            lines.append(f'      ~ entitlement {key}: '
                         f'- {a.entitlements[key]!r}  + {b.entitlements[key]!r}')
    return lines if len(lines) > 1 else []


def _root_line(label: str, sign: str, root: str) -> str:
    return f'  {label} ({sign}) [{Path(root).name}]: {root}'


def render_text(result: DiffResult, *, verbose: bool = False) -> str:
    a_label, b_label = result.a_label, result.b_label
    lines: list[str] = []
    lines.append(f'Comparing {a_label} vs {b_label} builds')
    lines.append(_root_line(a_label, '-', result.a_root))
    lines.append(_root_line(b_label, '+', result.b_root))
    lines.append(f'  arch:      {result.arch}')
    lines.append('')

    if result.binaries is not None:
        b = result.binaries
        lines.append(f'== Binaries ==  '
                     f'-{len(b.only_a)} only {a_label}, '
                     f'+{len(b.only_b)} only {b_label}, '
                     f'{len(b.common)} common, '
                     f'{len(b.kind_mismatch)} kind mismatch')
        for product in b.only_a:
            lines.append(f'    - {product.identity} ({product.kind}) '
                         f'[{product.target_name}]')
        for product in b.only_b:
            lines.append(f'    + {product.identity} ({product.kind}) '
                         f'[{product.target_name}]')
        for ap, bp in b.kind_mismatch:
            lines.append(f'    ~ {ap.identity}: - {ap.kind}  + {bp.kind}')
        if verbose:
            for identity in b.common:
                lines.append(f'      = {identity}')
        lines.append('')

    if result.compiler is not None:
        lines.append('== Compiler flags ==')
        for lang in result.compiler.languages():
            header = f'  [{lang.language}]'
            lines.append(header)
            if lang.note:
                lines.append(f'    note: {lang.note}')
            lines += _summary_lines(lang.a_summary, lang.b_summary,
                                    a_label, b_label)
            if lang.global_categories:
                lines.append('    global flag differences:')
                lines += _category_lines(lang.global_categories, verbose,
                                         indent='      ')
            differing_targets = [t for t in lang.per_target if t.any()]
            if differing_targets:
                lines.append(f'    per-target differences '
                             f'({len(differing_targets)} targets):')
                for target in differing_targets:
                    lines.append(f'      [{target.name}]')
                    sa, sb = target.a_summary, target.b_summary
                    if sa and sb and sa.tu_count != sb.tu_count:
                        lines.append(f'        tu_count: - {sa.tu_count}  '
                                     f'+ {sb.tu_count}')
                    lines += _category_lines(target.categories, verbose,
                                             indent='        ')
        lines.append('')

    if result.linkers or 'args' in result.dimensions:
        lines.append(f'== Linker flags ==  {len(result.linkers)} products differ')
        for linker in result.linkers:
            lines.append(f'  [{linker.identity}]')
            lines += _category_lines(linker.categories, verbose)
        lines.append('')

    if result.symbols:
        differing_syms = [s for s in result.symbols
                          if s.error or s.only_a or s.only_b]
        lines.append(f'== Exported symbols ==  '
                     f'{len(differing_syms)} products differ')
        for symbol in sorted(result.symbols, key=lambda s: s.identity):
            if not (symbol.error or symbol.only_a or symbol.only_b):
                continue
            lines.append(_symbol_line(symbol))
            if verbose:
                lines += _preview(list(symbol.only_a), verbose, '-')
                lines += _preview(list(symbol.only_b), verbose, '+')
        lines.append('')

    if result.codesign:
        differing_cs = [c for c in result.codesign if c.any()]
        lines.append(f'== Codesigning ==  {len(differing_cs)} products differ')
        for cs in sorted(result.codesign, key=lambda c: c.identity):
            lines += _codesign_lines(cs, verbose)
        lines.append('')

    return '\n'.join(lines).rstrip() + '\n'


def _jsonable(obj: Any) -> Any:
    if dataclasses.is_dataclass(obj) and not isinstance(obj, type):
        return {f.name: _jsonable(getattr(obj, f.name))
                for f in dataclasses.fields(obj)}
    if isinstance(obj, dict):
        return {str(k): _jsonable(v) for k, v in obj.items()}
    if isinstance(obj, (set, frozenset)):
        return sorted(_jsonable(x) for x in obj)
    if isinstance(obj, (list, tuple)):
        return [_jsonable(x) for x in obj]
    if isinstance(obj, Path):
        return str(obj)
    if isinstance(obj, Enum):
        return obj.value
    return obj


def to_json_dict(result: DiffResult) -> dict[str, Any]:
    return cast('dict[str, Any]', _jsonable(result))


def render_json(result: DiffResult) -> str:
    return json.dumps(to_json_dict(result), indent=2, sort_keys=True)


# --- HTML report -----------------------------------------------------------

_HTML_CSS = """\
:root {
  --bg: #ffffff; --fg: #1d1d1f; --muted: #6e6e73; --border: #d2d2d7;
  --panel: #f5f5f7; --x: #b3261e; --xbg: #fbeae9; --n: #1b6e2e;
  --nbg: #e7f4ea; --m: #7a4ad1; --accent: #0071e3;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #1c1c1e; --fg: #f5f5f7; --muted: #98989d; --border: #38383a;
    --panel: #2c2c2e; --x: #ff6b60; --xbg: #3a2422; --n: #5bd977;
    --nbg: #1f3324; --m: #b98cff; --accent: #4aa3ff;
  }
}
* { box-sizing: border-box; }
body {
  margin: 0; padding: 0 0 4rem; background: var(--bg); color: var(--fg);
  font: 14px/1.5 -apple-system, BlinkMacSystemFont, "SF Pro Text", Helvetica,
        Arial, sans-serif;
}
header {
  padding: 1.5rem 2rem 1rem; border-bottom: 1px solid var(--border);
}
h1 { font-size: 1.4rem; margin: 0 0 .5rem; }
.roots { color: var(--muted); font-size: .85rem; }
.roots code { color: var(--fg); }
.legend { margin-top: .5rem; font-size: .8rem; color: var(--muted); }
.legend .x, .legend .n { font-weight: 600; }
.toolbar {
  position: sticky; top: 0; z-index: 5; display: flex; gap: .5rem;
  align-items: center; padding: .75rem 2rem; background: var(--bg);
  border-bottom: 1px solid var(--border); flex-wrap: wrap;
}
#filter {
  flex: 1 1 240px; min-width: 180px; padding: .45rem .7rem; font-size: .9rem;
  border: 1px solid var(--border); border-radius: 8px; background: var(--panel);
  color: var(--fg);
}
button {
  padding: .45rem .8rem; font-size: .85rem; border: 1px solid var(--border);
  border-radius: 8px; background: var(--panel); color: var(--fg);
  cursor: pointer;
}
button:hover { border-color: var(--accent); }
.chips { display: flex; gap: .4rem; flex-wrap: wrap; padding: 1rem 2rem 0; }
.chip {
  padding: .3rem .7rem; border-radius: 999px; background: var(--panel);
  border: 1px solid var(--border); font-size: .8rem; color: var(--muted);
}
.chip b { color: var(--fg); }
main { padding: 1rem 2rem; }
section { margin: 1.5rem 0; }
section > h2 {
  font-size: 1.1rem; margin: 0 0 .25rem;
  padding-bottom: .3rem; border-bottom: 2px solid var(--border);
}
.count { color: var(--muted); font-weight: 400; font-size: .85rem; }
details {
  border: 1px solid var(--border); border-radius: 8px; margin: .5rem 0;
  background: var(--panel);
}
details > summary {
  cursor: pointer; padding: .5rem .8rem; font-weight: 600; list-style: none;
}
details > summary::-webkit-details-marker { display: none; }
details > summary::before { content: "\\25B8 "; color: var(--muted); }
details[open] > summary::before { content: "\\25BE "; }
.body { padding: .3rem .8rem .7rem; }
ul.flags, ul.rows { list-style: none; margin: .2rem 0; padding: 0; }
ul.flags li, ul.rows li {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: .82rem; padding: .1rem .4rem; border-radius: 4px;
  white-space: pre-wrap; word-break: break-all;
}
.x { color: var(--x); }
.n { color: var(--n); }
.m { color: var(--m); }
li.x { background: var(--xbg); }
li.n { background: var(--nbg); }
.cat { margin: .5rem 0; }
.cat-name {
  font-size: .78rem; text-transform: uppercase; letter-spacing: .04em;
  color: var(--muted); margin-bottom: .15rem;
}
.more { color: var(--muted); font-style: italic; }
.err { color: var(--x); font-weight: 600; }
table.summary { border-collapse: collapse; margin: .4rem 0; font-size: .85rem; }
table.summary th, table.summary td {
  text-align: left; padding: .25rem .8rem .25rem 0; border: none;
}
table.summary th { color: var(--muted); font-weight: 500; }
table.summary td.xv { color: var(--x); font-family: ui-monospace, monospace; }
table.summary td.nv { color: var(--n); font-family: ui-monospace, monospace; }
tr.diff td.field { font-weight: 700; }
tr.diff td.field::after { content: " *"; color: var(--accent); }
.empty { color: var(--muted); font-style: italic; padding: .3rem 0; }
h3.lang {
  font-size: .95rem; margin: 1rem 0 .25rem; text-transform: capitalize;
  border-bottom: 1px solid var(--border); padding-bottom: .2rem;
}
.note {
  color: var(--muted); font-style: italic; font-size: .82rem;
  margin: .2rem 0 .4rem;
}
"""

_HTML_JS = """\
(function () {
  var input = document.getElementById('filter');
  var filterables = Array.prototype.slice.call(
      document.querySelectorAll('.filterable'));
  var details = Array.prototype.slice.call(
      document.querySelectorAll('details'));
  function apply() {
    var q = input.value.trim().toLowerCase();
    filterables.forEach(function (el) {
      var hay = (el.getAttribute('data-filter') || el.textContent)
          .toLowerCase();
      el.style.display = (!q || hay.indexOf(q) !== -1) ? '' : 'none';
    });
    if (q) { details.forEach(function (d) { d.open = true; }); }
  }
  input.addEventListener('input', apply);
  document.getElementById('expand').addEventListener('click', function () {
    details.forEach(function (d) { d.open = true; });
  });
  document.getElementById('collapse').addEventListener('click', function () {
    details.forEach(function (d) { d.open = false; });
  });
})();
"""


def _esc(value: object) -> str:
    return html.escape(str(value))


def _html_items(items: list[str], sign: str, css: str, verbose: bool) -> list[str]:
    """Render a capped, filterable list of ``-``/``+`` flag or symbol items."""
    ordered = sorted(items)
    shown = ordered if verbose else ordered[:_HTML_ITEM_CAP]
    out = [f'<li class="filterable {css}">{_esc(sign)} {_esc(item)}</li>'
           for item in shown]
    if not verbose and len(ordered) > _HTML_ITEM_CAP:
        out.append(f'<li class="more">... {len(ordered) - _HTML_ITEM_CAP} '
                   f'more (use -v to include all)</li>')
    return out


def _html_categories(categories: list[ArgCategoryDiff], verbose: bool) -> str:
    blocks: list[str] = []
    for category in categories:
        items = (_html_items(list(category.only_a), '-', 'x', verbose) +
                 _html_items(list(category.only_b), '+', 'n', verbose))
        if not items:
            continue
        blocks.append(
            f'<div class="cat"><div class="cat-name">{_esc(category.category)}'
            f'</div><ul class="flags">{"".join(items)}</ul></div>')
    return ''.join(blocks)


def _html_summary_table(a: Optional[Summary], b: Optional[Summary],
                        a_label: str, b_label: str) -> str:
    rows = _summary_fields(a, b)
    if not rows:
        return ''
    body = [f'<tr><th></th><th>{_esc(a_label)} (-)</th>'
            f'<th>{_esc(b_label)} (+)</th></tr>']
    for field_name, av, bv, differs in rows:
        cls = ' class="diff"' if differs else ''
        body.append(f'<tr{cls}><td class="field">{_esc(field_name)}</td>'
                    f'<td class="xv">{_esc(av)}</td>'
                    f'<td class="nv">{_esc(bv)}</td></tr>')
    return f'<table class="summary">{"".join(body)}</table>'


def _html_binaries(result: DiffResult, verbose: bool) -> str:
    b = result.binaries
    if b is None:
        return ''
    a_label, b_label = result.a_label, result.b_label
    parts = [f'<section id="binaries"><h2>Binaries '
             f'<span class="count">-{len(b.only_a)} only {_esc(a_label)}, '
             f'+{len(b.only_b)} only {_esc(b_label)}, {len(b.common)} common, '
             f'{len(b.kind_mismatch)} kind mismatch</span></h2>']

    def group(title: str, rows: list[str]) -> str:
        if not rows:
            return ''
        return (f'<details open><summary>{_esc(title)} ({len(rows)})</summary>'
                f'<div class="body"><ul class="rows">{"".join(rows)}</ul>'
                f'</div></details>')

    only_a = [f'<li class="filterable x">- {_esc(p.identity)} '
              f'<span class="count">({_esc(p.kind)}) [{_esc(p.target_name)}]'
              f'</span></li>' for p in b.only_a]
    only_b = [f'<li class="filterable n">+ {_esc(p.identity)} '
              f'<span class="count">({_esc(p.kind)}) [{_esc(p.target_name)}]'
              f'</span></li>' for p in b.only_b]
    mismatch = [f'<li class="filterable m">~ {_esc(ap.identity)} '
                f'<span class="count">- {_esc(ap.kind)} / + {_esc(bp.kind)}'
                f'</span></li>' for ap, bp in b.kind_mismatch]
    parts.append(group(f'Only in {a_label}', only_a))
    parts.append(group(f'Only in {b_label}', only_b))
    parts.append(group('Kind mismatches', mismatch))
    if verbose and b.common:
        common = [f'<li class="filterable">= {_esc(i)}</li>' for i in b.common]
        parts.append(group('Common', common))
    parts.append('</section>')
    return ''.join(parts)


def _html_compiler(result: DiffResult, verbose: bool) -> str:
    compiler = result.compiler
    if compiler is None:
        return ''
    langs = compiler.languages()
    total_targets = sum(len([t for t in d.per_target if t.any()])
                        for d in langs)
    parts = [f'<section id="compiler"><h2>Compiler flags '
             f'<span class="count">{len(langs)} languages, '
             f'{total_targets} targets differ</span></h2>']
    for lang in langs:
        parts.append(_html_language(lang, result.a_label, result.b_label,
                                    verbose))
    parts.append('</section>')
    return ''.join(parts)


def _html_language(lang: LanguageCompilerDiff, a_label: str, b_label: str,
                   verbose: bool) -> str:
    differing_targets = [t for t in lang.per_target if t.any()]
    parts = [f'<h3 class="lang">{_esc(lang.language)} '
             f'<span class="count">{len(lang.global_categories)} global '
             f'categories, {len(differing_targets)} targets differ</span></h3>']
    if lang.note:
        parts.append(f'<p class="note">{_esc(lang.note)}</p>')
    parts.append(f'<div class="body">'
                 f'{_html_summary_table(lang.a_summary, lang.b_summary, a_label, b_label)}'
                 f'</div>')
    global_html = _html_categories(lang.global_categories, verbose)
    if global_html:
        parts.append(f'<details open><summary>Global flag differences</summary>'
                     f'<div class="body">{global_html}</div></details>')
    for target in differing_targets:
        tu = ''
        sa, sb = target.a_summary, target.b_summary
        if sa and sb and sa.tu_count != sb.tu_count:
            tu = (f'<div class="cat"><span class="x">- tu_count {sa.tu_count}'
                  f'</span> / <span class="n">+ tu_count {sb.tu_count}</span>'
                  f'</div>')
        cats = _html_categories(target.categories, verbose)
        parts.append(
            f'<details class="filterable" '
            f'data-filter="{_esc(lang.language)} {_esc(target.name)}">'
            f'<summary>{_esc(target.name)}</summary>'
            f'<div class="body">{tu}{cats}</div></details>')
    return ''.join(parts)


def _html_linkers(result: DiffResult, verbose: bool) -> str:
    if 'args' not in result.dimensions:
        return ''
    parts = [f'<section id="linkers"><h2>Linker flags '
             f'<span class="count">{len(result.linkers)} products differ</span>'
             f'</h2>']
    if not result.linkers:
        parts.append('<p class="empty">No linker-flag differences.</p>')
    for linker in result.linkers:
        parts.append(
            f'<details class="filterable" data-filter="{_esc(linker.identity)}">'
            f'<summary>{_esc(linker.identity)}</summary>'
            f'<div class="body">{_html_categories(linker.categories, verbose)}'
            f'</div></details>')
    parts.append('</section>')
    return ''.join(parts)


def _html_symbols(result: DiffResult, verbose: bool) -> str:
    if not result.symbols:
        return ''
    differing = [s for s in result.symbols
                 if s.error or s.only_a or s.only_b]
    parts = [f'<section id="symbols"><h2>Exported symbols '
             f'<span class="count">{len(differing)} products differ</span></h2>']
    for symbol in sorted(result.symbols, key=lambda s: s.identity):
        if not (symbol.error or symbol.only_a or symbol.only_b):
            continue
        if symbol.error:
            parts.append(
                f'<details class="filterable" data-filter="{_esc(symbol.identity)}">'
                f'<summary>{_esc(symbol.identity)} '
                f'<span class="err">error</span></summary>'
                f'<div class="body"><p class="err">{_esc(symbol.error)}</p>'
                f'</div></details>')
            continue
        head = (f'{_esc(symbol.identity)} <span class="count">'
                f'{_esc(result.a_label)}={symbol.a_count} '
                f'{_esc(result.b_label)}={symbol.b_count} '
                f'common={symbol.common_count} | '
                f'<span class="x">-{len(symbol.only_a)}</span> '
                f'<span class="n">+{len(symbol.only_b)}</span></span>')
        items = (_html_items(list(symbol.only_a), '-', 'x', verbose) +
                 _html_items(list(symbol.only_b), '+', 'n', verbose))
        parts.append(
            f'<details class="filterable" data-filter="{_esc(symbol.identity)}">'
            f'<summary>{head}</summary><div class="body">'
            f'<ul class="flags">{"".join(items)}</ul></div></details>')
    parts.append('</section>')
    return ''.join(parts)


def _html_codesign(result: DiffResult) -> str:
    if not result.codesign:
        return ''
    differing = [c for c in result.codesign if c.any()]
    parts = [f'<section id="codesign"><h2>Codesigning '
             f'<span class="count">{len(differing)} products differ</span></h2>']
    for cs in sorted(result.codesign, key=lambda c: c.identity):
        if not cs.any():
            continue
        rows: list[str] = []
        if cs.error:
            rows.append(f'<p class="err">{_esc(cs.error)}</p>')
        else:
            a, b = cs.a, cs.b
            assert a and b
            if cs.identifier_differs():
                rows.append(_cs_row('identifier', a.identifier, b.identifier))
            if cs.authority_differs():
                rows.append(_cs_row('authority', a.authority or 'ad-hoc',
                                    b.authority or 'ad-hoc'))
            if cs.flags_differ():
                rows.append(_cs_row('flags', a.flags, b.flags))
            for key in sorted(cs.entitlements_only_a):
                rows.append(f'<li class="filterable x">- entitlement '
                            f'{_esc(key)}</li>')
            for key in sorted(cs.entitlements_only_b):
                rows.append(f'<li class="filterable n">+ entitlement '
                            f'{_esc(key)}</li>')
            for key in sorted(cs.entitlement_value_differs):
                rows.append(_cs_row(f'entitlement {key}',
                                    a.entitlements[key], b.entitlements[key]))
        parts.append(
            f'<details open class="filterable" data-filter="{_esc(cs.identity)}">'
            f'<summary>{_esc(cs.identity)}</summary>'
            f'<div class="body"><ul class="rows">{"".join(rows)}</ul></div>'
            f'</details>')
    parts.append('</section>')
    return ''.join(parts)


def _cs_row(field: str, aval: object, bval: object) -> str:
    return (f'<li class="filterable"><b>{_esc(field)}</b>: '
            f'<span class="x">- {_esc(aval)}</span> '
            f'<span class="n">+ {_esc(bval)}</span></li>')


def render_html(result: DiffResult, *, verbose: bool = False) -> str:
    """Render the diff as a single self-contained HTML page."""
    a_label, b_label = result.a_label, result.b_label
    chips: list[str] = []
    if result.binaries is not None:
        b = result.binaries
        chips.append(f'<span class="chip">Binaries: '
                     f'<b>{len(b.only_a) + len(b.only_b) + len(b.kind_mismatch)}</b>'
                     f' differ</span>')
    if result.compiler is not None:
        langs = result.compiler.languages()
        differing_targets = sum(len([t for t in d.per_target if t.any()])
                                for d in langs)
        names = '/'.join(d.language for d in langs) or 'none'
        chips.append(f'<span class="chip">Compiler ({names}): '
                     f'<b>{differing_targets}</b> targets differ</span>')
    if 'args' in result.dimensions:
        chips.append(f'<span class="chip">Linker: '
                     f'<b>{len(result.linkers)}</b> products</span>')
    if result.symbols:
        n = len([s for s in result.symbols
                 if s.error or s.only_a or s.only_b])
        chips.append(f'<span class="chip">Symbols: <b>{n}</b> products</span>')
    if result.codesign:
        n = len([c for c in result.codesign if c.any()])
        chips.append(f'<span class="chip">Codesign: <b>{n}</b> products</span>')

    sections = ''.join((
        _html_binaries(result, verbose),
        _html_compiler(result, verbose),
        _html_linkers(result, verbose),
        _html_symbols(result, verbose),
        _html_codesign(result),
    ))

    title = f'{_esc(a_label)} vs {_esc(b_label)} build diff'
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>{_HTML_CSS}</style>
</head>
<body>
<header>
<h1>{title}</h1>
<div class="roots">
  <span class="x">{_esc(a_label)} (-)</span>: <code>{_esc(result.a_root)}</code><br>
  <span class="n">{_esc(b_label)} (+)</span>: <code>{_esc(result.b_root)}</code><br>
  arch: <code>{_esc(result.arch)}</code>
</div>
<div class="legend">
  <span class="x">- red</span> = only in {_esc(a_label)} &nbsp;
  <span class="n">+ green</span> = only in {_esc(b_label)} &nbsp;
  <span class="m">~ purple</span> = differs
</div>
</header>
<div class="toolbar">
  <input id="filter" type="search" placeholder="Filter (products, flags, symbols)...">
  <button id="expand" type="button">Expand all</button>
  <button id="collapse" type="button">Collapse all</button>
</div>
<div class="chips">{"".join(chips)}</div>
<main>{sections}</main>
<script>{_HTML_JS}</script>
</body>
</html>
"""

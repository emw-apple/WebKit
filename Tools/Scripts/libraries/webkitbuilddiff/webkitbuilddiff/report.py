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
"""Render a :class:`~webkitbuilddiff.diff.DiffResult` as text or JSON.

Convention throughout the text report: ``-`` marks something present only in
the Xcode build, ``+`` marks something present only in the Ninja build.
"""
from __future__ import annotations

import dataclasses
import html
import json
from enum import Enum
from pathlib import Path
from typing import Any, cast

from webkitbuilddiff.diff import (
    ArgCategoryDiff, CodesignDiff, CompilerDiff, DiffResult, SymbolDiff,
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
        lines += _preview(list(category.only_xcode), verbose, '-')
        lines += _preview(list(category.only_ninja), verbose, '+')
    return lines


def _summary_lines(compiler: CompilerDiff) -> list[str]:
    x = compiler.xcode_summary
    n = compiler.ninja_summary
    if not (x and n):
        return []
    lines = [f'    {"":16} {"xcode":>14} {"ninja":>14}']
    for field_name, xv, nv in (
            ('tu_count', x.tu_count, n.tu_count),
            ('optimization', x.optimization, n.optimization),
            ('debug_info', x.debug_info, n.debug_info),
            ('cxx_std', x.cxx_std, n.cxx_std),
            ('stdlib', x.stdlib, n.stdlib),
            ('lto', x.lto, n.lto),
            ('sanitizers', ','.join(x.sanitizers) or '-',
             ','.join(n.sanitizers) or '-'),
            ('modules', x.modules, n.modules)):
        marker = ' *' if xv != nv else ''
        lines.append(f'    {field_name:16} {str(xv):>14} {str(nv):>14}{marker}')
    return lines


def _symbol_line(symbol: SymbolDiff) -> str:
    if symbol.error:
        return f'  [{symbol.identity}] error: {symbol.error}'
    return (f'  [{symbol.identity}] xcode={symbol.xcode_count} '
            f'ninja={symbol.ninja_count} common={symbol.common_count} | '
            f'-{len(symbol.only_xcode)} +{len(symbol.only_ninja)}')


def _codesign_lines(cs: CodesignDiff, verbose: bool) -> list[str]:
    if cs.error:
        return [f'  [{cs.identity}] error: {cs.error}']
    lines = [f'  [{cs.identity}]']
    x, n = cs.xcode, cs.ninja
    assert x and n
    if cs.identifier_differs():
        lines.append(f'      identifier: - {x.identifier}  + {n.identifier}')
    if cs.authority_differs():
        lines.append(f'      authority:  - {x.authority or "ad-hoc"}  '
                     f'+ {n.authority or "ad-hoc"}')
    if cs.flags_differ():
        lines.append(f'      flags:      - {x.flags}  + {n.flags}')
    if cs.entitlements_only_xcode:
        lines += _preview(list(cs.entitlements_only_xcode), verbose,
                          '- entitlement')
    if cs.entitlements_only_ninja:
        lines += _preview(list(cs.entitlements_only_ninja), verbose,
                          '+ entitlement')
    if cs.entitlement_value_differs:
        for key in sorted(cs.entitlement_value_differs):
            lines.append(f'      ~ entitlement {key}: '
                         f'- {x.entitlements[key]!r}  + {n.entitlements[key]!r}')
    return lines if len(lines) > 1 else []


def render_text(result: DiffResult, *, verbose: bool = False) -> str:
    lines: list[str] = []
    lines.append('Comparing Xcode vs Ninja builds')
    lines.append(f'  Xcode (-): {result.xcode_root}')
    lines.append(f'  Ninja (+): {result.ninja_root}')
    lines.append(f'  arch:      {result.arch}')
    lines.append('')

    if result.binaries is not None:
        b = result.binaries
        lines.append(f'== Binaries ==  '
                     f'-{len(b.only_xcode)} only-xcode, '
                     f'+{len(b.only_ninja)} only-ninja, '
                     f'{len(b.common)} common, '
                     f'{len(b.kind_mismatch)} kind mismatch')
        for product in b.only_xcode:
            lines.append(f'    - {product.identity} ({product.kind}) '
                         f'[{product.target_name}]')
        for product in b.only_ninja:
            lines.append(f'    + {product.identity} ({product.kind}) '
                         f'[{product.target_name}]')
        for xp, np in b.kind_mismatch:
            lines.append(f'    ~ {xp.identity}: - {xp.kind}  + {np.kind}')
        if verbose:
            for identity in b.common:
                lines.append(f'      = {identity}')
        lines.append('')

    if result.compiler is not None:
        lines.append('== Compiler flags (global) ==')
        lines += _summary_lines(result.compiler)
        if result.compiler.global_categories:
            lines.append('    flag differences:')
            lines += _category_lines(result.compiler.global_categories, verbose,
                                     indent='      ')
        differing_targets = [t for t in result.compiler.per_target if t.any()]
        if differing_targets:
            lines.append(f'  per-target differences '
                         f'({len(differing_targets)} targets):')
            for target in differing_targets:
                lines.append(f'    [{target.name}]')
                sx, sn = target.xcode_summary, target.ninja_summary
                if sx and sn and sx.tu_count != sn.tu_count:
                    lines.append(f'      tu_count: - {sx.tu_count}  '
                                 f'+ {sn.tu_count}')
                lines += _category_lines(target.categories, verbose,
                                         indent='      ')
        lines.append('')

    if result.linkers or 'args' in result.dimensions:
        lines.append(f'== Linker flags ==  {len(result.linkers)} products differ')
        for linker in result.linkers:
            lines.append(f'  [{linker.identity}]')
            lines += _category_lines(linker.categories, verbose)
        lines.append('')

    if result.symbols:
        differing_syms = [s for s in result.symbols
                          if s.error or s.only_xcode or s.only_ninja]
        lines.append(f'== Exported symbols ==  '
                     f'{len(differing_syms)} products differ')
        for symbol in sorted(result.symbols, key=lambda s: s.identity):
            if not (symbol.error or symbol.only_xcode or symbol.only_ninja):
                continue
            lines.append(_symbol_line(symbol))
            if verbose:
                lines += _preview(list(symbol.only_xcode), verbose, '-')
                lines += _preview(list(symbol.only_ninja), verbose, '+')
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
        items = (_html_items(list(category.only_xcode), '-', 'x', verbose) +
                 _html_items(list(category.only_ninja), '+', 'n', verbose))
        if not items:
            continue
        blocks.append(
            f'<div class="cat"><div class="cat-name">{_esc(category.category)}'
            f'</div><ul class="flags">{"".join(items)}</ul></div>')
    return ''.join(blocks)


def _html_summary_table(compiler: CompilerDiff) -> str:
    x = compiler.xcode_summary
    n = compiler.ninja_summary
    if not (x and n):
        return ''
    rows = [
        ('tu_count', x.tu_count, n.tu_count),
        ('optimization', x.optimization, n.optimization),
        ('debug_info', x.debug_info, n.debug_info),
        ('cxx_std', x.cxx_std, n.cxx_std),
        ('stdlib', x.stdlib, n.stdlib),
        ('lto', x.lto, n.lto),
        ('sanitizers', ','.join(x.sanitizers) or '-',
         ','.join(n.sanitizers) or '-'),
        ('modules', x.modules, n.modules),
    ]
    body = [('<tr><th></th><th>Xcode (-)</th><th>Ninja (+)</th></tr>')]
    for field_name, xv, nv in rows:
        cls = ' class="diff"' if xv != nv else ''
        body.append(f'<tr{cls}><td class="field">{_esc(field_name)}</td>'
                    f'<td class="xv">{_esc(xv)}</td>'
                    f'<td class="nv">{_esc(nv)}</td></tr>')
    return f'<table class="summary">{"".join(body)}</table>'


def _html_binaries(result: DiffResult, verbose: bool) -> str:
    b = result.binaries
    if b is None:
        return ''
    parts = [f'<section id="binaries"><h2>Binaries '
             f'<span class="count">-{len(b.only_xcode)} only-xcode, '
             f'+{len(b.only_ninja)} only-ninja, {len(b.common)} common, '
             f'{len(b.kind_mismatch)} kind mismatch</span></h2>']

    def group(title: str, rows: list[str]) -> str:
        if not rows:
            return ''
        return (f'<details open><summary>{_esc(title)} ({len(rows)})</summary>'
                f'<div class="body"><ul class="rows">{"".join(rows)}</ul>'
                f'</div></details>')

    only_x = [f'<li class="filterable x">- {_esc(p.identity)} '
              f'<span class="count">({_esc(p.kind)}) [{_esc(p.target_name)}]'
              f'</span></li>' for p in b.only_xcode]
    only_n = [f'<li class="filterable n">+ {_esc(p.identity)} '
              f'<span class="count">({_esc(p.kind)}) [{_esc(p.target_name)}]'
              f'</span></li>' for p in b.only_ninja]
    mismatch = [f'<li class="filterable m">~ {_esc(xp.identity)} '
                f'<span class="count">- {_esc(xp.kind)} / + {_esc(np.kind)}'
                f'</span></li>' for xp, np in b.kind_mismatch]
    parts.append(group('Only in Xcode', only_x))
    parts.append(group('Only in Ninja', only_n))
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
    differing_targets = [t for t in compiler.per_target if t.any()]
    parts = [f'<section id="compiler"><h2>Compiler flags '
             f'<span class="count">{len(compiler.global_categories)} global '
             f'categories, {len(differing_targets)} targets differ</span></h2>']
    parts.append(f'<div class="body">{_html_summary_table(compiler)}</div>')
    global_html = _html_categories(compiler.global_categories, verbose)
    if global_html:
        parts.append(f'<details open><summary>Global flag differences</summary>'
                     f'<div class="body">{global_html}</div></details>')
    for target in differing_targets:
        tu = ''
        sx, sn = target.xcode_summary, target.ninja_summary
        if sx and sn and sx.tu_count != sn.tu_count:
            tu = (f'<div class="cat"><span class="x">- tu_count {sx.tu_count}'
                  f'</span> / <span class="n">+ tu_count {sn.tu_count}</span>'
                  f'</div>')
        cats = _html_categories(target.categories, verbose)
        parts.append(
            f'<details class="filterable" data-filter="{_esc(target.name)}">'
            f'<summary>{_esc(target.name)}</summary>'
            f'<div class="body">{tu}{cats}</div></details>')
    parts.append('</section>')
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
                 if s.error or s.only_xcode or s.only_ninja]
    parts = [f'<section id="symbols"><h2>Exported symbols '
             f'<span class="count">{len(differing)} products differ</span></h2>']
    for symbol in sorted(result.symbols, key=lambda s: s.identity):
        if not (symbol.error or symbol.only_xcode or symbol.only_ninja):
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
                f'xcode={symbol.xcode_count} ninja={symbol.ninja_count} '
                f'common={symbol.common_count} | '
                f'<span class="x">-{len(symbol.only_xcode)}</span> '
                f'<span class="n">+{len(symbol.only_ninja)}</span></span>')
        items = (_html_items(list(symbol.only_xcode), '-', 'x', verbose) +
                 _html_items(list(symbol.only_ninja), '+', 'n', verbose))
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
            x, n = cs.xcode, cs.ninja
            assert x and n
            if cs.identifier_differs():
                rows.append(_cs_row('identifier', x.identifier, n.identifier))
            if cs.authority_differs():
                rows.append(_cs_row('authority', x.authority or 'ad-hoc',
                                    n.authority or 'ad-hoc'))
            if cs.flags_differ():
                rows.append(_cs_row('flags', x.flags, n.flags))
            for key in sorted(cs.entitlements_only_xcode):
                rows.append(f'<li class="filterable x">- entitlement '
                            f'{_esc(key)}</li>')
            for key in sorted(cs.entitlements_only_ninja):
                rows.append(f'<li class="filterable n">+ entitlement '
                            f'{_esc(key)}</li>')
            for key in sorted(cs.entitlement_value_differs):
                rows.append(_cs_row(f'entitlement {key}',
                                    x.entitlements[key], n.entitlements[key]))
        parts.append(
            f'<details open class="filterable" data-filter="{_esc(cs.identity)}">'
            f'<summary>{_esc(cs.identity)}</summary>'
            f'<div class="body"><ul class="rows">{"".join(rows)}</ul></div>'
            f'</details>')
    parts.append('</section>')
    return ''.join(parts)


def _cs_row(field: str, xval: object, nval: object) -> str:
    return (f'<li class="filterable"><b>{_esc(field)}</b>: '
            f'<span class="x">- {_esc(xval)}</span> '
            f'<span class="n">+ {_esc(nval)}</span></li>')


def render_html(result: DiffResult, *, verbose: bool = False) -> str:
    """Render the diff as a single self-contained HTML page."""
    chips: list[str] = []
    if result.binaries is not None:
        b = result.binaries
        chips.append(f'<span class="chip">Binaries: '
                     f'<b>{len(b.only_xcode) + len(b.only_ninja) + len(b.kind_mismatch)}</b>'
                     f' differ</span>')
    if result.compiler is not None:
        differing_targets = len([t for t in result.compiler.per_target
                                 if t.any()])
        chips.append(f'<span class="chip">Compiler: '
                     f'<b>{len(result.compiler.global_categories)}</b> global, '
                     f'<b>{differing_targets}</b> targets</span>')
    if 'args' in result.dimensions:
        chips.append(f'<span class="chip">Linker: '
                     f'<b>{len(result.linkers)}</b> products</span>')
    if result.symbols:
        n = len([s for s in result.symbols
                 if s.error or s.only_xcode or s.only_ninja])
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

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Xcode vs Ninja build diff</title>
<style>{_HTML_CSS}</style>
</head>
<body>
<header>
<h1>Xcode vs Ninja build diff</h1>
<div class="roots">
  <span class="x">Xcode (-)</span>: <code>{_esc(result.xcode_root)}</code><br>
  <span class="n">Ninja (+)</span>: <code>{_esc(result.ninja_root)}</code><br>
  arch: <code>{_esc(result.arch)}</code>
</div>
<div class="legend">
  <span class="x">- red</span> = only in the Xcode build &nbsp;
  <span class="n">+ green</span> = only in the Ninja build &nbsp;
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


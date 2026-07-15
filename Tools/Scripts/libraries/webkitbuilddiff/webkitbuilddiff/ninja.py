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
"""Extract a :class:`~webkitbuilddiff.model.BuildGraph` from a CMake/Ninja build.

Two files describe a CMake/Ninja build:

* ``compile_commands.json`` - the standard compilation database, with one entry
  per translation unit (``directory``/``command``/``file``/``output``). It
  covers every compile but no link steps.
* ``build.ninja`` - link/archive ``build`` statements
  (``build <out>: CXX_{SHARED,STATIC,EXECUTABLE}_LIBRARY_LINKER__<target>_<config> ...``)
  followed by indented variable assignments (``LINK_FLAGS``, ``LINK_LIBRARIES``,
  ``ARCH_FLAGS``, ...) whose values are fully literal. Codesigning is a
  ``codesign`` call inside the rule's ``POST_BUILD`` variable.
"""
from __future__ import annotations

import json
import re
import shlex
from pathlib import Path
from typing import Optional

from webkitbuilddiff.model import (
    BuildGraph, BuildSystem, CompileStep, LinkStep, Product, ProductKind,
    Target, classify_product,
)

# CMakeFiles/<target>.dir/ path segment identifies a compile's owning target.
_OBJECT_TARGET = re.compile(r'CMakeFiles/([^/]+)\.dir/')

# Ninja link rule: <LANG>_<KIND>_LIBRARY_LINKER__<target>_<config> or
# <LANG>_EXECUTABLE_LINKER__<target>_<config>.
_LINK_RULE = re.compile(r'_LINKER__(?P<rest>.+)$')

# codesign invocation embedded in a POST_BUILD command chain.
_CODESIGN = re.compile(r'\bcodesign\b(?P<args>[^&]*)')
_ENTITLEMENTS = re.compile(r'--entitlements\s+(\S+)')
_SIGN_IDENTITY = re.compile(r'--sign\s+(\S+)')


def _unescape(value: str) -> str:
    """Undo Ninja's ``$``-escaping within a variable value."""
    # Protect '$$' (literal '$') while translating the other escapes.
    return (value.replace('$$', '\x00')
            .replace('$ ', ' ')
            .replace('$:', ':')
            .replace('\x00', '$'))


def _split_link_rule(rule: str) -> Optional[str]:
    """Return the target name from a link rule, or None if not a link rule."""
    match = _LINK_RULE.search(rule)
    if not match:
        return None
    # The remainder is <target>_<config>; the config is the final segment.
    target, _, _config = match.group('rest').rpartition('_')
    return target or match.group('rest')


def _parse_build_statements(build_ninja: Path) -> list[tuple[str, dict[str, str]]]:
    """Yield ``(target_name, variables)`` for each link ``build`` statement."""
    results: list[tuple[str, dict[str, str]]] = []
    lines = build_ninja.read_text().splitlines()
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        if not line.startswith('build '):
            i += 1
            continue
        # Consume any '$'-continuation lines of the build statement itself.
        stmt = line
        while stmt.endswith('$') and i + 1 < n:
            i += 1
            stmt = stmt[:-1] + lines[i]
        i += 1
        # `build <outputs>: <rule> <inputs>` - we only need the rule here.
        _outputs, _, remainder = stmt.partition(': ')
        if not remainder:
            continue
        rule = remainder.split(' ', 1)[0]
        target = _split_link_rule(rule)
        if target is None:
            continue
        # Collect the indented variable block.
        variables: dict[str, str] = {}
        while i < n:
            var_line = lines[i]
            if not var_line.startswith(' '):
                break
            i += 1
            stripped = var_line.strip()
            if not stripped:
                continue
            key, sep, value = stripped.partition(' = ')
            if not sep:
                continue
            while value.endswith('$') and i < n:
                value = value[:-1] + lines[i].strip()
                i += 1
            variables[key] = _unescape(value)
        results.append((target, variables))
    return results


def _assemble_link_args(variables: dict[str, str]) -> list[str]:
    """Reconstruct a link argv from a link rule's semantic variables."""
    args: list[str] = []
    for key in ('LANGUAGE_COMPILE_FLAGS', 'ARCH_FLAGS', 'LINK_FLAGS',
                'LINK_PATH', 'LINK_LIBRARIES'):
        value = variables.get(key)
        if value:
            args += shlex.split(value)
    soname_flag = variables.get('SONAME_FLAG')
    soname = variables.get('SONAME')
    if soname_flag and soname:
        args.append(soname_flag)
        args.append(variables.get('INSTALLNAME_DIR', '') + soname)
    return args


def _codesign_annotation(variables: dict[str, str]) -> tuple[Optional[Path],
                                                             Optional[str]]:
    """Extract (entitlements_source, sign_identity) from a POST_BUILD chain."""
    post_build = variables.get('POST_BUILD', '')
    match = _CODESIGN.search(post_build)
    if not match:
        return None, None
    codesign_args = match.group('args')
    ent = _ENTITLEMENTS.search(codesign_args)
    identity = _SIGN_IDENTITY.search(codesign_args)
    return (Path(ent.group(1)) if ent else None,
            identity.group(1) if identity else None)


def load(build_dir: Path) -> BuildGraph:
    """Parse the Ninja build at ``build_dir`` into a BuildGraph."""
    graph = BuildGraph(build_system=BuildSystem.NINJA, root=build_dir)

    def target_for(name: str) -> Target:
        target = graph.targets.get(name)
        if target is None:
            target = Target(name=name)
            graph.targets[name] = target
        return target

    # Compile steps from the compilation database.
    compile_db = build_dir / 'compile_commands.json'
    if compile_db.exists():
        with open(compile_db) as db_file:
            entries = json.load(db_file)
        for entry in entries:
            output = entry.get('output', '')
            command = entry.get('command')
            args = (shlex.split(command) if command
                    else list(entry.get('arguments', [])))
            match = _OBJECT_TARGET.search(output)
            name = match.group(1) if match else '<unknown>'
            source = entry.get('file')
            target_for(name).compile_steps.append(CompileStep(
                source=Path(source) if source else None,
                output=Path(output) if output else None,
                args=args))

    # Link/archive steps and products from build.ninja.
    build_ninja = build_dir / 'build.ninja'
    if build_ninja.exists():
        for target_name, variables in _parse_build_statements(build_ninja):
            target_file = variables.get('TARGET_FILE')
            if not target_file:
                continue
            output_path = build_dir / target_file
            identity, kind = classify_product(output_path)
            is_static = kind == ProductKind.STATIC_LIB
            target = target_for(target_name)
            target.products.append(Product(
                identity=identity, kind=kind, output_path=output_path,
                target_name=target_name))
            target.link_step = LinkStep(
                product_identity=identity,
                linker='libtool' if is_static else 'clang++',
                args=_assemble_link_args(variables))
            entitlements, identity_name = _codesign_annotation(variables)
            if entitlements:
                target.codesign_entitlements_source = entitlements
            if identity_name:
                target.codesign_identity = identity_name

    return graph

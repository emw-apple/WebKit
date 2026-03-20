# Copyright (C) 2025 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from decimal import Decimal
from pathlib import Path
from enum import Enum
from typing import Any, NamedTuple, Optional, TypeVar, Union
from ._toml_line_info import LineInfo, loads_with_lines

if sys.version_info < (3, 11):
    from webkitapipy._vendor import tomli as tomllib
else:
    import tomllib

if sys.version_info < (3, 11):
    class StrEnum(str, Enum):
        def __str__(self):
            return self.value
else:
    from enum import StrEnum

VERSION_REQ = re.compile(r'(?P<platform>[a-zA-Z]+) ?(?P<op>==|!=|>|>=|<=|<) '
                         r'?(?P<version>\d+(\.\d+\*?|\.\*)?)', flags=re.ASCII)

@dataclass(frozen=True)
class AllowedSPI:
    reason: AllowedReason
    bugs: Bugs

    symbols: list[Declaration]
    selectors: list[Selector]
    classes: list[Declaration]
    swift_decls: list[SwiftDecl] = field(default_factory=list)

    requires: list[str] = field(default_factory=list)
    requires_os: list[RequiredVersion] = field(default_factory=list)
    requires_sdk: list[RequiredVersion] = field(default_factory=list)
    allow_unused: bool = False

    @dataclass(frozen=True)
    class Declaration:
        name: str
        line: int
        cols: int

    @dataclass(frozen=True)
    class Selector(Declaration):
        class_: Optional[str]

    class Bugs(NamedTuple):
        request: Optional[str]
        cleanup: Optional[str]

    class RequiredVersion(NamedTuple):
        platform: str
        operator: str
        version: str

    @dataclass(frozen=True)
    class SwiftDecl(Declaration):
        type_kinds: Optional[dict[str, str]] = None
        extension: Optional[str] = None
        extension_base_depth: Optional[int] = None

class AllowedReason(StrEnum):
    LEGACY = 'legacy'

    # For SPI that we intend to replace with API before the next release.
    TEMPORARY_USAGE = 'temporary-usage'

    # For pre-adopting new API before it is available in the SDK. There should
    # be no active `staging` entries when WebKit ships.
    STAGING = 'staging'

    # For SPI implementing non-essential web engine features that a browser
    # vendor would either not use or provide their own implementation.
    NOT_WEB_ESSENTIAL = 'not-web-essential'

    # For SPI that has same behavior as API except in internal builds.
    EQUIVALENT_API = 'equivalent-api'


def _transform_wildcard_version(op: str, version: str) -> tuple[str, str]:
    '''
    As syntactic sugar, transform a gte (<=) version clause ending in a
    wildcard into its logical equivalent. This is helpful to avoid writing what
    appear to be unreleased marketing versions in allowlists. For example:

        iOS <= 26.*      =>     iOS < 27.00
        iOS <= 26.3*     =>     iOS < 26.40
    '''
    if op != '<=':
        raise ValueError(op)

    new_version = Decimal(version.replace('*', '99'))
    # `exponent` represents the location of the decimal point, which varies
    # based on where the "*" appeared. For example: 26.* => 26.99 => E -2
    _, _, exponent = new_version.as_tuple()
    assert isinstance(exponent, int), \
        f'exponent of version number "{version}" outside representible bounds'
    # Increment to the next possible new_version number.
    new_version += Decimal(10) ** exponent
    # Major and minor components must be no more than two digits.
    new_version = min(new_version, Decimal('99.99'))
    return '<', f'{new_version:.2f}'


T = TypeVar('T')

class ValidationError(Exception):
    def __init__(self, message: str, filename: str, line: int, cols: int):
        self.message = message
        self.filename = filename
        self.line = line
        self.cols = cols

    def __str__(self):
        return f'error: {self.filename}:{self.line}:{self.cols}: {self.message}'


@dataclass
class AllowList:
    allowed_spi: list[AllowedSPI]

    @classmethod
    def _from_dict(cls, doc: dict[str, Any], line_info: LineInfo, filename: str) -> AllowList:
        entries = []
        seen_syms: dict[AllowedSPI.Declaration, AllowedSPI] = {}
        seen_sels: dict[AllowedSPI.Declaration, AllowedSPI] = {}
        seen_clss: dict[AllowedSPI.Declaration, AllowedSPI] = {}

        def validating_list(items: list[T]) -> list[T]:
            if not isinstance(items, list):
                raise ValidationError('expected a list', filename,
                                      *line_info[id(items)])
            return items

        for reason in AllowedReason:
            for entry in doc.pop(reason.value, []):
                clss = []
                for name in validating_list(entry.pop('classes', [])):
                    line, cols = line_info[id(name)]
                    clss.append(AllowedSPI.Declaration(name, line, cols))
                reqs = validating_list(entry.pop('requires', []))
                sels = []
                for sel in validating_list(entry.pop('selectors', [])):
                    receiver = sel.get('class')
                    line, cols = line_info[id(sel)]
                    sels.append(AllowedSPI.Selector(
                        name=sel['name'],
                        class_=None if receiver == '?' else receiver,
                        line=line, cols=cols
                    ))
                # Symbols use C-style name mangling rules (implicit leading
                # underscore), so that the names of C symbols in allowlists
                # match their spelling in code. Internally, symbols are tracked
                # in their raw form.
                syms = []
                for sym in validating_list(entry.pop('symbols', [])):
                    line, cols = line_info[id(sym)]
                    syms.append(AllowedSPI.Declaration(f'_{sym}', line, cols))

                swift_decls = []
                for decl in validating_list(entry.pop('swift-decls', [])):
                    line, cols = line_info[id(decl)]
                    decl = AllowedSPI.SwiftDecl(**decl, line=line, cols=cols)
                    swift_decls.append(decl)

                bugs = AllowedSPI.Bugs(entry.pop('request', None),
                                       entry.pop('cleanup', None))
                allow_unused = bool(entry.pop('allow-unused', False))

                requires_os: list[AllowedSPI.RequiredVersion] = []
                requires_sdk: list[AllowedSPI.RequiredVersion] = []
                for required_versions, key in ((requires_os, 'requires-os'),
                                               (requires_sdk, 'requires-sdk')):
                    for clause in validating_list(entry.pop(key, [])):
                        m = VERSION_REQ.fullmatch(clause)
                        if not m:
                            raise ValidationError(
                                '<input file>:6:16: unmatched requirement clause',
                                filename, *line_info[id(clause)]
                            )
                        platform, op, version = m.group('platform', 'op',
                                                        'version')
                        if version.endswith('*'):
                            if op != '<=':
                                raise ValidationError(
                                    'wildcard in required version only '
                                    'supported when operator is "<="',
                                    filename, *line_info[id(clause)]
                                )
                            op, version = _transform_wildcard_version(op, version)
                        required_versions.append(
                            AllowedSPI.RequiredVersion(platform, op,
                                                       version))
                allow = AllowedSPI(reason=reason, bugs=bugs, symbols=syms,
                                   selectors=sels, classes=clss,
                                   swift_decls=swift_decls, requires=reqs,
                                   allow_unused=allow_unused,
                                   requires_os=requires_os,
                                   requires_sdk=requires_sdk)

                if reason == AllowedReason.TEMPORARY_USAGE:
                    if not bugs.cleanup:
                        # Typically a temporary-use entry should have *both* a
                        # request and cleanup bug, but in some cases the
                        # temporary usage does not require new API to resolve.
                        # For example, using SPI to work around a bug in an
                        # underlying framework.
                        raise ValidationError(
                            'Allowlist entries marked temporary-usage must '
                            'have a "cleanup" bug',
                            filename, *line_info[id(entry)]
                        )
                elif reason == AllowedReason.STAGING:
                    pass
                    # FIXME: Disabled while allowlist entries are cleaned up,
                    # cf. rdar://170360205
                    # if not requires_sdk:
                    #     raise ValueError('Allowlist entries marked staging '
                    #                      'must have a "requires-sdk" clause '
                    #                      'that specifies which SDK(s) do not '
                    #                      'yet have the API avaiable: {allow}')
                elif reason != AllowedReason.LEGACY:
                    if not bugs.request:
                        raise ValueError('Allowlist entries must have a '
                                         f'"request" bug: {allow}')

                if entry:
                    raise ValidationError(
                        f'Unrecognized items in allowlist entry: {entry}',
                        filename, *line_info[id(entry)]
                    )
                entries.append(allow)
        if doc:
            raise ValidationError(
                f'Unrecognized items in allowlist: {doc.keys()}',
                filename, line=1, cols=1
            )
        return cls(entries)

    @classmethod
    def from_file(cls, config_file: Path) -> AllowList:
        return cls.from_text(config_file.read_text(),
                             filename=str(config_file))

    @classmethod
    def from_text(cls, text: str, filename='<input file>') -> AllowList:
        try:
            doc, line_info = loads_with_lines(text)
        except tomllib.TOMLDecodeError as error:
            if sys.version_info < (3, 11):
                raise ValueError(f'{filename}: error: decode failed') from error
            else:
                error.add_note(f'{filename}: error: decode failed"')
                raise
        return cls._from_dict(doc, line_info=line_info, filename=filename)


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

import tempfile
from pathlib import Path
from unittest import TestCase

from .allow import AllowList, AllowedSPI, AllowedReason, ValidationError, _transform_wildcard_version

Toml = b'''
[[temporary-usage]]
request = "rdar://123456789"
cleanup = "rdar://123456790"
symbols = ["TemporarilyAllowedSymbol"]
swift-decls = [{ name = "UIKit._SomeSPIClass" }]
selectors = [{ name = "_initWithTemporarilyAllowedData:", class = "?" }]
classes = ["NSTemporarilyAllowed"]

[[not-web-essential]]
request = "rdar://234567890"
symbols = ["Permanent1", "Permanent2"]
requires = ["ENABLE_FOO", "!ENABLE_BAR"]
requires-os = ["iOS<26.0", "iOS>=18.2"]
requires-sdk = [ "iOS <= 26.*" ]
'''

A1 = AllowedSPI(reason=AllowedReason.TEMPORARY_USAGE,
                bugs=AllowedSPI.Bugs(request='rdar://123456789',
                                     cleanup='rdar://123456790'),
                symbols=[AllowedSPI.Declaration('_TemporarilyAllowedSymbol', 5, 12)],
                selectors=[AllowedSPI.Selector(
                    name='_initWithTemporarilyAllowedData:',
                    class_=None,
                    line=7, cols=14
                )],
                classes=[AllowedSPI.Declaration('NSTemporarilyAllowed', 8, 12)],
                swift_decls=[AllowedSPI.SwiftDecl(
                    name='UIKit._SomeSPIClass',
                    line=6, cols=16
                )])
A2 = AllowedSPI(reason=AllowedReason.NOT_WEB_ESSENTIAL,
                bugs=AllowedSPI.Bugs(request='rdar://234567890', cleanup=None),
                symbols=[
                    AllowedSPI.Declaration('_Permanent1', 12, 12),
                    AllowedSPI.Declaration('_Permanent2', 12, 26)
                ],
                selectors=[], classes=[], requires=['ENABLE_FOO', '!ENABLE_BAR'],
                requires_os=[AllowedSPI.RequiredVersion('iOS', '<', '26.0'),
                             AllowedSPI.RequiredVersion('iOS', '>=', '18.2')],
                requires_sdk=[AllowedSPI.RequiredVersion('iOS', '<', '27.00')])


class TestAllowList(TestCase):
    def setUp(self):
        self.tempfile = tempfile.NamedTemporaryFile(prefix='TestAllowList-')
        self.tempfile.write(Toml)
        self.tempfile.flush()

        self.file = Path(self.tempfile.name)

    def tearDown(self):
        self.tempfile.close()

    def test_parse(self):
        # When parsing the allowlist fixture...
        allowlist = AllowList.from_file(self.file)
        # It should load the two allowances:
        self.assertIn(A1, allowlist.allowed_spi)
        self.assertIn(A2, allowlist.allowed_spi)

    def test_allowed_reasons(self):
        # It supports the permanent exception categories:
        AllowList.from_text('''
[[not-web-essential]]
request = "rdar://2"
classes = ["Foo"]''')
        AllowList.from_text('''
[[equivalent-api]]
request = "rdar://3"
classes = ["Foo"]''')

        # It supports temporary exceptions from bugzilla URLs:
        AllowList.from_text('''
[[temporary-usage]]
request = "https://bugs.webkit.org/show_bug.cgi?id=12345"
cleanup = "https://bugs.webkit.org/show_bug.cgi?id=12345"
classes = ["Foo"]''')
        AllowList.from_text('''
[[temporary-usage]]
request = "https://webkit.org/b/12345"
cleanup = "https://webkit.org/b/12346"
classes = ["Foo"]''')

        # It rejects made up category names:
        with self.assertRaisesRegex(ValidationError, 'category-that-doesnt-exist'):
            AllowList.from_text('''
[[category-that-doesnt-exist]]
classes = ["Foo"]''')

    def test_repetition_allowed_with_requires(self):
        AllowList.from_text('''
[[temporary-usage]]
request = "rdar://1"
cleanup = "rdar://2"
classes = ["Foo"]
requires = ["A"]

[[temporary-usage]]
request = "rdar://3"
cleanup = "rdar://4"
classes = ["Foo"]
requires = ["B"]''')

    def test_no_string(self):
        with self.assertRaisesRegex(ValidationError, 'error: '
                                    '<input file>:5:11: expected a list'):
            AllowList.from_text('''
[[temporary-usage]]
request = "rdar://1"
cleanup = "rdar://2"
classes = "Foo"''')

    def test_invalid_version_requirements(self):
        with self.assertRaisesRegex(ValidationError, '<input file>:6:16: '
                                    'unmatched requirement clause'):
            AllowList.from_text('''
[[temporary-usage]]
request = "rdar://1"
cleanup = "rdar://2"
classes = ["Foo"]
requires-os = ["15.0 < macOS"]''')
        with self.assertRaisesRegex(ValidationError, '<input file>:6:16: '
                                    'unmatched requirement clause'):
            AllowList.from_text('''
[[temporary-usage]]
request = "rdar://1"
cleanup = "rdar://2"
classes = ["Foo"]
requires-os = ["macOS=15.0"]''')
        with self.assertRaisesRegex(ValidationError, '<input file>:6:16: '
                                    'wildcard in required version only '
                                    'supported when operator is "<="'):
            AllowList.from_text('''
[[temporary-usage]]
request = "rdar://1"
cleanup = "rdar://2"
classes = ["Foo"]
requires-os = ["macOS<15.0*"]''')

    def test_required_fields(self):
        with self.assertRaisesRegex(ValidationError, '<input file>:2:1: '
                                    'Allowlist entries marked temporary-usage '
                                    'must have a "cleanup" bug'):
            AllowList.from_text('''
[[temporary-usage]]
request = "rdar://1"
classes = ["Foo"]''')

        # FIXME: Disabled while allowlist entries are cleaned up,
        # cf. rdar://170360205
        # with self.assertRaisesRegex(ValidationError, 'must have a "requires-sdk" '
        #                             'clause'):
        #     AllowList.from_dict({'staging': [
        #         {'classes': ['Foo']}
        #     ]})

    def test_wildcard_version(self):
        self.assertEqual(_transform_wildcard_version('<=', '26.*'), ('<', '27.00'))
        self.assertEqual(_transform_wildcard_version('<=', '26.3*'), ('<', '26.40'))
        self.assertEqual(_transform_wildcard_version('<=', '26.9*'), ('<', '27.00'))
        self.assertEqual(_transform_wildcard_version('<=', '26.0*'), ('<', '26.10'))
        # Kind of nonsensical, but syntactically valid.
        self.assertEqual(_transform_wildcard_version('<=', '26.30*'), ('<', '26.31'))
        self.assertEqual(_transform_wildcard_version('<=', '*'), ('<', '99.99'))
        with self.assertRaises(ValueError):
            _transform_wildcard_version('<', '26.*')
            _transform_wildcard_version('==', '26.*')
            _transform_wildcard_version('!=', '26.*')

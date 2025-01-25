
import argparse
import json
import re
import os
import plistlib
import sqlite3
import typing

from . import objdump
from .tbd import TBD
from fnmatch import fnmatch

SCHEMA_VERSION = 1


class sdkdb:
    def __init__(self, db_file: typing.Optional[str] = None):
        self.con = sqlite3.connect(':memory:')
        if db_file is None:
            _initialize_db(self.con)
        else:
            cached = sqlite3.connect(db_file)
            cur = cached.cursor()
            cur.execute('SELECT value FROM meta WHERE key = "version"')
            row = cur.fetchone()
            if row != (SCHEMA_VERSION,):
                print(f'{db_file} is an older schema version, building fresh')
                _initialize_db(self.con)
            else:
                cached.backup(self.con)
                cached.close()

    def save_as(self, db_file: str):
        with self.con:
            cur = self.con.cursor()
            cur.execute('CREATE INDEX IF NOT EXISTS symbol_names '
                        'ON symbol (name)')
            cur.execute('CREATE INDEX IF NOT EXISTS symbol_globs '
                        'ON symbol_glob (pattern)')
            cur.execute('CREATE INDEX IF NOT EXISTS selector_names '
                        'ON objc_selector (name)')
            cur.execute('PRAGMA optimize')
        persistent = sqlite3.connect(db_file)
        self.con.backup(persistent)

    def add_partial_sdkdb(self, sdkdb_file: str):
        doc = json.load(open(sdkdb_file))

        # There may be multiple RuntimeRoot entries for different architectures 
        #install_name = next((
        #    root.get('binaryInfo', {}).get('installName')
        #    for root in doc.get('RuntimeRoot')
        #), f'unknown ({sdkdb_file})')

        with self.con:
            for key in 'PublicSDKContentRoot', 'SDKContentRoot':
                root = doc[key]
                for ent in root:
                    for category in ent.get('categories', []):
                        class_name = f'{category["interface"]}({category["name"]})'
                        self._add_objc_interface(category, class_name, sdkdb_file)
                    for glob in ent.get('globals', []):
                        if glob.get('access', 'public') == 'public':
                            self._add_symbol(glob['name'], sdkdb_file)
                    for iface in ent.get('interfaces', []):
                        if iface.get('access', 'public') == 'public':
                            self._add_objc_interface(iface, iface['name'],
                                                     sdkdb_file)
                            self._add_objc_class(iface['name'], sdkdb_file)
                    for proto in ent.get('protocols', []):
                        if proto.get('access', 'public') == 'public':
                            self._add_objc_interface(proto, proto['name'],
                                                     sdkdb_file)

    _method_to_selector_re = re.compile(r'[-+]\[(?P<class>\S+) '
                                        r'(?P<selector>[^\]]+)\]')

    def add_objdump(self, report: objdump.report, include_locals=False):
        filename = report.filename
        #if report.objc_class_list:
        #    for cls in report.objc_class_list.entries:
        #        if not cls.table:
        #            continue
        #        class_name = cls.table['data', 'name']
        #        assert class_name, 'objdump class metadata must contain a name'
        #        self._add_objc_class(class_name, filename)
        #        if cls.metaclass:
        #            self._add_symbol(f'_OBJC_METACLASS_$_{class_name}',
        #                             filename)
        #        for method in cls.table.getall('data', 'baseMethods', 'imp'):
        #            method = method
        #            m = self._method_to_selector_re.match(method)
        #            if m:
        #                self._add_objc_selector(m.group('selector'),
        #                                        class_name, filename)
        #            else:
        #                print(f'unmatched method name: {method}')

        #if report.objc_category_list:
        #    for cat in report.objc_category_list.entries:
        #        for key in ('instanceMethods', 'classMethods'):
        #            for method in cat.table.getall(key, 'imp'):
        #                method = method
        #                m = self._method_to_selector_re.match(method)
        #                if m:
        #                    self._add_objc_selector(m.group('selector'),
        #                                            m.group('class'), filename)
        #                else:
        #                    print(f'unmatched method name: {method}')

        # TODO: does this duplicate the objc metadata parsing above?
        if report.symbol_table:
            for symbol in report.symbol_table.entries:
                if not symbol.segment.startswith('*'):
                    m = self._method_to_selector_re.match(symbol.name)
                    # When include_locals=False, only track global exported
                    # symbols and method implementations.
                    if include_locals or m or 'g' in symbol.flags:
                        if m:
                            self._add_objc_selector(m.group('selector'),
                                                    m.group('class'), filename)
                        else:
                            self._add_symbol(symbol.name, filename)

        self.con.commit()

    def add_tbd(self, tbd: TBD):
        if not hasattr(tbd, 'exports'):
            return
        for export in tbd.exports:
            name = f'{tbd.install_name}{export["targets"]}'
            for symbol in export.get('symbols', ()):
                self._add_symbol(symbol, name)
            for class_ in export.get('objc-classes', ()):
                self._add_objc_class(class_, name)

    # TODO: remove?
    def add_allowlist(self, filename: str):
        allowlist = plistlib.load(open(filename, 'rb'))
        for sym in allowlist.get('symbols', ()):
            # allowlist format omits the platform prefix (_)
            if '*' in sym:
                self._add_symbol_glob(f'_{sym}', filename)
            else:
                self._add_symbol(f'_{sym}', filename)
        for cls in allowlist.get('classes', ()):
            self._add_objc_class(cls, filename)
        for sel in allowlist.get('selectors', ()):
            self._add_objc_selector(sel, None, filename)

    def symbol(self, name: str):
        cur = self.con.cursor()
        cur.execute('SELECT * FROM symbol WHERE name = ? UNION '
                    'SELECT * FROM symbol_glob WHERE ? GLOB pattern '
                    'LIMIT 1', (name, name))
        row = cur.fetchone()
        if row:
            return dict(name=row[0], file=row[1])

    def objc_class(self, name: str):
        cur = self.con.cursor()
        cur.execute('SELECT * FROM objc_class WHERE name = ?', (name,))
        row = cur.fetchone()
        if row:
            return dict(name=row[0], file=row[1])

    def objc_selector(self, selector: str):
        cur = self.con.cursor()
        cur.execute('SELECT * FROM objc_selector WHERE name = ?', (selector,))
        row = cur.fetchone()
        if row:
            return dict(name=row[0], class_name=row[1], file=row[2])

    def stats(self):
        cur = self.con.cursor()
        cur.execute('SELECT (SELECT COUNT(name) FROM symbol) AS symbols, '
                    '(SELECT COUNT(name) FROM objc_class) AS classes, '
                    '(SELECT COUNT(name) FROM objc_selector) AS selectors')
        row = cur.fetchone()
        return row

    def _add_objc_interface(self, ent, class_name, install_name):
        for key in 'instanceMethods', 'classMethods':
            for method in ent.get(key, []):
                if method.get('access', 'public') == 'public':
                    self._add_objc_selector(method['name'], class_name,
                                            install_name)
        for prop in ent.get('properties', []):
            if prop.get('access', 'public') == 'public':
                self._add_objc_selector(prop['getter'], class_name,
                                        install_name)
                if 'readonly' not in prop.get('attr', {}):
                    self._add_objc_selector(prop['setter'], class_name,
                                            install_name)
        for ivar in ent.get('ivars', []):
            if ivar.get('access', 'public') == 'public':
                self._add_symbol(
                    f'_OBJC_IVAR_$_{class_name}.{ivar["name"]}', install_name)

    def _add_symbol(self, name, file):
        cur = self.con.cursor()
        cur.execute('INSERT INTO symbol VALUES (?, ?)', (name, file))

    def _add_symbol_glob(self, pattern, file):
        cur = self.con.cursor()
        cur.execute('INSERT INTO symbol_glob VALUES (?, ?)', (pattern, file))

    def _add_objc_class(self, name, file):
        cur = self.con.cursor()
        cur.execute('INSERT INTO objc_class VALUES (?, ?)',
                    (name, file))
        cur.execute('INSERT INTO symbol VALUES (?, ?)',
                    (f'_OBJC_CLASS_$_{name}', file))
        cur.execute('INSERT INTO symbol VALUES (?, ?)',
                    (f'_OBJC_METACLASS_$_{name}', file))

    def _add_objc_selector(self, name, class_name, file):
        cur = self.con.cursor()
        cur.execute('INSERT INTO objc_selector VALUES (?, ?, ?)',
                    (name, class_name, file))


def _initialize_db(con: sqlite3.Connection):
    cur = con.cursor()
    cur.execute('CREATE TABLE symbol(name, file)')
    cur.execute('CREATE TABLE symbol_glob(pattern, file)')
    cur.execute('CREATE TABLE objc_class(name, file)')
    cur.execute('CREATE TABLE objc_selector(name, class, file)')
    cur.execute('CREATE TABLE meta(key, value)')
    # TODO: maybe include target tuple here?
    cur.execute('INSERT INTO meta VALUES '
                '("version", ?), ("partial_hash", NULL)',
                (SCHEMA_VERSION,))
    con.commit()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-o', '--output', dest='db_file')
    parser.add_argument('--allowlist', nargs='+')
    parser.add_argument('input_files', nargs='+')
    args = parser.parse_args()

    s = sdkdb()
    for arg in args.input_files:
        m = re.match(r'(?P<name>[^(]+)(\((?P<patterns>.+)\))?', arg)
        if not m:
            raise ValueError(f'Unrecognized file "{arg}"')
        file = m.group('name')
        ext = os.path.splitext(file)[1]
        if ext == '.sdkdb':
            s.add_partial_sdkdb(file)
        elif ext == '.tbd':
            patterns = m.group('patterns')
            if patterns:
                patterns = patterns.split(', ')
                
            for tbd in TBD.load_from(m.group('name') if m else file):
                if patterns and all(not fnmatch(tbd.install_name, pat)
                                    for pat in patterns):
                    continue
                s.add_tbd(tbd)
        else:
            raise ValueError(f'Unknown input file type "{ext}"')
            

    symbols, classes, selectors = s.stats()
    print(f'{symbols=} {classes=} {selectors=}')

    if args.db_file:
        s.save_as(args.db_file)
        print(f'Saved to "{args.db_file}"')


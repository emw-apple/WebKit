import argparse
import re
import io
import os
import subprocess
import sys

import typing
from typing import NamedTuple, Union, List, GenericAlias, Optional, Generic, TypeVar
from dataclasses import dataclass

from .grammar import Parseable

TAB_SIZE = 4

# class table(NamedTuple):
#     values : dict
#
#     _line_re = re.compile(rb'(?P<indent> +)(?P<key>[a-zA-Z]) +(?P<value>.+)')
#
#     @classmethod
#     def load(cls, fd, indent=4):
#         result = {}
#         cursor = result
#
#         while line := fd.readline():
#             if not m := _line_re.match(line):
#
#                 return result
#             matched_indent = len(m.group('indent'))
#             if matched_indent == indent:
#                 k, v = match.group('key', 'value')
#                 cursor[k] = v
#             elif matched_indent == indent + TAB_SIZE:
#                 k, _ = result.popitem()
#                 result[k] = {}
#                 cursor = result[k]
#                 indent += TAB_SIZE

@dataclass
class objc_table_line(Parseable):
    indent: str
    key: str
    address: str
    value: str

    pattern = re.compile(r'(?P<indent> {4,})(?P<key>[a-zA-Z]+(\[\d+\])?) +'
                         r'(?P<address>0x[0-9a-f]+ (\(0x[0-9a-f]+\) )?)?'
                         r'(?P<value>.+)')


@dataclass
class objc_table(Parseable):
    lines : List[objc_table_line]

    def __getitem__(self, key_path: tuple[str]):
        i = 0
        j = 0
        while i < len(self.lines) and j < len(key_path):
            key = key_path[j]
            if self.lines[i].indent == ' ' * TAB_SIZE * (j+1) and \
                    self.lines[i].key == key:
                j += 1
            else:
                i += 1
        if j == len(key_path):
            return self.lines[i].value
        raise KeyError(key_path[j])

    def getall(self, *key_path: tuple[str]):
        i = 0
        j = 0
        result = []
        while i < len(self.lines) and j < len(key_path):
            key = key_path[j]
            if self.lines[i].indent == ' ' * TAB_SIZE * (j+1) and \
                    self.lines[i].key == key:
                if j+1 == len(key_path):
                    result.append(self.lines[i].value)
                    i += 1
                else:
                    j += 1
            else:
                i += 1
        return result


@dataclass
class objc_metaclass(Parseable):
    header: str
    data: List[objc_table_line]

    pattern = re.compile(r'(?P<header>Meta Class)')


@dataclass
class objc_class(Parseable):
    address: str
    symbol: str
    table: Optional[objc_table]
    metaclass: Optional[objc_metaclass]

    pattern = re.compile(r'(?P<address>[0-9a-f]+ 0x[0-9a-f]+)'
                         r'( (?P<symbol>_OBJC_CLASS_\S+))?')

@dataclass
class objc_selref(Parseable):
    address: str
    name: str

    pattern = re.compile(r'    +(?P<address>0x[0-9a-f]+) (?P<name>[\w:.]+)')

@dataclass
class objc_symbol(Parseable):
    offset: str
    address: str
    name: str

    pattern = re.compile(r'(?P<offset>[0-9a-z]+) (?P<address>0x[0-9a-z]+) '
        r'(?P<name>.+)')


@dataclass
class objc_section(Parseable):
    segment: str
    section: str
    entries: Union[List[objc_class], List[objc_symbol], List[objc_selref], objc_table]

    pattern = re.compile(
        r'Contents of \((?P<segment>\w+),(?P<section>\w+)\) section')

@dataclass
class binding_header(Parseable):
    column1: str
    column2: str
    column3: str
    column4: str
    column5: str
    column6: str
    column7: str

    pattern = re.compile(r'(?P<column1>\w+)\s+(?P<column2>\w+)\s+'
        r'(?P<column3>\w+)\s+(?P<column4>\w+)\s+(?P<column5>\w+)\s+'
        r'(?P<column6>\w+)\s+(?P<column7>\w+)')


@dataclass
class binding(Parseable):
    segment: str
    section: str
    address: str
    typ: str
    addend: str
    dylib: str
    symbol: str
    attributes: str

    pattern = re.compile(r'(?P<segment>\w+)\s+'
        r'(?P<section>\w+)\s+'
        r'(?P<address>0x[0-9A-F]+)\s+'
        r'(?P<typ>\w+( \w+)*)\s+'
        r'(?P<addend>\d+)\s+'
        r'(?P<dylib>\S+)\s+'
        r'(?P<symbol>\S+)( \((?P<attributes>.+)\))?')

@dataclass
class bind_table(Parseable):
    header: str
    columns: binding_header
    entries: List[binding]

    pattern = re.compile(r'(?P<header>Bind table:)')

@dataclass
class export(Parseable):
    address: str
    symbol: str

    pattern = re.compile(r'(?P<address>0x[0-9A-F]+)\s+(?P<symbol>.+)')

@dataclass
class exports_trie(Parseable):
    header: str
    entries: List[export]

    pattern = re.compile(r'(?P<header>Exports trie:)')

@dataclass
class whitespace(Parseable):
    space: str
    pattern = re.compile(r'(?P<space>\s+)')

@dataclass
class report(Parseable):
    filename: str
    objc_metadata: Optional[List[objc_section]]
    space1: Optional[whitespace]
    exports: Optional[exports_trie]
    space2: Optional[whitespace]
    bind_table: Optional[bind_table]

    pattern = re.compile(r'(?P<filename>.+):')

    @property
    def objc_class_list(self):
        for entry in self.objc_metadata:
            if entry.section == '__objc_classlist':
                return entry

    @property
    def objc_category_list(self):
        for entry in self.objc_metadata:
            if entry.section == '__objc_catlist':
                return entry


# ---

@dataclass
class nested_table_entry:
    key: str
    value: Union[str, List['nested_table_entry']]



def nested_table_from_entries(entries: List[objc_table_line]):
    result = []
    cur_indent = TAB_SIZE
    cursor = [result]

    for table_line in objc_table.lines:
        line_indent = len(table_line.indent)
        indent_cmp = line_indent - cur_indent
        if indent_cmp > 0:
            # discard the "value" recorded for the last key (which is just
            # an address for the sub-data) and replace it with a new dict.
            sub_entries = []
            cursor[-1][-1].value = sub_entries
            cursor.append(sub_entries)
            cur_indent = line_indent
        elif indent_cmp < 0:
            for _ in range(line_indent, cur_indent, TAB_SIZE):
                cursor.pop()
            cur_indent = line_indent
        cursor[-1].append(nested_table_entry(
            key=table_line.key, value=table_line.value))
    return result


# ---

def load(binary_path, bindings=True, objc_metadata=True, exports=True):
    objdump = subprocess.run(('xcrun', 'llvm-objdump', '-m', binary_path,
                             *(('--bind',) if bindings else ()),
                             *(('--objc-meta-data',) if objc_metadata else ()),
                             *(('--exports-trie',) if exports else ())),
                            check=True, stdout=subprocess.PIPE,
                            text=True, errors='replace')
    return report.load(io.StringIO(objdump.stdout))

def main(argv=None):
    pass

if __name__ == '__main__':
    main()
#     from io import StringIO
#     fd = open(sys.argv[1], 'r')
#     # faster than passing the I/O stream because tell() requires seeking on the
#     # actual file descriptor
#     buf = StringIO(fd.read())
#     t = report.load(buf)
#     print(f'{t.filename}: {len(t.bind_table.entries)} bindings')
#     from pprint import pprint
#     pprint(t, width=200)
#     nt = nested_table.from_parsed(t)
#     pprint(nt.as_tuple(), width=200)

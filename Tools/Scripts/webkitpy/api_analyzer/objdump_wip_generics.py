import argparse
import re
import io
import os
import subprocess
import sys

import typing
from typing import NamedTuple, Union, List, GenericAlias, Optional, Generic, TypeVar
from dataclasses import dataclass

class ParseError(RuntimeError):
    def __init__(self, line, regexp, typ):
        self.line = line
        self.regexp = regexp
        self.typ = typ

    def __str__(self):
        line = self.line.replace('\n', '\\n')
        return f'while loading {self.typ}: "{line}" does not match expected ' \
            f'pattern "{self.regexp.pattern}"'

class Parseable:
    pattern = None

    @classmethod
    def load(cls, fd, _type_args=()):
        if cls.pattern:
            line = fd.readline()
            m = cls.pattern.match(line)
            if not m:
                raise ParseError(line, cls.pattern, cls)
            members = m.groupdict()
        else:
            members = {}
        type_args = iter(_type_args)

        for name, typ in cls.__annotations__.items():
            # use type() instead of isinstance() for faster lookup, since this
            # is a hot loop.
            if type(typ) == TypeVar:
                # python generics aren't "instantiated" in any meaningful sense,
                # so pass in a list of (ordered) type variables to use when
                # encountering a type variable in the dataclass.
                typ = next(type_args)

            # token fields -- str
            if typ == str:
                assert cls.pattern, f'{cls} must define a regexp pattern ' \
                    'to match token types'
                assert name in members, f'{name} not matched by pattern ' \
                    'in {cls}'
            # one or more expressions -- [T]
            elif type(typ) == list:
                subtype, = typ
                if type(subtype) == TypeVar:
                    subtype = next(type_args)
                # load one instance, then accumulate any others
                result = [subtype.load(fd, typing.get_args(typ))]
                type_args = typing.get_args(subtype) if \
                    hasattr(subtype, '__origin__') else ()
                while item := subtype.try_load(fd, type_args):
                    result.append(item)
                members[name] = result
            else:
                # optional expression -- Optional[T]
                if hasattr(typ, '__origin__') and \
                        typing.get_origin(typ) == Union:
                    typ, none_type = typing.get_args(typ)
                    assert none_type == type(None), \
                        'only optional types implemented, not generic unions'
                    loader = typ.try_load
                # plain expression -- T
                else:
                    loader = typ.load

                type_args = typing.get_args(typ) if \
                    hasattr(typ, '__origin__') else ()
                members[name] = loader(fd, type_args)

        return cls(**members)

    @classmethod
    def try_load(cls, fd, _type_args=()):
        start = fd.tell()
        try:
            return cls.load(fd, _type_args)
        except ParseError:
            fd.seek(start)
            return


# ---

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
    lines : [objc_table_line]

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
    data: [objc_table_line]

    pattern = re.compile(r'(?P<header>Meta Class)')


@dataclass
class objc_class(Parseable):
    address: str
    symbol: str
    table: Optional[objc_table]
    metaclass: Optional[objc_metaclass]

    pattern = re.compile(r'(?P<address>[0-9a-f]+ 0x[0-9a-f]+)'
                         r'( (?P<symbol>_OBJC_CLASS_\S+))?')

T = TypeVar('T')

@dataclass
class objc_classlist(Parseable):
    segment: str
    entries: [objc_class]

    pattern = re.compile(
        r'Contents of \((?P<segment>\w+),__objc_classlist\) section')


@dataclass
class objc_superrefs(Parseable):
    segment: str
    entries: [objc_symbol]

    pattern = re.compile(
        r'Contents of \((?P<segment>\w+),__objc_superrefs\) section')


@dataclass
class objc_catlist(Parseable):
    segment: str
    entries: [objc_class]

    pattern = re.compile(
        r'Contents of \((?P<segment>\w+),__objc_catlist\) section')


@dataclass
class objc_protolist(Parseable):
    segment: str
    entries: [objc_class]

    pattern = re.compile(
        r'Contents of \((?P<segment>\w+),__objc_protolist\) section')


@dataclass
class objc_selrefs(Parseable):
    segment: str
    entries: [objc_selref]

    pattern = re.compile(
        r'Contents of \((?P<segment>\w+),__objc_selrefs\) section')


@dataclass
class objc_imageinfo(Parseable):
    segment: str
    table: objc_table

    pattern = re.compile(
        r'Contents of \((?P<segment>\w+),__objc_imageinfo\) section')


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
    entries: [binding]

    pattern = re.compile(r'(?P<header>Bind table:)')

@dataclass
class objc_metadata(Parseable):
    class_list: objc_classlist
    super_refs: objc_superrefs
    category_list : objc_catlist
    proto_list : objc_protolist
    selector_list : objc_selrefs
    image_info: objc_imageinfo


@dataclass
class export(Parseable):
    address: str
    symbol: str

    pattern = re.compile(r'(?P<address>0x[0-9A-F]+)\s+(?P<symbol>.+)')

@dataclass
class exports_trie(Parseable):
    header: str
    entries: [export]

    pattern = re.compile(r'(?P<header>Exports trie:)')

@dataclass
class whitespace(Parseable):
    space: str
    pattern = re.compile(r'(?P<space>\s+)')

@dataclass
class report(Parseable):
    filename: str
    objc_metadata: objc_metadata
    space1: Optional[whitespace]
    exports: exports_trie
    space2: Optional[whitespace]
    bind_table: bind_table

    pattern = re.compile(r'(?P<filename>.+):')


# ---

@dataclass
class nested_table_entry:
    key: str
    value: Union[str, List['nested_table_entry']]



def nested_table_from_entries(entries: [objc_table_line]):
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
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', help='file to test')
    parser.add_argument('-i', '--sdkdb-file')
    parser.add_argument('-l', '--extra-binaries', action='append')
    args = parser.parse_args(argv)

    from .sdkdb import sdkdb
    db = sdkdb(args.sdkdb_file)

    import pdb; pdb.set_trace()
    for dylib in args.extra_binaries:
        interface = load(dylib, bindings=False)
        db.add_objdump(interface)

    report = load(args.binaries)
    db.add_objdump(report)

    for binding in report.bind_table.entries:
        if binding.dylib == 'this-image':
            continue
        if not db.symbol(binding.symbol):
            print(f'Missing: {binding}')

    import pdb; pdb.set_trace()
    print(db)

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

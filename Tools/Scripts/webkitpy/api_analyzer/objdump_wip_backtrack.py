import argparse
import io
import re
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
        line = self.line.decode().replace('\n', '\\n')
        return f'while loading {self.typ}: "{line}" does not match expected ' \
            f'pattern "{self.regexp.pattern.decode()}"'

class parser_iterator:
    def __init__(self, fd: io.IOBase):
        self.fd = fd
        self._next_line = None

    def backtrack(self):
        self._next_line = self._cur_line

    def readline(self):
        return next(self)

    def __next__(self):
        if self._next_line:
            self._cur_line = self._next_line
            self._next_line = None
        else:
            self._cur_line = self.fd.readline()
        if not self._cur_line:
            raise StopIteration
        return self._cur_line


class Parseable:
    pattern = None
    @classmethod
    def load(cls, fd: parser_iterator, _type_args=(), _failable=False):
        if cls.pattern:
            line = fd.readline()
            m = cls.pattern.match(line)
            if not m:
                if _failable:
                    fd.backtrack()
                    return
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

            # token fields -- bytes
            if typ == bytes:
                assert cls.pattern, f'{cls} must define a regexp pattern ' \
                    'to match token types'
                assert name in members, f'{name} not matched by pattern ' \
                    'in {cls}'
            # one or more expressions -- [T]
            elif type(typ) == list:
                subtype, = typ
                if type(subtype) == TypeVar:
                    subtype = next(type_args)
#                 # load one instance, then accumulate any others
#                 result = [subtype.load(fd, typing.get_args(typ))]
                result = []
                type_args = typing.get_args(subtype) if \
                    hasattr(subtype, '__origin__') else ()
                while item := subtype.load(fd, type_args, _failable=True):
                    result.append(item)
                members[name] = result
            else:
                # optional expression -- Optional[T]
                if hasattr(typ, '__origin__') and \
                        typing.get_origin(typ) == Union:
                    typ, none_type = typing.get_args(typ)
                    assert none_type == type(None), \
                        'only optional types implemented, not generic unions'
                    failable = True
                else:
                    failable = False
                # plain expression -- T

                type_args = typing.get_args(typ) if \
                    hasattr(typ, '__origin__') else ()
                members[name] = typ.load(fd, type_args, failable)

        return cls(**members)

#     @classmethod
#     def try_load(cls, fd, _type_args=()):
# #         start = fd.tell()
#         try:
#             return cls.load(fd, _type_args)
#         except ParseError:
#             fd.backtrack()
# #             fd.seek(start)
#             return

    def as_tuple(self):
        def generator(data):
            for name, typ in data.__annotations__.items():
                if typ == bytes:
                    yield getattr(data, name)
                elif isinstance(typ, list):
                    yield from map(lambda x: x.as_tuple(), getattr(data, name))
                else:
                    yield getattr(data, name).as_tuple()
        return tuple(generator(self))


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
    indent: bytes
    key: bytes
    value: bytes

    pattern = re.compile(rb'(?P<indent> {4,})(?P<key>[a-zA-Z]+(\[\d+\])?) +(?P<value>.+)')


@dataclass
class objc_table(Parseable):
    lines : [objc_table_line]

@dataclass
class objc_metaclass(Parseable):
    header: bytes
    data: [objc_table_line]

    pattern = re.compile(rb'(?P<header>Meta Class)')


@dataclass
class objc_class(Parseable):
    address: bytes
    symbol: bytes
    data: [objc_table_line]
    metaclass: Optional[objc_metaclass]

    pattern = re.compile(rb'(?P<address>[0-9a-f]+ 0x[0-9a-f]+)'
                         rb'( (?P<symbol>_OBJC_CLASS_\S+))?')



T = TypeVar('T')

@dataclass
class objdump_section(Parseable, Generic[T]):
    segment: bytes
    section: bytes
    list: [T]

    pattern = re.compile(
        rb'Contents of \((?P<segment>\w+),(?P<section>\w+)\) section')

@dataclass
class objc_selref(Parseable):
    address: bytes
    name: bytes

    pattern = re.compile(rb'    +(?P<address>0x[0-9a-f]+) (?P<name>[\w:.]+)')

@dataclass
class objc_symbol(Parseable):
    offset: bytes
    address: bytes
    name: bytes

    pattern = re.compile(rb'(?P<offset>[0-9a-z]+) (?P<address>0x[0-9a-z]+) '
        rb'(?P<name>.+)')


@dataclass
class binding_header(Parseable):
    column1: bytes
    column2: bytes
    column3: bytes
    column4: bytes
    column5: bytes
    column6: bytes
    column7: bytes

    pattern = re.compile(rb'(?P<column1>\w+)\s+(?P<column2>\w+)\s+'
        rb'(?P<column3>\w+)\s+(?P<column4>\w+)\s+(?P<column5>\w+)\s+'
        rb'(?P<column6>\w+)\s+(?P<column7>\w+)')


@dataclass
class binding(Parseable):
    segment: bytes
    section: bytes
    address: bytes
    typ: bytes
    addend: bytes
    dylib: bytes
    symbol: bytes

    pattern = re.compile(rb'(?P<segment>\w+)\s+'
        rb'(?P<section>\w+)\s+'
        rb'(?P<address>0x[0-9A-F]+)\s+'
        rb'(?P<typ>\w+( \w+)*)\s+'
        rb'(?P<addend>\d+)\s+'
        rb'(?P<dylib>\S+)\s+'
        rb'(?P<symbol>.+)')

@dataclass
class bind_table(Parseable):
    header: bytes
    columns: binding_header
    entries: [binding]

    pattern = re.compile(rb'(?P<header>Bind table:)')

@dataclass
class objc_metadata(Parseable):
    class_list: objdump_section[objc_class]
    super_refs: objdump_section[objc_symbol]
    category_list : objdump_section[objc_class]
    proto_list : objdump_section[objc_class]
    selector_list : objdump_section[objc_selref]
    image_info: objdump_section[objc_table]


@dataclass
class whitespace(Parseable):
    space: bytes
    pattern = re.compile(rb'(?P<space>\s+)')

@dataclass
class report(Parseable):
    filename: bytes
    objc_metadata: objc_metadata
    space: Optional[whitespace]
    bind_table: bind_table

    pattern = re.compile(rb'(?P<filename>.+):')


# ---

@dataclass
class nested_table_entry:
    key: bytes
    value: Union[str, List['nested_table_entry']]

    def as_tuple(self):
        return (self.key, tuple(ent.as_tuple() for ent in self.value)
                    if isinstance(self.value, list) else self.value)


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



def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', help='file to test')
    parser.add_argument('-i', '--sdkdb')
    args = parser.parse_args(argv)

    objdump = subprocess.Popen(('xcrun', 'llvm-objdump', '-m', '--bind',
                               '--objc-meta-data', args.binary),
                               stdout=subprocess.PIPE)
    t = report.load(parser_iterator(objdump.stdout))
    objdump.wait()

    print(f'{t.filename.decode()}: {len(t.bind_table.entries)} bindings')

if __name__ == '__main__':
    main()
#     from io import BytesIO
#     fd = open(sys.argv[1], 'rb')
#     # faster than passing the I/O stream because tell() requires seeking on the
#     # actual file descriptor
#     buf = BytesIO(fd.read())
#     t = report.load(buf)
#     print(f'{t.filename.decode()}: {len(t.bind_table.entries)} bindings')
#     from pprint import pprint
#     pprint(t, width=200)
#     nt = nested_table.from_parsed(t)
#     pprint(nt.as_tuple(), width=200)

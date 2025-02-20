import typing


class ParseError(RuntimeError):
    def __init__(self, line, regexp, typ):
        self.line = line
        self.regexp = regexp
        self.typ = typ

    def __str__(self):
        line = self.line.replace('\n', '\\n')
        return f'while loading {self.typ}: "{line}" does not match expected ' \
            f'pattern "{self.regexp.pattern}"'


def _load_production(typ, fd, failable=False):
    # optionals
    if typing.get_origin(typ) is typing.Union:
        for subtype in typing.get_args(typ):
            result = _load_production(subtype, fd, failable=True)
            if result or subtype is type(None):
                return result
        if not failable:
            raise TypeError(f'no productions matched {typ}')
    # one or more productions -- List[T]
    elif typing.get_origin(typ) is list:
        subtype, = typing.get_args(typ)
        # load one instance, then accumulate any others
        result = [_load_production(subtype, fd, failable)]
        if result == [None]:
            # bail out when failable=True and the first production failed
            return
        while item := _load_production(subtype, fd, failable=True):
            result.append(item)
        return result
    # base cases
    elif typ is type(None):
        return
    else:
        if failable:
            start = fd.tell()
        if typ.pattern:
            line = fd.readline()
            m = typ.pattern.match(line)
            if not m:
                if failable:
                    fd.seek(start)
                    return
                else:
                    raise ParseError(line, typ.pattern, typ)
            members = m.groupdict()
        else:
            members = {}

        for name, typ_ in typ.__annotations__.items():
            # token fields -- str
            if typ_ == str:
                assert typ.pattern, f'{typ_} must define a regexp pattern ' \
                    'to match token types'
                assert name in members, f'{name} not matched by pattern ' \
                    'in {typ_}'
            else:
                members[name] = _load_production(typ_, fd)
        return typ(**members)


class Parseable:
    pattern: typing.Optional[typing.Pattern[str]] = None

#     @classmethod
#     def load(cls, fd):
#         if cls.pattern:
#             line = fd.readline()
#             m = cls.pattern.match(line)
#             if not m:
#                 raise ParseError(line, cls.pattern, cls)
#             members = m.groupdict()
#         else:
#             members = {}
#
#         for name, typ in cls.__annotations__.items():
#             # token fields -- str
#             if typ == str:
#                 assert cls.pattern, f'{cls} must define a regexp pattern ' \
#                     'to match token types'
#                 assert name in members, f'{name} not matched by pattern ' \
#                     'in {cls}'
#             else:
#                 members[name] = _load_production(typ, fd)
#         return cls(**members)
#
#     @classmethod
#     def try_load(cls, fd):
#         start = fd.tell()
#         try:
#             return cls.load(fd)
#         except ParseError:
#             fd.seek(start)
#             return

import argparse
import cProfile
import re

from .sdkdb import sdkdb
from . import objdump

from webkitpy.port.config import apple_additions

ALLOWED_SYMBOLS = set((
    '_OBJC_METACLASS_$_NSObject',
    '_OBJC_EHTYPE_$_NSException',
    # Foundation constant classes are part of ABI but do not exist in headers.
    '_OBJC_CLASS_$_NSConstantArray',
    '_OBJC_CLASS_$_NSConstantDictionary',
    '_OBJC_CLASS_$_NSConstantDoubleNumber',
    '_OBJC_CLASS_$_NSConstantIntegerNumber',
))

ALLOWED_DYLIBS = set((
    # ICU is not part of the public SDK, but it is OSS.
    #'libicucore',
    # These language runtimes are explicitly treated as public by tapi.
    #'libswiftCore',
    #'libobjc',
    # Ignore bindings to our own symbols.
    'this-image',
))


class Reporter:
    def __init__(self):
        self.n_issues = 0
        self.seen = set()

    def missing_selector(self, name: str, ignore=False):
        self.seen.add(name)
        if not ignore:
            print(f'selector:', name, sep='\t')
            self.n_issues += 1

    def missing_class(self, dylib: str, name: str, ignore=False):
        self.seen.add(name)
        if not ignore:
            print(f'class:', dylib, name, sep='\t')
            self.n_issues += 1

    def missing_symbol(self, dylib: str, name: str, ignore=False):
        self.seen.add(name)
        if not ignore:
            print(f'symbol:', dylib, name, sep='\t')
            self.n_issues += 1

    def finished(self):
        print(f'{self.n_issues} issue(s).')


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', help='file to test')
    parser.add_argument('-i', '--sdkdb-file')
    parser.add_argument('-l', '--extra-binaries', action='append')
    parser.add_argument('-s', '--extra-sdkdb', action='append')
    parser.add_argument('-a', '--arch-name', required=True)
    parser.add_argument('--profile', help='write cProfile data to path')
    if apple_additions():
        apple_additions().api_analyzer.add_arguments(parser)
    args = parser.parse_args(argv)

    if args.profile:
        prof = cProfile.Profile()
        prof.enable()

    db = sdkdb(args.sdkdb_file)

    # FIXME: Load in parallel when there are more than a few extra binaries.
    for dylib in args.extra_binaries or ():
        interface = objdump.load(dylib, arch=args.arch_name, bindings=False)
        db.add_objdump(interface)

    for name in args.extra_sdkdb or ():
        db.add_partial_sdkdb(name, spi=True, abi=True)

    report = objdump.load(args.binary, arch=args.arch_name)
    if apple_additions():
        reporter = apple_additions().api_analyzer.configure_reporter(args, db)
    else:
        reporter = Reporter()

    db.add_objdump(report, include_locals=True)

    for section in report.objc_metadata:
        if not section.section == '__objc_selrefs':
            continue
        for selref in sorted(section.entries, key=lambda s: s.name):
            if not db.objc_selector(selref.name):
                reporter.missing_selector(selref.name)

    objc_class_re = re.compile(r'_OBJC_CLASS_\$_(?P<name>.+)')

    for binding in sorted(report.bind_table.entries, key=lambda b: b.symbol):
        if binding.symbol in reporter.seen:
            continue  # for ___CFConstantStringClassReference and ___kCFBooleanTrue only afaict
        ignored = binding.dylib in ALLOWED_DYLIBS or \
            binding.symbol in ALLOWED_SYMBOLS
        m = objc_class_re.match(binding.symbol)
        # TODO: does this imply we should only store symbol names?
        if m and not db.objc_class(m.group('name')) and not db.symbol(binding.symbol):
            reporter.missing_class(binding.dylib, m.group('name'), ignored)
        elif not db.symbol(binding.symbol):
            reporter.missing_symbol(binding.dylib, binding.symbol, ignored)

    reporter.finished()

    if args.profile:
        prof.disable()
        prof.dump_stats(args.profile)



if __name__ == '__main__':
    main()

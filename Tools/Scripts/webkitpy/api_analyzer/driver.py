import argparse
import cProfile

from .sdkdb import sdkdb
from . import objdump

ALLOWED_SYMBOLS = set((
    '_OBJC_METACLASS_$_NSObject',
))

def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', help='file to test')
    parser.add_argument('-i', '--sdkdb-file')
    parser.add_argument('-l', '--extra-binaries', action='append')
    parser.add_argument('--profile', help='write cProfile data to path')
    args = parser.parse_args(argv)

    if args.profile:
        prof = cProfile.Profile()
        prof.enable()

    db = sdkdb(args.sdkdb_file)

    for dylib in args.extra_binaries or ():
        interface = objdump.load(dylib, bindings=False)
        db.add_objdump(interface)

    report = objdump.load(args.binary)
#     from pprint import pp; pp(report, width=200)
    db.add_objdump(report)

    seen = set()
    for binding in report.bind_table.entries:
        if binding.dylib == 'this-image':
            continue
        if binding.symbol in ALLOWED_SYMBOLS:
            continue
        if binding.symbol in seen:
            continue
        if not db.symbol(binding.symbol):
            print(f'Missing: {binding}')
        seen.add(binding.symbol)

    if args.profile:
        prof.disable()
        prof.dump_stats(args.profile)

if __name__ == '__main__':
    main()

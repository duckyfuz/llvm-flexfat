"""Choose all-ones bits for the original undef after sanitizer instrumentation.

This is a legal materialization of undef, making the regression deterministic
across backends. It deliberately leaves the sanitizer's null operands alone.
"""
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
assert "[ undef," in source, "the original undefined PHI must remain"
source = source.replace("[ undef,", "[ inttoptr (i64 -1 to ptr),")
pathlib.Path(sys.argv[2]).write_text(source)

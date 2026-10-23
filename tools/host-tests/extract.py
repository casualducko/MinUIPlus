#!/usr/bin/env python3
"""Print part of a source file: from the first occurrence of START up to (not
including) the first occurrence of END after it. Used by run.sh so the tests
compile the real code from workspace/, never a copy that could drift."""
import sys

if len(sys.argv) != 4:
	sys.exit("usage: extract.py FILE START END")
path, start, end = sys.argv[1:]
s = open(path).read()
a = s.find(start)
if a < 0:
	sys.exit("extract.py: start marker not found in %s: %r" % (path, start))
b = s.find(end, a + len(start))
if b < 0:
	sys.exit("extract.py: end marker not found in %s: %r" % (path, end))
sys.stdout.write(s[a:b])

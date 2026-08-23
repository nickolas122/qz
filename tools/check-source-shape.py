#!/usr/bin/env python3
"""Catch constructs that a bulk deletion cut in half but left syntactically plausible.

The strip deletes features by script, and the scripts are line-oriented while several
C++/Qt constructs wrap onto a second line. Taking one line of a two-line construct
leaves a fragment that is often still brace- and paren-balanced, so it survives every
structural check and fails much later, in a compiler or moc message that names an
innocent line.

Each check here is deliberately narrow and shaped like the damage rather than like the
language: it knows what a whole Q_PROPERTY looks like, and complains when it sees half
of one.

Exit code 0 if clean, 1 otherwise.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"


def logical_lines(text):
    """Yield (line_number, joined_text) with paren continuations folded together."""
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        start = i
        buf = lines[i]
        masked = re.sub(r'"(?:[^"\\]|\\.)*"', '""', buf)
        depth = masked.count("(") - masked.count(")")
        while depth > 0 and i + 1 < len(lines):
            i += 1
            buf += " " + lines[i].strip()
            masked = re.sub(r'"(?:[^"\\]|\\.)*"', '""', lines[i])
            depth += masked.count("(") - masked.count(")")
        yield start + 1, buf
        i += 1


def check_q_properties(path):
    """Every Q_PROPERTY needs a type, a name and a READ (or MEMBER)."""
    problems = []
    for lineno, text in logical_lines(path.read_text(encoding="utf-8", errors="replace")):
        stripped = text.strip()
        if not stripped.startswith("Q_PROPERTY("):
            continue
        inner = stripped[len("Q_PROPERTY("):].rsplit(")", 1)[0].strip()
        if "READ" not in inner and "MEMBER" not in inner:
            problems.append(
                f"{path.relative_to(ROOT)}:{lineno}: Q_PROPERTY with no READ/MEMBER - "
                f"looks like half a declaration: {inner[:60] or '(empty)'}"
            )
        elif len(inner.split()) < 3:
            problems.append(
                f"{path.relative_to(ROOT)}:{lineno}: Q_PROPERTY is too short to be whole: {inner[:60]}"
            )
    return problems


def main():
    problems = []
    headers = sorted(SRC.glob("*.h")) + sorted(SRC.glob("**/*.h"))
    seen = set()
    for h in headers:
        if h in seen:
            continue
        seen.add(h)
        problems.extend(check_q_properties(h))

    print(f"scanned {len(seen)} headers for half-deleted constructs")
    if problems:
        print(f"\nFAIL: {len(problems)} problem(s)")
        for p in problems:
            print(f"  - {p}")
        return 1
    print("\nOK: no half-deleted constructs found")
    return 0


if __name__ == "__main__":
    sys.exit(main())

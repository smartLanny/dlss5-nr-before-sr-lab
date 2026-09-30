"""Small source reader for checks against the maintained Core source."""

from pathlib import Path


def source(root: Path, path: str) -> str:
    return (root / path).read_text(encoding="utf-8-sig")


def definition(text: str, signature: str) -> str:
    """Return a unique brace-balanced C++/HLSL definition.

    Braces inside ordinary strings and comments are ignored. This deliberately
    fails if a source refactor makes the selected function ambiguous.
    """
    if text.count(signature) != 1:
        raise ValueError(f"Unique definition required: {signature}")
    start = text.index(signature)
    opening = text.index("{", start)
    level, i, state, quote = 0, opening, "code", ""
    while i < len(text):
        char, pair = text[i], text[i:i + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if pair == "*/":
                state = "code"
                i += 1
        elif state == "quote":
            if char == "\\":
                i += 1
            elif char == quote:
                state = "code"
        elif pair == "//":
            state = "line"
            i += 1
        elif pair == "/*":
            state = "block"
            i += 1
        elif char in ('"', "'"):
            state, quote = "quote", char
        elif char == "{":
            level += 1
        elif char == "}":
            level -= 1
            if level == 0:
                return text[start:i + 1]
        i += 1
    raise ValueError(f"Unclosed definition: {signature}")

"""Small, fail-closed extractors for production C++ validation fixtures."""


def block(text: str, signature: str) -> tuple[int, int]:
    """Return a unique brace-delimited definition, excluding quoted braces."""
    if text.count(signature) != 1:
        raise ValueError(f"Non-unique production definition: {signature}")
    start = text.index(signature)
    index = text.index("{", start)
    depth = 0
    state = "code"
    while index < len(text):
        char = text[index]
        pair = text[index:index + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if pair == "*/":
                state = "code"
                index += 1
        elif state in ('"', "'"):
            if char == "\\":
                index += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            index += 1
        elif pair == "/*":
            state = "comment"
            index += 1
        elif pair == 'R"':
            raise ValueError(f"Raw string in production definition: {signature}")
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return start, index + 1
        index += 1
    raise ValueError(f"Unterminated production definition: {signature}")

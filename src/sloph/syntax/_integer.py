def parse_decimal(text: str) -> int:
    negative = text.startswith("-")
    value = 0
    for digit in text[1:] if negative else text:
        value = value * 10 + (ord(digit) - ord("0"))
    return -value if negative else value


def format_decimal(value: int) -> str:
    if value == 0:
        return "0"
    negative = value < 0
    remaining = -value if negative else value
    chunks: list[int] = []
    while remaining:
        remaining, chunk = divmod(remaining, 1_000_000_000)
        chunks.append(chunk)
    rendered = str(chunks.pop()) + "".join(f"{chunk:09d}" for chunk in reversed(chunks))
    return "-" + rendered if negative else rendered


def decimal_digits(value: int) -> int:
    if value == 0:
        return 1
    remaining = -value if value < 0 else value
    count = 0
    while remaining >= 1_000_000_000:
        remaining //= 1_000_000_000
        count += 9
    while remaining:
        remaining //= 10
        count += 1
    return count

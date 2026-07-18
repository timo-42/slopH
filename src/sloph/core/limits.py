from dataclasses import dataclass, replace


@dataclass(frozen=True, slots=True)
class Limits:
    input_bytes: int = 1_048_576
    tokens: int = 100_000
    token_bytes: int = 4_096
    syntax_depth: int = 256
    ast_nodes: int = 100_000
    literal_digits: int = 4_096
    fuel: int = 1_000_000
    integer_bits: int = 16_384
    evaluation_depth: int = 4_096
    value_nodes: int = 100_000
    output_bytes: int = 1_048_576
    project_files: int = 10_000
    project_bytes: int = 268_435_456

    def with_fuel(self, fuel: int) -> "Limits":
        return replace(self, fuel=fuel)

    def __post_init__(self) -> None:
        for name in self.__dataclass_fields__:
            if getattr(self, name) <= 0:
                raise ValueError(f"limit {name} must be positive")

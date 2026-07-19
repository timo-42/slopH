# Compiler Representations and Transforms

SlopH gives each compiler representation a short codename. The codename is a
convenient name for command lines and discussion; the formal representation
name remains part of the specification.

| Codename | Formal representation | Role |
| --- | --- | --- |
| **Canopy** | parsed Source abstract syntax tree (AST) | Preserves authored, high-level Source structure after surface punctuation has been parsed. |
| **Crown** | transformed/desugared Standard Source AST (high-level IR, HIR) | Makes Source's internal `transform` and `desugar` steps explicit and produces the standard form used for semantic analysis. |
| **Heartwood** | elaborated, typed Core IR (functional mid-level IR, MIR) | Resolves names and types and expresses the program in canonical typed Core. It is functional rather than an SSA/TAC IR. |
| **Timber** | generated portable C11 (low-level handoff representation) | Makes runtime and native operations concrete, then delegates machine-dependent lowering to the host C compiler. |

Timber is deliberately portable C, not a custom machine-dependent LIR. The
host C compiler supplies its own lower IRs, instruction selection, register
allocation, assembly generation, and object emission.

## Transform Commands

Each explicit compiler transform is named from its input and output codenames:

```text
sloph canopy-to-crown
sloph crown-to-heartwood
sloph heartwood-to-timber
```

These are public commands of the authoritative C11 compiler. The names state
the boundary being inspected and are preferable to a generic `transform` or
`lower` command when exposing an individual stage. Project-level commands
compose these transforms behind
operations such as `check`, `compile`, and `run`.

The complete representation pipeline is:

```text
Source text
    |
    | parse
    v
Canopy (parsed Source AST)
    |
    | canopy-to-crown: transform and desugar
    v
Crown (Standard Source AST / HIR)
    |
    | crown-to-heartwood: resolve, check, and elaborate
    v
Heartwood (typed Core IR / functional MIR)
    |
    | heartwood-to-timber: generate portable C11
    v
Timber (portable C11 / low-level handoff)
    |
    | host C compiler and linker
    v
target object code and executable
```

## T-Diagrams

In each T-diagram, the left arm names the accepted representation, the right
arm names the emitted representation, and the stem names the implementation
language of the translator.

### Canopy to Crown

```text
 Canopy           Crown
      \           /
       \_________/
            |
            |
           C11
```

### Crown to Heartwood

```text
 Crown           Heartwood
      \           /
       \_________/
            |
            |
           C11
```

### Heartwood to Timber

```text
 Heartwood       Timber
        \         /
         \_______/
             |
             |
            C11
```

### Composed compiler

The matching arms show how the three translators compose without skipping a
representation boundary:

```text
 canopy-to-crown     crown-to-heartwood     heartwood-to-timber

 Canopy       Crown       Heartwood       Timber
      \       /   \       /      \       /
       \_____/     \_____/        \_____/
          |           |              |
          |           |              |
         C11         C11            C11
```

Parsing precedes these three named representation transforms: it converts
Source text into Canopy. After Timber, the host C compiler performs the
machine-dependent remainder of the native pipeline.

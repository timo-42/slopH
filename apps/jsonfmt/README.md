# sloph-jsonfmt

`sloph-jsonfmt` is a deterministic, stdin-only JSON pretty-printer. It accepts
at most 16 MiB, emits two-space indentation and one final newline, retains JSON
number lexemes, and preserves object member order and duplicate keys.

```sh
printf '{"answer":42}' | apps/jsonfmt/build/sloph-jsonfmt
```

Malformed input, excess input, unexpected arguments, and I/O failures produce a
stable error category on standard error and a nonzero exit status.

# Hello World

The smallest SlopH application writes a byte string to standard output and
returns a successful process result.

Run it from the repository root:

```text
make
src/sloph-c-bootstrap/build/bin/sloph run examples/hello-world
```

Or compile and execute a native program:

```text
src/sloph-c-bootstrap/build/bin/sloph compile examples/hello-world -o hello
./hello
```

Expected output:

```text
Hello, world!
```

# Hello World

The smallest SlopH application writes a byte string to standard output and
returns a successful process result.

Run it from the repository root:

```text
sloph run examples/hello-world
```

Or compile and execute a native program:

```text
sloph compile examples/hello-world -o hello
./hello
```

Expected output:

```text
Hello, world!
```

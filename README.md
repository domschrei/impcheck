 
# Parallel & Distributed LRAT - Trusted Modules

Provides code for two executables, a parser and a checker module, which are meant to be used within a parallel clause-sharing solver
to perform LRAT checking on-the-fly.

## Build

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=DEBUG -DWRITE_DIRECTIVES=0
make
cd ..
```

### Flags

* `-DWRITE_DIRECTIVES=0`: Normal mode of operation
* `-DWRITE_DIRECTIVES=1`: Write each incoming directive into a separate binary file
* `-DWRITE_DIRECTIVES=2`: Write each incoming directive into a separate human-readable ASCII file

## Execute

`build/trusted_parser_process -formula-input=<path/to/cnf> -fifo-parsed-formula=<path/to/output>`  
`build/trusted_checker_process -fifo-directives=<path/to/input> -fifo-feedback=<path/to/output>`

The intended mode of operation is that all paths specified via `-fifo` options are in fact named UNIX pipes (i.e., via `mkfifo`).
However, you can also specify actual, complete files to "replay" a sequence of written directives and to write the results persistently.

### Checker Input/Output Format

Each directive to a checker process begins with a single character specifying the type of the directive, followed by a sequence of objects whose length (individually and in total) is given by the directive type and (in some cases) by certain "size" fields. The output by the checker process is similarly well-defined based on the shape of the directive. Please consult the definitions provided in `src/trusted/trusted_checker.c` for the exact specification.

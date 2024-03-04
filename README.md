 
# Parallel & Distributed LRAT - Trusted Modules

Provides code for two executables, a parser and a checker module, which are meant to be used within a parallel clause-sharing solver to perform LRAT checking on-the-fly.

## Compilation

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=DEBUG -DWRITE_DIRECTIVES=0 -DPARLRAT_FLUSH_ALWAYS=1
make
cd ..
```

### Flags

* `-DPARLRAT_WRITE_DIRECTIVES=0`: Normal mode of operation
* `-DPARLRAT_WRITE_DIRECTIVES=1`: Write each incoming directive into a separate binary file
* `-DPARLRAT_WRITE_DIRECTIVES=2`: Write each incoming directive into a separate human-readable ASCII file

* `-DPARLRAT_FLUSH_ALWAYS=0`: Flush checker's feedback pipe only for selected directives. Can be enabled (and is the most efficient) if the reading of feedback is done in a different thread than the writing of directives, or if reads are done in a non-blocking manner. CAN HANG otherwise, e.g., if a single thread forwards a clause derivation with a blocking write and then attempts a blocking read of the result.
* `-DPARLRAT_FLUSH_ALWAYS=1`: Flush checker's feedback pipe after every single directive. Required if a single thread alternates between blocking reads and writes to the respective pipes. Safe, but may be slower.

## Execution

### Isolated Execution

`build/trusted_parser_process -formula-input=<path/to/cnf> -fifo-parsed-formula=<path/to/output>`  
`build/trusted_checker_process -fifo-directives=<path/to/input> -fifo-feedback=<path/to/output>`

The intended mode of operation is that all paths specified via `-fifo-*` options are in fact named UNIX pipes (i.e., via `mkfifo`).
However, you can also specify actual, complete files to "replay" a sequence of written directives and to write the results persistently.

### End-to-end Execution

Please examine the file `test/test_full.c` and the corresponding executable `build/test_full`.
This code is (also) meant as an introduction and quick start on how these modules can be integrated in a parallel solver framework.

### Checker Input/Output Format

Each directive to a checker process begins with a single character specifying the type of the directive, followed by a sequence of objects whose length (individually and in total) is given by the directive type and (in some cases) by certain "size" fields. The output by the checker process is similarly well-defined based on the shape of the directive. Please consult the definitions provided in `src/trusted/checker_interface.h` for the exact specification.

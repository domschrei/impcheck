 
# ImpCheck - Immediate Parallel Proof Checking

Provides code for three executables (parser, checker, confirmer) to be used within a parallel clause-sharing solver for on-the-fly LRAT proof checking.

## Compilation

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=RELEASE -DIMPCHECK_WRITE_DIRECTIVES=0 -DIMPCHECK_FLUSH_ALWAYS=1
make
cd ..
```

### Flags

* `-DIMPCHECK_WRITE_DIRECTIVES=0`: Normal mode of operation
* `-DIMPCHECK_WRITE_DIRECTIVES=1`: Write each incoming directive into a separate binary file
* `-DIMPCHECK_WRITE_DIRECTIVES=2`: Write each incoming directive into a separate human-readable ASCII file

* `-DIMPCHECK_FLUSH_ALWAYS=0`: Flush checker's feedback pipe only for selected directives. Can be enabled (and is the most efficient) if the reading of feedback is done in a different thread than the writing of directives, or if reads are done in a non-blocking manner. CAN HANG otherwise, e.g., if a single thread forwards a clause derivation with a blocking write and then attempts a blocking read of the result.
* `-DIMPCHECK_FLUSH_ALWAYS=1`: Flush checker's feedback pipe after every single directive. Required if a single thread alternates between blocking reads and writes to the respective pipes. Safe, but may be slower.

## Execution

### Isolated Execution

`build/impcheck_parse -formula-input=<path/to/cnf> -fifo-parsed-formula=<path/to/output>`  
`build/impcheck_check -fifo-directives=<path/to/input> -fifo-feedback=<path/to/output>`  
`build/impcheck_confirm -formula-input=<path/to/cnf> -result=<10|20> -result-sig=<signature>`

The intended mode of operation is that all paths specified via `-fifo-*` options are in fact named UNIX pipes (i.e., via `mkfifo`).
However, you can also specify actual, complete files to "replay" a sequence of written directives and to write the results persistently.

For `impcheck_check`, specify the optional argument `-check-model` if you also intend to get found models a.k.a. satisfying assignments checked (used together with Mallob's `-otfcm=1`).

### End-to-end Execution

Please examine the file `test/test_full.c` which is built into the executable `build/test_full`.
This code is (also) meant as an introduction and quick start on how these modules can be integrated in a parallel solver framework.

### Checker Input/Output Format

Each directive to a checker begins with a single character specifying the type of the directive, followed by a sequence of objects whose length (individually and in total) is given by the directive type and (in some cases) by certain "size" fields. A checker's output is similarly well-defined based on the shape of the directive. Please consult the definitions provided in `src/trusted/checker_interface.h` for the exact specification.

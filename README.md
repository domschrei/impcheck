 
# ImpCheck - Immediate Massively Parallel Propositional Proof Checking

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

```
build/impcheck_parse -formula-input=<path/to/cnf> -fifo-parsed-formula=<path/to/output>
build/impcheck_check -fifo-directives=<path/to/input> -fifo-feedback=<path/to/output> [-check-model]
build/impcheck_confirm -formula-input=<path/to/cnf> -result=<10|20> -result-sig=<signature>
```
The intended mode of operation is that all paths specified via `-fifo-*` options are in fact named UNIX pipes precreated via `mkfifo`.
However, you can also specify actual, complete files to "replay" a sequence of written directives and to write the results persistently.

For `impcheck_check`, specify the optional argument `-check-model` if you also intend to get found models a.k.a. satisfying assignments checked (used together with Mallob's `-otfcm=1`). This can incur some additional memory overhead during solving for large formulas since all original problem clauses need to be kept (i.e., deletions concerning them are ignored by the checker).

### End-to-end Execution

Please examine the file `test/test_full.c` which is built into the executable `build/test_full`.
This code is (also) meant as an introduction and quick start on how these modules can be integrated in a parallel solver framework.

For an example within an actual parallel solver framework, please examine the following files of [MallobSat](https://github.com/domschrei/mallob/tree/proof23/):
* [`trusted_parser_process_adapter.hpp`](https://github.com/domschrei/mallob/blob/b9c7d1ec87d1511c541074562573af709536a8d2/src/app/sat/proof/trusted_parser_process_adapter.hpp)
  * Runs an instance of `impcheck_parse` to fetch the formula data and signature.
* [`cadical.cpp`](https://github.com/domschrei/mallob/blob/b9c7d1ec87d1511c541074562573af709536a8d2/src/app/sat/solvers/cadical.cpp)
  * Creates an `LratConnector` instance which is used to run and communicate with a checker instance. Initializes callbacks within CaDiCaL which connect to the `LratConnector`. Also consider the reactions for a solver finding a result.
* [`lrat_connector.hpp`](https://github.com/domschrei/mallob/blob/b9c7d1ec87d1511c541074562573af709536a8d2/src/app/sat/proof/lrat_connector.hpp)
  * Holds a `TrustedCheckerProcessAdapter` and lets solvers put LRAT directives (`LratOp` objects) into a SPSC ringbuffer. Spawns two separate threads: one reads directives from the ringbuffer and forwards them to the process adapter, and one receives results from the process adapter and reacts accordingly.
* [`trusted_checker_process_adapter.hpp`](https://github.com/domschrei/mallob/blob/b9c7d1ec87d1511c541074562573af709536a8d2/src/app/sat/proof/trusted_checker_process_adapter.hpp)
  * Runs an instance of `impcheck_check`, writing and reading directives/results as needed.

### Checker Input/Output Format

Each directive to a checker begins with a single character specifying the type of the directive, followed by a sequence of objects whose length (individually and in total) is given by the directive type and (in some cases) by certain "size" fields. A checker's output is similarly well-defined based on the shape of the directive. Please consult the definitions provided in `src/trusted/checker_interface.h` for the exact specification.

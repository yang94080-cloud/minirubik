# Refactor verification results

Environment: GCC 13.3.0, Linux x86-64, compiled with `-std=c99 -O2 -Wall -Wextra -Werror`.

| Gate / check | Result |
|---|---:|
| Build `solver` and `verify` | Passed |
| Solved input `12345671111111` | Passed; empty move sequence |
| One-move input `12356741112323` | Passed; `B'` |
| 11-move input `21345671111111` | Passed; `R B' D2 R' B R' B' R D2 R B` |
| H1 admissibility | 3,674,160 checked; 0 failures |
| H2 permutation table | missing=0, goal=0, max=7, decreasing failures=0, edge failures=0 |
| H2 orientation table | missing=0, goal=0, max=6, decreasing failures=0, edge failures=0 |
| H3 optimal lengths and replay | 3,674,160 checked; length failures=0; replay failures=0 |
| H3 wall-clock time | 470.209 seconds |
| H4 packed table accessors | 5,769 checked; 0 failures |

These are host C results for this refactored source. They do not replace the separate Ripes assembly tests or its retired-instruction measurements.

## User-machine run

The user reran `verify.exe --self-test` on their Windows machine and reported:

```text
H3: checked=3674160, length_failures=0, replay_failures=0
H3 wall-clock verification time: 620.810 seconds
H4: checked=5769, failures=0
H1-H4 implemented checks passed
```

The environment is recorded below in the same format as the HackMD note. The user requested reuse of that record; it lists two machines and does not identify which one produced this run. Keep the 620.810-second Windows run separate from the Linux/GCC 13.3.0 supporting run above. No missing value or per-run attribution has been inferred.

## Experimental Environment (HackMD format)


Development continued on a second computer. The machine used for each measurement must be identified explicitly.

| Item  | Environment in the uploaded report |
|---|---|
| Operating system | Windows 11 |
| Processor | AMD Ryzen 7 7700, 8 cores |
| Installed memory | 32 GiB | 
| Native compiler | MinGW.org GCC 6.3.0 |
| Native compiler target |  Windows x86-64 |

**TODO:** Confirm which environment produced the Stage 1 memory and throughput results, the native C verification, and the final target tests. The later pipeline, GCC-comparison, and final-audit runs were performed after changing computers. Do not assign all results to one machine without checking the records.

| Tool or setting | Value |
|---|---|
| Ripes channel | Continuous pre-release |
| Ripes version | `v2.2.6-106-g5b8a616` |
| Ripes commit | `5b8a616` |
| Platform | Windows x86-64 |
| Assembly target | RV32I, without ISA extensions |
| Fast simulation model | `RV32_ISS` |
| Visual pipeline model | `RV32_5S` |
| Cross compiler for comparison/audit | xPack RISC-V GNU GCC 15.2.0 |
| Cross-compiler options | `-O2 -march=rv32i -mabi=ilp32` |
| Selected multilib | `rv32i/ilp32` |

`RV32_ISS` is an instruction-set simulator, not a pipelined processor model. Ripes' built-in assembler is an assembler, not an instruction set.


## Repository integration checks (2026-10-16)

The Makefile, README, and ignore rules were merged with the current public
`yang94080-cloud/minirubik` repository files. The cube/search core (`solver.c`,
`solver.h`, `solver_main.c`) is byte-identical to the previously verified
refactor. Only the verifier's vector-check entry point was added; H1–H4
implementations were not changed. The exhaustive H3 run was not repeated for
this documentation/build integration.

Supporting environment: Linux x86-64, GCC 13.3.0,
`-O2 -std=c99 -Wall -Wextra`.

- All three host executables build successfully.
- H2 and H4 pass.
- All eight existing solution vectors pass the IDA* length-and-replay check.
- `make check-quick` passes, including original mini vectors, invalid inputs,
  argument errors, and closed-stdout checks.
- The reference input prints `R B' D2 R' B R' B' R D2 R B`.
- The optional inherited Frama-C proof target has not been rerun.

These integration checks are separate from the user's 620.810-second Windows
measurement and the retained Ripes results.

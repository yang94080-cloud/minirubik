# Minirubik on RV32I

> Edited draft, 2026-10-08. The measurements below were supplied from my Windows runs. Items marked **TODO** need to be completed before submission. This draft records tested behavior; it does not claim that every assignment requirement has been satisfied.

- Assignment: [Optimizations and RISC-V Assembly](https://hackmd.io/@sysprog/2026-arch-homework1).
- Fork: [yang94080-cloud/minirubik](https://github.com/yang94080-cloud/minirubik).
- Reported baseline commit: `231796c`.
- Submission commit/tag: **TODO**.
- Published HackMD revision URL: **TODO**.

## Stage 1: Characterize the Baseline

### 1.1 Experimental Environment

Development continued on a second computer. The machine used for each measurement must be identified explicitly.

| Item | Earlier declared environment | Environment in the uploaded report |
|---|---|---|
| Operating system | Windows 11 | Windows 11 |
| Processor | Intel Core i9-14900K | AMD Ryzen 7 7700, 8 cores |
| Installed memory | 64 GiB | **TODO: confirm capacity/unit; the draft says 32 MiB** |
| Native compiler | GCC 16.2.0 | MinGW.org GCC 6.3.0 |
| Native compiler target | Previously recorded as Windows x86-64 | **TODO: copy the output of `gcc -dumpmachine`** |

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

### 1.2 What the Baseline Computes

The program computes a shortest sequence of moves that solves an input 2×2×2 cube. It builds a complete BFS table starting at the solved state. For each discovered state, `toward_solved` records a move that leads one step closer to solved.

The program parses the input, ranks the state, and follows the recorded moves until it reaches rank zero. It prints right (`R`), back (`B`), and down (`D`) turns, including half turns and inverse turns.

The metric is the **half-turn metric (HTM)**: a quarter turn, half turn, or inverse quarter turn costs one move. The complete host BFS reports `3,674,160 states; diameter 11`.

### 1.3 Cube Representation and Invariants

One corner is fixed at the front-upper-left position in its solved orientation. This removes the 24 whole-cube orientations from the representation. Only seven moving corners need to be stored.

The input format is `PPPPPPPOOOOOOO`:

- The first seven digits identify the cubies at the seven moving positions.
- The last seven digits encode orientation using `1`, `2`, and `3`.
- Parsing subtracts one from each digit, producing internal cubie identifiers `0..6` and twists `0..2`.

The position order is front-upper-right, front-down-right, front-down-left, back-upper-right, back-down-right, back-down-left, and back-upper-left.

For example, `12345671111111` is solved. `21345671111111` swaps the first two cubies and leaves all twists zero.

#### Input Parsing

`parse_state()` converts a 14-character input string into the permutation and orientation arrays of a `state_t`.

The first seven characters must be digits from `'1'` to `'7'`, and the last seven must be digits from `'1'` to `'3'`. An invalid character causes the function to return `0`.

Each accepted character is converted by subtracting `'1'`. The first seven values are stored in `state->p` as `0–6`; the last seven are stored in `state->o` as `0–2`.

After conversion, the function checks that `input[14]` is `'\0'` and calls `valid(state)` to verify unique cubie identifiers and a total twist divisible by three.

It returns `1` when all checks pass and `0` otherwise. The converted arrays remain in `state`; `rank_state()` then encodes them into the permutation and orientation ranks.

#### Invariants and Ranking

The validity constraints are:

1. Every cubie identifier occurs exactly once.
2. Every orientation is in `0..2`.
3. The total twist satisfies

$$
\sum_{i=0}^{6} o[i] \equiv 0 \pmod{3}.
$$

The seventh twist is determined by the first six. There is no restriction to even permutations in this model. The twist constraint is modulo three; it is not a permutation-parity condition.

The program combines a permutation rank with six base-three orientation digits:

$$
\text{rank} = p \times 729 + o,
\qquad
0 \le p < 5040,\quad 0 \le o < 729.
$$

The number of represented states is

$$
7! \times 3^6 = 5040 \times 729 = 3,674,160.
$$

**TODO — student-written mathematical explanation:** Explain the fixed-corner group `<R,B,D>`, its nine HTM generators, and its Cayley graph. Explain how exhaustive BFS proves both the existence of distance-11 states and the absence of deeper states.

### 1.4 Baseline Memory Cost

| Array | Calculation | Bytes |
|---|---|---:|
| `toward_solved` | `3,674,160 × 1` | 3,674,160 |
| BFS `queue` | `3,674,160 × 4` | 14,696,640 |
| `permutation[3][5040]` | `3 × 5040 × 2` | 30,240 |
| `orientation[3][729]` | `3 × 729 × 2` | 4,374 |
| **Dominant-array peak** | Sum | **18,405,414** |

This is calculated array storage, not a measured process-memory peak. Other local variables and allocation overhead are excluded. The BFS queue is the largest allocation.

### 1.5 Baseline Computation Cost

The original program rebuilds the complete table for every valid normal invocation, including an already-solved input.

| Transition-table construction operation | Count |
|---|---:|
| `unrank_state()` | 5,769 |
| `quarter_turn()` | 17,307 |
| `rank_state()` | 17,307 |

BFS expands every state once and generates nine neighbors per state:

| BFS operation | Count |
|---|---:|
| States expanded | 3,674,160 |
| Neighbor candidates | 33,067,440 |
| Permutation lookups | 33,067,440 |
| Orientation lookups | 33,067,440 |
| **Total transition lookups** | **66,134,880** |
| Newly discovered non-root states enqueued | 3,674,159 |

An already-discovered candidate still requires its two transition lookups and rank calculation. It skips the following recording operations:

~~~c
toward_solved[there] = inverse_move[move];
queue[tail++] = there;
~~~

For a returned path of length `L`, query processing performs `L` solution-table lookups, `L` calls to `apply_move()`, and `L + 1` calls to `rank_state()`. All returned paths have at most 11 HTM moves.

### 1.6 Host Bytes per Guest Byte

Using `RV32_ISS`, two fresh Ripes processes wrote different amounts of guest memory with a word-store loop. Host private memory was read using `PrivateMemorySize64`.

| Guest bytes written | Host private memory, bytes |
|---:|---:|
| 4,096 | 30,875,648 |
| 1,048,576 | 118,489,088 |

The incremental ratio is

$$
\frac{118,489,088 - 30,875,648}
     {1,048,576 - 4,096}
= 83.88235.
$$

The measured result is approximately **83.88 host bytes per additional guest byte written**. This is an empirical slope for this build and test, not a universal property of every computer or workload.

**TODO — student-written interpretation:** Use this slope to estimate the cost of the baseline's dominant-array peak, distinguish an extrapolation from a measurement, and explain why the full BFS table should remain a host verification artifact.

### 1.7 Retired Instructions per Second

The benchmark rate uses Ripes' wall-clock **model execution time**, not the GUI clock-rate field.

| Model | Retired instructions | Model execution time | Retired instructions/s |
|---|---:|---:|---:|
| `RV32_ISS` | 1,048,581 | 0.422 s | 2,484,789 |
| `RV32_5S` | 1,048,581 | 7.234 s | 144,952 |

For this benchmark, the ISS simulation rate is about 17.14 times the five-stage rate. This compares simulator throughput, not the clock frequency of a physical processor.

**TODO — student-written transition to Stage 2:** Explain how the baseline storage and simulation-work counts motivate the chosen target design.

## Stage 2: Representation and Optimal Search

### 2.1 Objective and Constraints

Return a shortest HTM solution for every valid input with:

- `.data + .bss + .rodata <= 131,072 bytes`.
- RV32I instructions only, without multiply/divide extensions or compiler arithmetic helpers.
- No target heap allocation, recursion, or floating point.
- Host-generated transition and heuristic tables, with the actual search running on the target.
- At most 50,000,000 retired instructions for every distance-11 input on the pinned `RV32_ISS` build, with rendering excluded.

The assignment also requires an arbitrary input supplied as a 14-character string inlined at assembly time. The currently tested assembly starts from `START_PERM` and `START_ORIENT` constants. **That input-interface requirement remains to be addressed.**

### 2.2 State Representation and Four Tables

The search stores the permutation and orientation ranks separately, using two `uint16_t` values per state: **4 bytes**.

The baseline already has separate permutation and orientation transition tables. The revised solver retains them and replaces ordinary solving's complete `toward_solved` table with input-dependent search.

| Table | Entry type | Entries | Bytes |
|---|---|---:|---:|
| `permutation` | `uint16_t` | 15,120 | 30,240 |
| `orientation` | `uint16_t` | 2,187 | 4,374 |
| `perm_dist` | `uint8_t` | 5,040 | 5,040 |
| `orient_dist` | `uint8_t` | 729 | 729 |
| **Four-table payload** | | **23,076** | **40,383** |

The two distance tables are constructed by BFS over the projected permutation and orientation graphs. Each of the nine HTM moves has cost one, including a half turn.

### 2.3 Search and Lower Bound

The implementation uses nonrecursive IDA* with

$$
h(p,o) = \max(\text{perm\_dist}[p],\text{orient\_dist}[o]).
$$

A complete solution must restore both coordinates. Their maximum is a lower bound; their sum can overestimate because one move can affect both.

Search starts at the input's lower bound and increases the bound by one after an unsuccessful iteration. A branch is pruned when `depth + h > bound`. Consecutive moves on the same face are skipped because they combine or cancel in HTM.

**TODO — student-written justification:** Explain why the first solution is shortest, why neither pruning rule removes a necessary shortest path, and why this memory/time trade-off fits the Stage 1 measurements.

### 2.4 Search Workspace

The developed version includes candidate reuse and a heuristic cache:

| Array | Bytes |
|---|---:|
| `path_perm[12]` | 24 |
| `path_orient[12]` | 24 |
| `candidate_perm[12]` | 24 |
| `candidate_orient[12]` | 24 |
| `next_move[12]` | 12 |
| `path_h[12]` | 12 |
| Output `path_move[11]` | 11 |
| **Array payload** | **131** |

There are twelve state slots for depths `0..11` and eleven move slots. This replaces the earlier, incomplete 71-byte workspace count. Scalars, constants, alignment, and other target data are accounted for in the final section sizes.

Normal host solving calls `build_small_tables()` and `search_ida()`. Full `build_table()` is retained for host validation.

### 2.5 Host Correctness Gates

| Gate | Scope | Recorded result |
|---|---|---|
| H1 | Compare `h(s)` with exact BFS distance over 3,674,160 states | 0 admissibility failures |
| H2 | Check populated tables, maxima, solved entries, and transitions | Native distance checks and the final exported-table audit passed |
| H3 | Compare search length with exact distance and replay the path over 3,674,160 states | 0 length failures; 0 replay failures |
| H4 | Compare packed and unpacked access at every valid permutation/orientation index | 5,769 checked; 0 failures |

The latest shared full C H3 run recorded **1478.317 seconds**. This is exhaustive verification time for that development version, not a single-query benchmark.

H4 tested an experimental two-distances-per-byte accessor at both even and odd indices. The production target uses **unpacked byte distances**. Passing H4 does not mean packed tables were integrated into the final solver.

The later H2 audit checked the actual exported assembly tables; its results appear in Stage 4. It supplements the earlier native H2 function, which checked the distance tables but did not completely audit the exported transition tables.

## Stage 3: Improve Efficiency in C

### 3.1 Changes and Operation Counts

The C search was changed to reuse intermediate quarter turns, cache each search frame's heuristic, and skip a whole same-face move group.

Recorded input: `21345671111111`.

| Instrumented count | With optimization | Estimated equivalent without that optimization |
|---|---:|---:|
| Transition-table lookups | 467,922 | 935,830 |
| Heuristic evaluations | 233,962 | 506,905 |

These are operation counts from instrumented development runs. The equivalent counts are calculated for the same search activity; they are not separately timed executions.

For a fully generated three-move face group, independent generation needs `2 + 4 + 6 = 12` transition lookups; reuse needs `2 + 2 + 2 = 6`. Early termination can leave a partially generated group.

The move metadata is `move_face[9]` and `face_first_move[3]`. Candidate-reuse storage adds 48 bytes, and `path_h[12]` adds 12 bytes.

**TODO — student-written optimization analysis:** For each change, explain the removed work, its added storage or memory traffic, and why the change preserves correctness. Distinguish fewer lookups from fewer total retired instructions.

### 3.2 Native Search Timing

With tables already built and solution printing outside the timing loop, a recorded batch for the reference input produced:

| Runs | Total search time | Average search time |
|---:|---:|---:|
| 1,000 | 1,890.771 ms | 1.891 ms |

Native compilation used:

~~~powershell
gcc -std=c99 -O2 -g solver.c -o solver.exe
~~~

This is a development timing record. Its machine and source revision still need to be associated with the retained logs. It does not establish an optimization speedup without a matched comparison.

The exhaustive host H1/H3 checks passed after the C changes. The uploaded latest C source has its profiling counter increments commented out.

## Stage 4: RV32I Assembly and Target Evidence

### 4.1 Implementation and Input Interface

The assembly uses fixed arrays, `lhu/sh` for 16-bit coordinates and transitions, and `lbu/sb` for byte distances and move indices.

The transition-row byte offsets are:

| Table | R row | B row | D row |
|---|---:|---:|---:|
| Permutation | 0 | 10,080 | 20,160 |
| Orientation | 0 | 1,458 | 2,916 |

Each entry is addressed as `base + face_offset + 2 * coordinate`. The measured search keeps table and workspace addresses in registers, rejects children before descent, and avoids repeating the admitted child's bound check.

**TODO — student-written RV32I explanation:** Identify the shifts, additions, comparisons, and load/store widths used in the important fragments. Explain the cost of pseudo-instructions after assembly and the register-lifetime choices.

**Open input requirement:** The current builder accepts rank coordinates, not an inlined 14-character state string. The tested core handles many inputs, but this is not evidence that the specified input interface has been implemented.

### 4.2 Assembly Refinement

The following development runs used the formerly worst input `54721631111111`, rank `2437047`. They are individual-case measurements, not a worst-case proof for each intermediate version.

| Change recorded during development | RV32_ISS retired instructions |
|---|---:|
| Initial full-run version | 97,830,672 |
| Early child pruning | 75,757,758 |
| Transition/distance table bases cached | 71,385,872 |
| Repeated admitted-state bound check removed | 66,267,628 |
| Initial workspace-base changes | 62,002,400 |
| Candidate and child-state base changes completed | 55,604,472 |
| Further control-array base changes | 50,699,564 |
| Restored source, corrected heuristic store, and cached face-offset bases | **48,353,660** |

Historical linked `.text` sizes were not supplied for these intermediate measurements. The final production CLI `.text` is **1048 bytes**.

**TODO — student-written interpretation:** Explain the important before/after instruction sequences. Attach source revisions and retained logs, and report any historical code sizes that were actually recorded. Do not invent missing sizes.

### 4.3 Complete Distance-11 ISS Gate

The final renderer-free build completed all **2,644 distance-11 states**:

| Result | Value |
|---|---:|
| Cases completed | 2,644 / 2,644 |
| Execution/report errors | 0 |
| Length failures | 0 |
| Replay failures | 0 |
| Budget failures | 0 |
| Maximum retired instructions | **48,353,660** |
| Worst input | `54721631111111` |
| New parallel-batch wall time | **2594.337 s** |
| Full distance-11 ISS gate | **PASS** |

The batch time excludes the previously completed seed case and includes host process overhead. It is not Ripes' model execution time. Parallel host execution shortened collection time; it did not change each case's retired-instruction count.

Evidence is retained in `results-final-iss-seed` and `results-final-iss-parallel`, including the merged CSV, per-case reports, and source/tool manifests.

**Required separate reference count:** The final production `RV32_ISS` report for `21345671111111` recorded **17,683,217 retired instructions**, a solution length of **11**, and replay status **0**. This row was retrieved from the retained full-run CSV. The GCC-comparison count below belongs to a different harness.

### 4.4 Replay, Required Vector, and Pipeline Tests

The program replays its returned path and checks for solved coordinates. At termination, `x10/a0` holds the length and `x12/a2 = 0` indicates successful replay.

The final production build was tested with a solved cube, a short scramble, and the required distance-11 vector on both models:

| Input | Case | Length | Replay status, both models | RV32_ISS instructions | RV32_5S instructions |
|---|---|---:|---:|---:|---:|
| `12345671111111` | Solved | 0 | 0 | 103 | 102 |
| `12356741112323` | Short scramble | 1 | 0 | 539 | 538 |
| `21345671111111` | Distance 11 | 11 | 0 | 17,683,217 | 17,683,216 |

Each model completed 3/3 cases with zero execution/report errors, length failures, and replay failures. The distance-11 vector also passed the ISS instruction budget. Average process wall times were **0.436 seconds on RV32_ISS** and **17.727 seconds on RV32_5S**. These are process times, including assembly and reporting.

Evidence directories: `results-required-three-iss` and `results-required-three-5s`. The `All 2644 ... False` fields in these three-case summaries describe their limited scope; the complete distance-11 result is recorded separately in Section 4.3.

An additional earlier final-build five-stage test of `54321671111111` returned length 1, replay status 0, and 304 retired instructions. That record is retained in `results-final-smoke-5s-newpc-v2`.

The required vector returned an optimal 11-move path:

~~~text
R B' D2 R' B R' B' R D2 R B
~~~

T5 is supported for the returned paths actually tested. T6 passed for the required vector. The prescribed solved/short/distance-11 set now reproduces on both models. This completes the tested-case portion of T7; the inlined 14-character input interface and reproduction instructions for arbitrary grader inputs remain outstanding.

### 4.5 GCC RV32I Comparison

The cross-compiler comparison used GCC 15.2.0, `-O2 -march=rv32i -mabi=ilp32`, the same input, shared table data, and a common replay checker. Renderer and solution printing were excluded from both comparison builds.

| Input | Length | GCC instructions | Manual assembly instructions | GCC .text, bytes | Manual .text, bytes |
|---|---:|---:|---:|---:|---:|
| `12345671111111` | 0 | 65 | 97 | 1144 | 960 |
| `12356741112323` | 1 | 742 | 519 | 1144 | 960 |
| `54321671111111` | 1 | 340 | 285 | 1148 | 964 |
| `21345671111111` | 11 | 25,832,625 | 17,683,027 | 1144 | 960 |

All selected comparison cases passed length and replay checks. For the 11-move case, manual assembly retired 31.55% fewer instructions and had 16.08% smaller linked `.text`. The GCC build retired fewer instructions for the solved case.

The C function was taken from the latest uploaded C search. The manual version additionally uses early child pruning and register-held bases; the comparison includes those implementation differences. It must not be described as a compiler-only comparison.

This harness differs from the final submission build. Its manual `.text = 960` is not the final production `.text = 1048`. The comparison harness reserves a 4-KiB stack; that reservation is not part of the final static-data audit.

Evidence directory: `gcc-compare/results-v1`.

**TODO — student-written comparison analysis:** Explain the solved-case exception using the entry paths and disassembly, and identify which changes account for the nontrivial-case gains. Avoid claiming that the manual version is faster for every input.

### 4.6 Final H2 and Memory Audit

The host auditor compared the actual assembly table data against cubie-model transitions and recomputed projected HTM distances.

| Table | Entries checked | Missing | Range failures | Mismatches | Maximum | Verified solved entry/row |
|---|---:|---:|---:|---:|---:|---|
| Permutation transitions | 15,120 | 0 | 0 | 0 | 5039 | `[1104,9,198]` |
| Orientation transitions | 2,187 | 0 | 0 | 0 | 728 | `[426,16,0]` |
| `perm_dist` | 5,040 | 0 | — | 0 | 7 | 0 |
| `orient_dist` | 729 | 0 | — | 0 | 6 | 0 |

Search metadata failures were zero. Host H2 audit time was **0.102 seconds**. This additional audit did not rerun exhaustive H1 or H3.

| Storage measurement | Bytes |
|---|---:|
| Four-table payload | 40,383 |
| Final CLI `.data` | 40,587 |
| Final GUI `.data` | 40,766 |
| Final `.bss` and `.rodata` in the audited layout | 0 |
| Final production CLI linked `.text` | 1048 |
| Final CLI `.text + .data` | 41,635 |
| Static-data limit | 131,072 |

The final CLI and GUI static-data layouts both pass the 128-KiB budget. The four-table payload is not the full data total. The extra `.riscv.attributes` bytes in data-only objects are metadata, not allocated static data.

The GUI data-only object has `.text = 0` because it contains data only; it is not a measurement of GUI code size.

Evidence directory: `final-audit/results-20261008-215036`.

### 4.7 LED Matrix and Build Selection

The GUI demonstration used a **35-wide × 25-high** LED Matrix and completed the actual 11-move solution, ending with the cube solved.

The renderer uses `LED_MATRIX_0_BASE`, `LED_MATRIX_0_WIDTH`, and `LED_MATRIX_0_HEIGHT`. Its row-major address expression is

$$
\text{BASE} + 4(y \times \text{WIDTH} + x).
$$

Each facelet occupies 4×3 pixels. The unfolded net spans 35×20 pixels, with five spare rows below it. The colors are white, orange, green, red, blue, and yellow.

The display shows the initial cube while search is running. After a solution is found, it redraws after each move read from `path_move[]`. This animation follows the returned path.

The pinned assembler did not accept `.if/.endif`. The build script instead retains or removes `RENDER_BEGIN/RENDER_END` blocks before assembly:

| Build | Renderer | Use |
|---|---|---|
| `solver-gui.s` | Included | GUI LED demonstration |
| `solver-cli.s` | Excluded | CLI measurements |

Both are produced from `solver-rv32i.s` and share the search, replay checker, and text output. Renderer-related code and data are their only build-selection difference; all LED-symbol references are excluded from the CLI build.

**TODO — student-written mapping and evidence:** Explain how cubie positions, twists, and sticker colors map to the net. Insert your own initial/intermediate/solved LED screenshots and the matrix-dimension screenshot.

### 4.8 Pipeline Walkthrough

The captured `RV32_5S` trace follows `lhu t1,0(t0)` at instruction address **0x1c0**.

| Stage | Recorded cycle |
|---|---:|
| IF | 109 |
| ID | 110 |
| EX | 111 |
| MEM | 112 |
| WB | 113 |
| After the WB clock edge | 114 |

The load's data address was `0x10000012` and the loaded halfword was `0x04cb`. `t1/x6` still showed `0x00000009` at MEM and in the pre-edge WB snapshot; the following clock updated it to `0x000004cb`.

Use `EX(1).png` for EX. The earlier `EX.png` was actually a MEM-stage capture. `write success.png` records the register update after WB.

The store `sh t1,0(t0)` at instruction address **0x1dc** was captured in MEM at cycle 119, with data input `0x000004cb` and memory write enable asserted. The reported destination was `0x10009df0`, subsequently containing bytes `cb 04`. The memory-view screenshot still needs to be attached.

**TODO — student-written instruction analysis:** Explain IF/ID/EX/MEM/WB for the selected load and store, the register-write-enable and writeback-multiplexer signals, and why the memory bytes are correct. Attach readable signal/value screenshots. Distinguish an instruction address from a data address, and distinguish a stage snapshot from the clock edge that commits its result.

## Development Evidence and AI Assistance

ChatGPT/Codex provided substantial assistance with concepts, C search and verification fragments, assembly and renderer fragments, debugging and optimization, recovery of a working assembly source, test/audit/comparison scripts, and report organization and English editing.

The Windows measurement outputs reported here were supplied from my local runs. I also carried out the reported Ripes LED demonstration and pipeline stepping. Supporting assistant-side checks are not presented as my Ripes measurements.

**TODO — student-written contribution and reflection:** State the decisions or analysis you made independently, the suggestions you accepted, rejected, or changed, and the difficulties you resolved. Identify actual commits and at least three substantive report revisions, or explain thin history and provide equivalent development evidence. Do not invent a revision history or describe AI-generated assembly as independently authored.

The assignment explicitly reserves representation/search design, admissibility reasoning, reported measurements, optimization reasoning, RV32I assembly, and report analysis for the student. Disclosure does not establish compliance with that independent-work requirement. This draft records the assistance actually received.

## Items to Complete Before Phase 1 Submission

- [ ] Confirm machine/compiler attribution and the memory unit.
- [ ] Complete the student-written mathematical, design, optimization, comparison, LED, and pipeline analysis.
- [ ] Address the inlined 14-character assembly input interface.
- [x] Record solved/short/distance-11 production tests on both required processor models: 3/3 on each, with zero length or replay failures.
- [x] Record the reference vector's production ISS instruction count from the final CSV: 17,683,217.
- [ ] Add readable screenshots and revision-pinned source/test-evidence links.
- [ ] Check the final source against the tested manifests; distinguish earlier evidence from any new revision.
- [ ] Push the submission source and evidence, tag the submission commit, and record the tag.
- [ ] Publish HackMD, enable editing by signed-in users, and record the report revision URL.
- [ ] Submit the fork and report through the assignment form and retain the acceptance email.

Phase 2 is a separate interview due on October 18. Completing the tested quantitative gates does not submit Phase 1 or complete Phase 2.

## References

- [Assignment 1 specification](https://hackmd.io/@sysprog/2026-arch-homework1).
- [Course AI guidelines](https://hackmd.io/@sysprog/arch2026-ai-guidelines).
- [Upstream minirubik](https://github.com/sysprog21/minirubik).
- [Ripes documentation](https://github.com/mortbopet/Ripes/tree/master/docs).
- The retained native validation logs, Ripes reports, comparison results, final audit, and original screenshots described above.

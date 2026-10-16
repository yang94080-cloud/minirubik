# minirubik

An optimal C99 solver for the 2×2×2 Rubik’s Cube. The production C solver uses small transition and distance tables with
nonrecursive IDA* and returns a shortest solution in at most 11 half-turn-metric
moves. The complete BFS table is built only by the host verification program.

## Why a cube is a graph

Ernő Rubik created the original cube in 1974 to demonstrate how parts can move
independently without breaking the whole. A 3×3 cube has 20 moving pieces and
about 4.3 × 10¹⁹ reachable arrangements. The smaller 2×2 cube keeps the eight
corners and removes the edges and fixed centers. [Philo Li’s formula-free
introduction](https://philoli.com/zh/blog/solve-rubiks-cube-without-formulas/)
offers the key intuition: every turn is reversible, turns can be composed, and
their order matters—`R U` is generally not `U R`.

Human solvers use those facts to move a few pieces while restoring the rest;
the commutator `A B A⁻¹ B⁻¹` is the standard example. This program uses the
same group structure differently: it treats every valid arrangement as a node,
every face turn as an edge, and searches the graph. The verifier builds the complete BFS reference; the
production solver searches only as needed for its input. It does not use
the article’s 3×3 Roux stages or a library of memorized algorithms.

The solver gives the eight corner positions the numbers `0–7`. The 2.5D
walkthrough below shows where those numbers are on the physical cube.

## How it works

1. Fix one corner to remove whole-cube rotations.
2. Rank the remaining corner permutation and six independent orientations into
   a dense integer.
3. Build separate permutation/orientation transition and HTM distance tables.
4. Use IDA* with `max(perm_dist[p], orient_dist[o])` as an admissible bound.
5. Increase the bound until a solution is found; validate against exact BFS in
   the separate host verification executable.

## Build and run

```sh
make
./solver 21345671111111
./verify --self-test       # H1–H4; H3 takes minutes
make check-quick          # solution vectors, invalid input, output errors
make check                # quick checks plus exhaustive H1–H4
make prove                # optional inherited Frama-C target; not rerun here
```

`make` builds `solver`, `verify`, and the original `mini` variant.

- `solver.c` / `solver.h`: cube model, small tables, input parsing, IDA*.
- `solver_main.c`: normal solver entry point.
- `verify.c`: full BFS oracle, H1–H4, exporters, and vector validation.
- `mini.c`: original compact complete-BFS solver, retained unchanged.

On Windows PowerShell without GNU Make:

```powershell
gcc -std=c99 -O2 -Wall -Wextra solver.c solver_main.c -o solver.exe
gcc -std=c99 -O2 -Wall -Wextra solver.c verify.c -o verify.exe
.\solver.exe 21345671111111
.\verify.exe --self-test
```

Verification/export options have moved from `solver` to `verify`:
`--verify-h1`, `--verify-h2`, `--verify-h3`, `--verify-h4`, `--self-test`,
`--export-depth11`, `--export-transition-tables`, `--export-distance-tables`.
Run `./verify --check-vectors` from the repository root for the existing vector
file. IDA* vectors are checked by length and replay; multiple shortest paths are
valid. `mini` retains its original byte-exact vector check.

Individual gates are available as `make check-h1` through `make check-h4`.
The optional proof target is retained, but prior proof counts and coverage
measurements do not automatically apply to this refactor.

The RV32I assembly, renderer scripts, target test evidence, and report images
are retained. This host refactor does not change them. The original timing and
memory comparison of `mini` against the old BFS solver is historical, not a
measurement against the new IDA* executable.

See [TEST-RESULTS.md](TEST-RESULTS.md) for environments and the latest host run
(H3: 620.810 seconds). See [report-reviewed.md](report-reviewed.md) for the
assignment implementation and target evidence; [report.md](report.md) remains
the original baseline description.

The 14-digit argument describes the scramble and the printed line is the
solution. Both formats are explained below.

### Reading the 14-digit input

The program receives one 14-digit code with no spaces. For explanation, split
it into two groups:

```diagram
2134567 1111111
└── P ─┘ └── O ─┘
  cubies   twists
```

Imagine seven numbered seats and seven students. A position is a seat fixed in
space; a cubie is the physical corner that can move to another seat. In the
solved cube, cubie 1 sits in position 1, cubie 2 in position 2, and so on.
The real cube has no printed numbers; `0–7` are labels used only by this solver.

#### Step 1: Hold the cube in one direction

Keep `FRONT` facing you and `UP` pointing upward. Position `0` is the corner
nearest the upper-left of the front face. It is an anchor for describing the
other corners; the physical cubie is not glued in place.

```diagram
                              BACK
                    ·───────────────·
                   ╱               ╱│
                  ╱        UP     ╱ │
                 ╱               ╱  │
              [0]───────────────·   │
               │                │   │
               │     FRONT      │ R │
               │                │   ·
               │                │  ╱
               │                │ ╱
               │                │╱
               ·────────────────·
```

`R` marks the narrow `RIGHT` face.

#### Step 2: Separate the front and back layers

A 2×2×2 cube has only corner cubies. Looking from the fixed direction, four
corner positions touch the front face and four touch the back face. Each
bracketed number below names one whole corner, not one colored sticker:

```diagram
 FRONT LAYER                          BACK LAYER

 upper-left   upper-right             upper-left   upper-right
     [0]────────[1]                       [7]────────[4]
      │          │                         │          │
      │          │       front ↔ back      │          │
     [3]────────[2]                       [6]────────[5]
 down-left    down-right               down-left    down-right
```

The front layer runs clockwise from its upper-left corner as `0, 1, 2, 3`.
The back layer is drawn as if seen through the cube from the front: `7` is
upper-left, followed clockwise by `4, 5, 6`.

#### Step 3: Join the two layers into positions 0–7

Slide the back square up and to the right, the same direction the cube recedes
in Step 1, to get the complete 2.5D position map. The back edges are drawn
through the front face rather than hidden behind it:

```diagram
                           BACK
                      [7]────────[4]
                     ╱ │        ╱ │
                  [0]──│─────[1]  │
                   │   │      │   │
                   │  [6]─────│──[5]
                   │ ╱        │ ╱
                  [3]────────[2]
                      FRONT
```

The seven characters of `P` describe positions `1, 2, 3, 4, 5, 6, 7` in that
order; the anchor at position `0` is left out.

#### Step 4: Put the cubies into those positions

Compare the position map on the left with the filled cube on the right. Read
`P = 2134567` from left to right to fill the positions. The arrows below the
figure identify the two positions that change.

```diagram
 POSITION MAP                             AFTER P = 2134567
 (fixed seats)                            (cubies now in seats)

     [7]────────[4]                           [7]────────[4]
    ╱ │        ╱ │                           ╱ │        ╱ │
 [0]──│─────[1]  │                        [0]──│─────[2]  │
  │   │      │   │                         │   │      │   │
  │  [6]─────│──[5]                        │  [6]─────│──[5]
  │ ╱        │ ╱                           │ ╱        │ ╱
 [3]────────[2]                           [3]────────[1]
     FRONT                                    FRONT

 position:     1 2 3 4 5 6 7
 P says:       2 1 3 4 5 6 7
               │ │ └───────── cubies 3–7 stay in their matching seats
               │ └─────────── put cubie 1 in position 2: [2] becomes [1]
               └───────────── put cubie 2 in position 1: [1] becomes [2]
```

So the first two digits, `21`, exchange the two corners on the front-right
edge. The remaining digits, `34567`, leave the other five movable corners
where they were. `P` must contain every digit from `1` through `7` exactly
once; otherwise a cubie would be missing or duplicated.

The seven seats named by `P` are:

| Position | Corner of the cube |
| :---: | :--- |
| 1 | front, upper, right |
| 2 | front, down, right |
| 3 | front, down, left |
| 4 | back, upper, right |
| 5 | back, down, right |
| 6 | back, down, left |
| 7 | back, upper, left |

The second group, `O = 1111111`, describes the twist of the cubie in each of
those same seven positions:

| Digit | Meaning |
| :---: | :--- |
| 1 | not twisted |
| 2 | twisted by +120° |
| 3 | twisted by −120° |

Here every orientation digit is `1`, so the two corners change places without
being twisted. For a valid cube, convert orientation digits to `0`, `1`, and
`2`; their sum must be divisible by three. The solved code is
`12345671111111`. `make check` uses the exchanged-corner example above.

## Reading the solution

```sh
$ ./solver 21345671111111
R B' D2 R' B R' B' R D2 R B
```

Each token is one face turn. Apply them left to right; after the last one the
cube is solved.

| Token | Meaning |
| :---: | :--- |
| `R` | turn the `RIGHT` face 90° clockwise |
| `B` | turn the `BACK` face 90° clockwise |
| `D` | turn the `DOWN` face 90° clockwise |

Clockwise means clockwise as seen by someone looking directly at that face from
outside the cube, so you have to walk around to the back to read `B` and look up
from underneath to read `D`. Two suffixes modify a turn:

| Suffix | Meaning |
| :---: | :--- |
| none | 90° clockwise |
| `'` | 90° counterclockwise, the inverse |
| `2` | 180°, direction does not matter |

`R`, `B`, and `D` are the only faces that appear, because turning `UP`, `FRONT`,
or `LEFT` would move the anchor at position `0`. A turn counts as one move
whichever suffix it carries, which is the half-turn metric; under that metric no
position needs more than 11 moves. Solving an already-solved cube prints an
empty line.

See [`report.md`](report.md) for the model, algorithm, diagrams, and Frama-C
validation notes.


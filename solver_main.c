#include "solver.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    state_t state;

    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver");
        return 2;
    }

    build_small_tables();

    uint32_t rank = rank_state(&state);
    uint16_t p = (uint16_t) (rank / ORIENTATIONS);
    uint16_t o = (uint16_t) (rank % ORIENTATIONS);
    uint8_t moves[11];

    int length = search_ida(p, o, permutation, orientation,
                            perm_dist, orient_dist, moves);
    if (length < 0) {
        fputs("could not find a solution\n", stderr);
        return 1;
    }

    for (int i = 0; i < length; ++i) {
        printf("%s%s", i ? " " : "", move_names[moves[i]]);
    }
    putchar('\n');
    return output_failed();
}

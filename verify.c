#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "solver.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *build_table(uint8_t *diameter);

#ifdef _WIN32
#include <windows.h>
static double monotonic_seconds(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER now;
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (double) now.QuadPart / (double) frequency.QuadPart;
}
#else
#include <time.h>
static double monotonic_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double) now.tv_sec + (double) now.tv_nsec / 1000000000.0;
}
#endif

static uint8_t *build_verified_reference(void)
{
    uint8_t diameter = 0;
    uint8_t *table = build_table(&diameter);

    if (!table || diameter != 11) {
        free(table);
        fputs("BFS reference table failed\n", stderr);
        return NULL;
    }
    return table;
}

static uint8_t *build_table(uint8_t *diameter)
{
    uint8_t *toward_solved = malloc(STATES);
    uint32_t *queue = malloc((size_t) STATES * sizeof *queue);

    if (!toward_solved || !queue) {
        free(toward_solved);
        free(queue);
        return NULL;
    }

    uint32_t head = 0, tail = 1, level_end = 1;

    memset(toward_solved, UINT8_MAX, STATES);
    queue[0] = 0;
    toward_solved[0] = 0;
    *diameter = 0;

    while (head < tail) {
        if (head == level_end) {
            level_end = tail;
            ++*diameter;
        }

        uint32_t here = queue[head++];
        uint16_t p = here / ORIENTATIONS;
        uint16_t o = here % ORIENTATIONS;

        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t next_p = p;
            uint16_t next_o = o;

            for (uint8_t turn = 0; turn < 3; ++turn) {
                next_p = permutation[face][next_p];
                next_o = orientation[face][next_o];

                uint32_t there =
                    (uint32_t) next_p * ORIENTATIONS + next_o;

                if (toward_solved[there] == UINT8_MAX) {
                    uint8_t move = face * 3U + turn;
                    toward_solved[there] = inverse_move[move];
                    queue[tail++] = there;
                }
            }
        }
    }

    free(queue);

    if (tail != STATES) {
        free(toward_solved);
        return NULL;
    }

    return toward_solved;
}

static int baseline_length(uint32_t rank, const uint8_t *table)
{
    state_t state;
    unrank_state(rank, &state);

    int length = 0;

    while (rank != 0) {
        /* 防止錯誤參考表造成越界或無限迴圈 */
        if (length >= 11 || table[rank] >= MOVES) {
            return -1;
        }

        state = apply_move(state, table[rank]);
        rank = rank_state(&state);
        ++length;
    }

    return length;
}

static int export_depth11_cases(void)
{
    build_small_tables();

    uint8_t diameter;
    uint8_t *table = build_table(&diameter);

    if (!table || diameter != 11) {
        free(table);
        fputs("BFS oracle failed\n", stderr);
        return 0;
    }

    FILE *out = fopen("depth11-cases.csv", "w");
    if (!out) {
        free(table);
        perror("depth11-cases.csv");
        return 0;
    }

    fprintf(out, "rank,p,o,state,distance\n");

    unsigned count = 0;
    int ok = 1;

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        int distance = baseline_length(rank, table);

        if (distance < 0) {
            ok = 0;
            break;
        }

        if (distance != 11)
            continue;

        state_t state;
        char input[15];

        unrank_state(rank, &state);

        for (unsigned i = 0; i < CUBIES; ++i) {
            input[i] = (char) ('1' + state.p[i]);
            input[i + CUBIES] = (char) ('1' + state.o[i]);
        }
        input[14] = '\0';

        fprintf(out, "%u,%u,%u,%s,11\n",
                (unsigned) rank,
                (unsigned) (rank / ORIENTATIONS),
                (unsigned) (rank % ORIENTATIONS),
                input);

        ++count;
    }

    if (ferror(out))
        ok = 0;
    if (fclose(out) != 0)
        ok = 0;

    free(table);

    printf("Distance-11 cases exported: %u\n", count);
    return ok && count == 2644;
}

static int verify_h1(const uint8_t *table)
{
    unsigned failures = 0;

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint16_t p = rank / ORIENTATIONS;
        uint16_t o = rank % ORIENTATIONS;

        uint8_t h = perm_dist[p];
        if (orient_dist[o] > h) {
            h = orient_dist[o];
        }

        int d = baseline_length(rank, table);

        if (d < 0 || h > d) {
            if (failures == 0) {
                printf("H1 first failure: state=%u, h=%u, d=%d\n",
                       (unsigned) rank, (unsigned) h, d);
            }
            ++failures;
        }
    }

    printf("H1: checked=%u, failures=%u\n",
           (unsigned) STATES, failures);

    return failures == 0;
}

static int verify_distance_table(
    const char *name,
    unsigned count,
    const uint16_t transition[3][count],
    const uint8_t distance[count])
{
    unsigned missing = 0;
    unsigned decreasing_failures = 0;
    unsigned edge_failures = 0;
    unsigned maximum = 0;

    for (unsigned index = 0; index < count; ++index) {
        if (distance[index] == UINT8_MAX) {
            ++missing;
            continue;
        }

        if (distance[index] > maximum) {
            maximum = distance[index];
        }

        bool found = index == 0;

        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t next = (uint16_t) index;

            for (uint8_t turn = 0; turn < 3; ++turn) {
                next = transition[face][next];

                if (distance[index] > distance[next] + 1) {
                    ++edge_failures;
                }

                if (index != 0 &&
                    distance[next] + 1 == distance[index]) {
                    found = true;
                }
            }
        }

        if (!found) {
            ++decreasing_failures;
        }
    }

    printf("H2 %s: missing=%u, goal=%u, max=%u, "
           "decreasing_failures=%u, edge_failures=%u\n",
           name, missing, (unsigned) distance[0], maximum,
           decreasing_failures, edge_failures);

    return missing == 0 &&
           distance[0] == 0 &&
           decreasing_failures == 0 &&
           edge_failures == 0;
}

static int verify_h2(void)
{
    int permutation_ok = verify_distance_table(
        "permutation", PERMUTATIONS, permutation, perm_dist);

    int orientation_ok = verify_distance_table(
        "orientation", ORIENTATIONS, orientation, orient_dist);

    return permutation_ok && orientation_ok;
}

static int verify_h3(const uint8_t *table)
{
    unsigned length_failures = 0;
    unsigned replay_failures = 0;

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint16_t p = rank / ORIENTATIONS;
        uint16_t o = rank % ORIENTATIONS;

        int d = baseline_length(rank, table);

        uint8_t moves[11];
        
        int length = search_ida(
            p, o,
            permutation, orientation,
            perm_dist, orient_dist,
            moves);

        if (d < 0 || length != d) {
            if (length_failures == 0) {
                printf("H3 first length failure: "
                       "state=%u, baseline=%d, IDA=%d\n",
                       (unsigned) rank, d, length);
            }
            ++length_failures;
        }

        if (length < 0 || length > 11) {
            ++replay_failures;
        } else {
            state_t replay;
            unrank_state(rank, &replay);

            for (int i = 0; i < length; ++i) {
                replay = apply_move(replay, moves[i]);
            }

            if (rank_state(&replay) != 0) {
                if (replay_failures == 0) {
                    printf("H3 first replay failure: state=%u\n",
                           (unsigned) rank);
                }
                ++replay_failures;
            }
        }

        if ((rank + 1) % 10000 == 0) {
            printf("H3 checked %u / %u states\n",
                   (unsigned) (rank + 1), (unsigned) STATES);
            fflush(stdout);
        }
    }

    printf("H3: checked=%u, length_failures=%u, "
           "replay_failures=%u\n",
           (unsigned) STATES, length_failures, replay_failures);

    return length_failures == 0 && replay_failures == 0;
}

static void pack_distances(
        const uint8_t *distance,
        unsigned count,
        uint8_t *packed)
    {
        memset(packed, 0, (count + 1) / 2);

        for (unsigned index = 0; index < count; ++index) {
            unsigned byte_index = index / 2;
            unsigned shift = (index % 2) * 4;

            packed[byte_index] |= distance[index] << shift;
        }
    }

static uint8_t read_packed(
        const uint8_t *packed,
        unsigned index)
    {
        unsigned byte_index = index / 2;
        unsigned shift = (index % 2) * 4;

        return (packed[byte_index] >> shift) & 0x0F;
    }

static int verify_h4(void)
{
    uint8_t packed_perm[(PERMUTATIONS + 1) / 2];
    uint8_t packed_orient[(ORIENTATIONS + 1) / 2];
    unsigned failures = 0;

    pack_distances(perm_dist, PERMUTATIONS, packed_perm);
    pack_distances(orient_dist, ORIENTATIONS, packed_orient);

    for (unsigned index = 0; index < PERMUTATIONS; ++index) {
        if (read_packed(packed_perm, index) !=perm_dist[index]) {
            ++failures;
        }
    }

    for (unsigned index = 0; index < ORIENTATIONS; ++index) {
        if (read_packed(packed_orient, index) != orient_dist[index]) {
            ++failures;
        }
    }

    printf("H4: checked=%u, failures=%u\n",
           (unsigned) (PERMUTATIONS + ORIENTATIONS), failures);

    return failures == 0;
}

static int self_test(void)
{
    const state_t solved = {{0, 1, 2, 3, 4, 5, 6}, {0}};
    state_t state;
    for (uint8_t move = 0; move < MOVES; ++move) {
        state = solved;
        state = apply_move(state, move);
        state = apply_move(state, inverse_move[move]);
        if (memcmp(&solved, &state, sizeof solved))
            return 0;
    }
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        unrank_state(rank, &state);
        if (!valid(&state) || rank_state(&state) != rank)
            return 0;
    }
    return 1;
}

static int export_distance_tables(void)
{
    FILE *file = fopen("distance-tables.s", "w");

    if (!file) {
        perror("distance-tables.s");
        return 0;
    }

    fprintf(file, ".data\n\nperm_dist:\n");

    for (unsigned i = 0; i < PERMUTATIONS; ++i) {
        fprintf(file, "    .byte %u\n",
                (unsigned) perm_dist[i]);
    }

    fprintf(file, "\norient_dist:\n");

    for (unsigned i = 0; i < ORIENTATIONS; ++i) {
        fprintf(file, "    .byte %u\n",
                (unsigned) orient_dist[i]);
    }

    int failed = ferror(file);

    if (fclose(file) != 0) {
        failed = 1;
    }

    return !failed;
}

static int export_transition_tables(void)
{
    FILE *file = fopen("transition-tables.s", "w");

    if (!file) {
        perror("transition-tables.s");
        return 0;
    }

    fprintf(file, ".data\n\npermutation:\n");

    for (unsigned face = 0; face < 3; ++face) {
        for (unsigned i = 0; i < PERMUTATIONS; ++i) {
            uint16_t value = permutation[face][i];

            fprintf(file, "    .byte %u, %u\n",
                    (unsigned) (value & 0xFF),
                    (unsigned) (value >> 8));
        }
    }

    fprintf(file, "\norientation:\n");

    for (unsigned face = 0; face < 3; ++face) {
        for (unsigned i = 0; i < ORIENTATIONS; ++i) {
            uint16_t value = orientation[face][i];

            fprintf(file, "    .byte %u, %u\n",
                    (unsigned) (value & 0xFF),
                    (unsigned) (value >> 8));
        }
    }

    int failed = ferror(file);

    if (fclose(file) != 0) {
        failed = 1;
    }

    return !failed;
}

/* IDA* may return a different optimal path from the baseline vectors. */
static int verify_solution_vectors(void)
{
    FILE *in = fopen("tests/solutions.txt", "r");
    if (!in) {
        perror("tests/solutions.txt");
        return 0;
    }
    char line[256];
    unsigned checked = 0, failures = 0;
    while (fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char *separator = strchr(line, '|');
        if (!separator) {
            ++failures;
            continue;
        }
        *separator++ = '\0';
        state_t state;
        if (!parse_state(line, &state)) {
            ++failures;
            continue;
        }
        unsigned expected = 0;
        for (char *token = strtok(separator, " \t\r\n"); token;
             token = strtok(NULL, " \t\r\n"))
            ++expected;
        uint32_t rank = rank_state(&state);
        uint8_t moves[11];
        int length = search_ida(rank / ORIENTATIONS, rank % ORIENTATIONS,
                               permutation, orientation, perm_dist,
                               orient_dist, moves);
        int ok = length >= 0 && length <= 11;
        if (ok) {
            for (int i = 0; i < length; ++i) {
                if (moves[i] >= MOVES) { ok = 0; break; }
                state = apply_move(state, moves[i]);
            }
        }
        if (!ok || length != (int) expected || rank_state(&state) != 0) {
            fprintf(stderr, "Solution vector failed: %s\n", line);
            ++failures;
        }
        ++checked;
    }
    int failed = ferror(in);
    if (fclose(in) != 0)
        failed = 1;
    printf("IDA* vectors: checked=%u, failures=%u\n", checked, failures);
    return checked > 0 && failures == 0 && !failed;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,
                "usage: %s --self-test|--verify-h1|--verify-h2|"
                "--verify-h3|--verify-h4|--check-vectors|--export-depth11|"
                "--export-transition-tables|--export-distance-tables\n",
                argc > 0 && argv[0] ? argv[0] : "verify");
        return 2;
    }

    build_small_tables();

    if (strcmp(argv[1], "--check-vectors") == 0)
        return verify_solution_vectors() ? output_failed() : 1;

    if (strcmp(argv[1], "--verify-h2") == 0)
        return verify_h2() ? output_failed() : 1;
    if (strcmp(argv[1], "--verify-h4") == 0)
        return verify_h4() ? output_failed() : 1;
    if (strcmp(argv[1], "--verify-h1") == 0 ||
        strcmp(argv[1], "--verify-h3") == 0) {
        uint8_t *table = build_verified_reference();
        if (!table)
            return 1;

        int ok;
        if (strcmp(argv[1], "--verify-h1") == 0) {
            ok = verify_h1(table);
        } else {
            double start = monotonic_seconds();
            ok = verify_h3(table);
            printf("H3 wall-clock verification time: %.3f seconds\n",
                   monotonic_seconds() - start);
        }
        free(table);
        return (!ok || output_failed()) ? 1 : 0;
    }
    if (strcmp(argv[1], "--export-depth11") == 0)
        return export_depth11_cases() ? output_failed() : 1;
    if (strcmp(argv[1], "--export-transition-tables") == 0) {
        if (!export_transition_tables()) {
            fputs("could not export transition tables\n", stderr);
            return 1;
        }
        puts("Exported transition-tables.s");
        return output_failed();
    }
    if (strcmp(argv[1], "--export-distance-tables") == 0) {
        if (!export_distance_tables()) {
            fputs("could not export distance tables\n", stderr);
            return 1;
        }
        puts("Exported distance-tables.s");
        return output_failed();
    }
    if (strcmp(argv[1], "--self-test") != 0) {
        fprintf(stderr, "unknown verification option: %s\n", argv[1]);
        return 2;
    }

    if (!self_test()) {
        fputs("self-test failed\n", stderr);
        return 1;
    }

    uint8_t *table = build_verified_reference();
    if (!table)
        return 1;

    int h1_ok = verify_h1(table);
    int h2_ok = verify_h2();

    double start = monotonic_seconds();
    int h3_ok = verify_h3(table);
    double elapsed = monotonic_seconds() - start;
    printf("H3 wall-clock verification time: %.3f seconds\n", elapsed);

    int h4_ok = verify_h4();
    free(table);

    if (!h1_ok || !h2_ok || !h3_ok || !h4_ok) {
        fputs("Correctness verification failed\n", stderr);
        return 1;
    }
    puts("H1-H4 implemented checks passed");
    return output_failed();
}

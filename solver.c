#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    STATES = PERMUTATIONS * ORIENTATIONS,
    MOVES = 9
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

    static uint16_t permutation[3][PERMUTATIONS];
    static uint16_t orientation[3][ORIENTATIONS];
    static uint8_t perm_dist[PERMUTATIONS];
    static uint8_t orient_dist[ORIENTATIONS];
    static uint64_t transition_lookups = 0;
    static uint64_t lookups_without_reuse = 0;
    static uint64_t cached_h_evaluations = 0;
    static uint64_t equivalent_h_evaluations = 0;
/*@ predicate valid_state(state_t *state) =
      (\forall integer i; 0 <= i < CUBIES ==>
         state->p[i] < CUBIES && state->o[i] < 3) &&
      (\forall integer i, j; 0 <= i < j < CUBIES ==>
         state->p[i] != state->p[j]) &&
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
 */

static const char *const move_names[MOVES] = {"R",  "R2", "R'", "B", "B2",
                                              "B'", "D",  "D2", "D'"};
static const uint8_t inverse_move[MOVES] = {2, 1, 0, 5, 4, 3, 8, 7, 6};
/* Each destination takes a cubie from source[face][destination]. */
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};
static const uint8_t move_face[MOVES] = {
    0, 0, 0,  // R、R2、R′
    1, 1, 1,  // B、B2、B′
    2, 2, 2   // D、D2、D′
};

static const uint8_t face_first_move[3] = {0, 3, 6};

/* The three quarter-turns preserve the fixed front-upper-left corner. */
/*@ requires face < 3;
    assigns \nothing;
    ensures \forall integer i; 0 <= i < CUBIES ==>
              \result.p[i] == state.p[source[face][i]];
    ensures \forall integer i; 0 <= i < CUBIES ==>
              \result.o[i] == (state.o[source[face][i]] + twist[face][i]) % 3;
 */
static state_t quarter_turn(state_t state, uint8_t face)
{
    state_t result;
    /*@ loop invariant 0 <= i <= CUBIES;
        loop invariant \forall integer j; 0 <= j < i ==>
          result.p[j] == state.p[source[face][j]];
        loop invariant \forall integer j; 0 <= j < i ==>
          result.o[j] == (state.o[source[face][j]] + twist[face][j]) % 3;
        loop assigns i, result.p[0..6], result.o[0..6];
        loop variant CUBIES - i;
    */
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        result.p[i] = state.p[from];
        result.o[i] = (uint8_t) ((state.o[from] + twist[face][i]) % 3U);
    }
    return result;
}

static state_t apply_move(state_t state, uint8_t move)
{
    uint8_t turns = (uint8_t) (move % 3U + 1U);
    for (uint8_t i = 0; i < turns; ++i)
        state = quarter_turn(state, (uint8_t) (move / 3U));
    return state;
}

/*@ requires \valid_read(state);
    requires \forall integer i; 0 <= i < CUBIES ==>
      0 <= state->p[i] < CUBIES;
    requires \forall integer i, j; 0 <= i < j < CUBIES ==>
      state->p[i] != state->p[j];
    requires \forall integer i; 0 <= i < CUBIES ==>
      0 <= state->o[i] < 3;
    assigns \nothing;
    ensures \result < STATES;
 */
static uint32_t rank_state(const state_t *state)
{
    uint32_t p = 0, o = 0;
    /*@ loop invariant 0 <= i <= CUBIES;
        loop invariant (i == 0 ==> p == 0) && (i == 1 ==> p <= 6) &&
          (i == 2 ==> p <= 41) && (i == 3 ==> p <= 209) &&
          (i == 4 ==> p <= 839) && (i == 5 ==> p <= 2519) &&
          (i >= 6 ==> p <= 5039);
        loop assigns i, p;
        loop variant CUBIES - i;
     */
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        /*@ loop invariant i + 1 <= j <= CUBIES;
            loop invariant smaller <= j - i - 1;
            loop assigns j, smaller;
            loop variant CUBIES - j;
         */
        for (uint8_t j = (uint8_t) (i + 1U); j < CUBIES; ++j)
            if (state->p[j] < state->p[i])
                ++smaller;
        p = p * (CUBIES - i) + smaller;
    }
    /*@ loop invariant 0 <= i <= 6;
        loop invariant (i == 0 ==> o == 0) && (i == 1 ==> o < 3) &&
          (i == 2 ==> o < 9) && (i == 3 ==> o < 27) &&
          (i == 4 ==> o < 81) && (i == 5 ==> o < 243) &&
          (i == 6 ==> o < 729);
        loop assigns i, o;
        loop variant 6 - i;
     */
    for (uint8_t i = 0; i < 6; ++i)
        o = o * 3U + state->o[i];
    return p * ORIENTATIONS + o;
}

/*@ requires \valid(state); requires rank < STATES; assigns *state; */
static void unrank_state(uint32_t rank, state_t *state)
{
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t p = rank / ORIENTATIONS, o = rank % ORIENTATIONS, f = 720;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t q = (uint8_t) (p / f);
        p %= f;
        state->p[i] = available[q];
        for (uint8_t j = q; j + 1U < CUBIES - i; ++j)
            available[j] = available[j + 1U];
        if (i < 5)
            f /= 6U - i;
    }
    for (uint8_t i = 6; i-- > 0;) {
        state->o[i] = (uint8_t) (o % 3U);
        sum = (uint8_t) (sum + state->o[i]);
        o /= 3U;
    }
    state->o[6] = (uint8_t) ((3U - sum % 3U) % 3U);
}

/*@ requires \valid_read(state);
    requires \initialized(&state->p[0..6]) && \initialized(&state->o[0..6]);
    assigns \nothing;
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->p[i] < CUBIES && state->o[i] < 3;
    ensures \result != 0 ==> \forall integer i, j; 0 <= i < j < CUBIES ==>
      state->p[i] != state->p[j];
    ensures \result != 0 ==>
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
    ensures complete: valid_state(state) ==> \result != 0;
 */
static int valid(const state_t *state)
{
    uint8_t sum = 0;
    /*@ loop invariant 0 <= i <= CUBIES;
        loop invariant sum <= 2 * i;
        loop invariant sum == (i > 0 ? state->o[0] : 0) +
          (i > 1 ? state->o[1] : 0) + (i > 2 ? state->o[2] : 0) +
          (i > 3 ? state->o[3] : 0) + (i > 4 ? state->o[4] : 0) +
          (i > 5 ? state->o[5] : 0) + (i > 6 ? state->o[6] : 0);
        loop invariant \forall integer j; 0 <= j < i ==>
          state->p[j] < CUBIES && state->o[j] < 3;
        loop invariant \forall integer j, k; 0 <= j < k < i ==>
          state->p[j] != state->p[k];
        loop assigns i, sum;
        loop variant CUBIES - i;
    */
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3)
            return 0;
        /*@ loop invariant 0 <= j <= i;
            loop invariant \forall integer k; 0 <= k < j ==>
              state->p[k] != state->p[i];
            loop assigns j;
            loop variant i - j;
        */
        for (uint8_t j = 0; j < i; ++j)
            if (state->p[j] == state->p[i])
                return 0;
        sum = (uint8_t) (sum + state->o[i]);
    }
    return sum % 3U == 0;
}

static int search_ida(
    uint16_t start_perm,
    uint16_t start_orient,
    const uint16_t permutation[3][PERMUTATIONS],
    const uint16_t orientation[3][ORIENTATIONS],
    const uint8_t perm_dist[PERMUTATIONS],
    const uint8_t orient_dist[ORIENTATIONS],
    uint8_t path_move[11])
{
    uint16_t path_perm[12];
    uint16_t path_orient[12];
    uint8_t next_move[12];

    uint16_t candidate_perm[12];
    uint16_t candidate_orient[12];
    uint8_t path_h[12];

    /* 計算一次輸入狀態的下界 */
    uint8_t bound = perm_dist[start_perm];
    if (orient_dist[start_orient] > bound) {
        bound = orient_dist[start_orient];
    }

    //++cached_h_evaluations;
    //++equivalent_h_evaluations;

    const uint8_t start_h = bound;

    for (; bound <= 11; ++bound) {
        uint8_t depth = 0;

        path_perm[0] = start_perm;
        path_orient[0] = start_orient;
        path_h[0] = start_h;
        next_move[0] = 0;

        while (true) {
            uint16_t p = path_perm[depth];
            uint16_t o = path_orient[depth];

            if (p == 0 && o == 0) {
                return depth;
            }

            /* 沒有快取時，每次到這裡都會重算下界 */
            //++equivalent_h_evaluations;

            /* 現在直接讀取已保存的下界 */
            uint8_t h = path_h[depth];

            if (depth + h > bound ||
                depth >= bound ||
                next_move[depth] >= MOVES) {
                if (depth == 0) {
                    break;
                }

                --depth;
                continue;
            }

            uint8_t move = next_move[depth]++;
            uint8_t face = move_face[move];

            /* 跳過連續轉同一面 */
           if (depth > 0 &&
                face == move_face[path_move[depth - 1]]) {
                next_move[depth] = face_first_move[face] + 3;
                continue;
            }
            /* 開始新的一個面，從父狀態重新初始化 */
            if (move == face_first_move[face]) {
                candidate_perm[depth] = p;
                candidate_orient[depth] = o;
            }
            /* 接續同一面的中間結果 */
            candidate_perm[depth] =
                permutation[face][candidate_perm[depth]];

            candidate_orient[depth] =
                orientation[face][candidate_orient[depth]];

            //transition_lookups += 2;
            //lookups_without_reuse += 2 * (move % 3 + 1);

            /* 保存子狀態與動作 */
            path_move[depth] = move;
            path_perm[depth + 1] = candidate_perm[depth];
            path_orient[depth + 1] = candidate_orient[depth];

            /* 新子狀態只計算一次下界 */
            uint8_t child_h = perm_dist[candidate_perm[depth]];
            if (orient_dist[candidate_orient[depth]] > child_h) {
                child_h = orient_dist[candidate_orient[depth]];
            }

            path_h[depth + 1] = child_h;
            // ++cached_h_evaluations;

            ++depth;
            next_move[depth] = 0;
        }
    }

    return -1;
}
static int parse_state(const char *input, state_t *state);

static void build_small_tables(void)
{
    state_t state;
     for (uint16_t rank = 0; rank < PERMUTATIONS; ++rank) {
        unrank_state((uint32_t) rank * ORIENTATIONS, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            permutation[face][rank] =
                (uint16_t) (rank_state(&next) / ORIENTATIONS);
        }
    }
    uint16_t perm_queue[PERMUTATIONS];
    uint16_t perm_head = 0;
    uint16_t perm_tail = 1;

    memset(perm_dist, UINT8_MAX, sizeof perm_dist);
    perm_dist[0] = 0;
    perm_queue[0] = 0;

    while (perm_head < perm_tail) {
        uint16_t p = perm_queue[perm_head++];

        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t next = p;

            for (uint8_t turn = 0; turn < 3; ++turn) {
                next = permutation[face][next];

                if (perm_dist[next] == UINT8_MAX) {
                    perm_dist[next] = perm_dist[p] + 1;
                    perm_queue[perm_tail++] = next;
                }
            }
        }
    }

    for (uint16_t rank = 0; rank < ORIENTATIONS; ++rank) {
        unrank_state(rank, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            orientation[face][rank] =
                (uint16_t) (rank_state(&next) % ORIENTATIONS);
        }
    }
    
    uint16_t orient_queue[ORIENTATIONS];
    uint16_t orient_head = 0;
    uint16_t orient_tail = 1;

    memset(orient_dist, UINT8_MAX, sizeof orient_dist);
    orient_dist[0] = 0;
    orient_queue[0] = 0;

    while (orient_head < orient_tail) {
    uint16_t o = orient_queue[orient_head++];

    for (uint8_t face = 0; face < 3; ++face) {
        uint16_t next = o;

        for (uint8_t turn = 0; turn < 3; ++turn) {
            next = orientation[face][next];

            if (orient_dist[next] == UINT8_MAX) {
                orient_dist[next] = orient_dist[o] + 1;
                orient_queue[orient_tail++] = next;
            }
        }
    }
}
    // 四張小型表的建置程式
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
/*@ requires valid_read_string(input);
    requires \valid(state);
    assigns state->p[0..6], state->o[0..6];
    ensures \result != 0 ==> input[14] == '\0';
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->p[i] < CUBIES && state->o[i] < 3;
    ensures \result != 0 ==> \forall integer i, j; 0 <= i < j < CUBIES ==>
      state->p[i] != state->p[j];
    ensures \result != 0 ==>
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->p[i] == input[i] - '1';
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->o[i] == input[i + CUBIES] - '1';
 */
static int parse_state(const char *input, state_t *state)
{
    /*@ loop invariant 0 <= i <= 14;
        loop invariant i <= strlen(input);
        loop invariant i <= 7 ==> \initialized(&state->p[0..i-1]);
        loop invariant i >= 7 ==> \initialized(&state->p[0..6]);
        loop invariant i >= 7 ==> \initialized(&state->o[0..i-8]);
        loop invariant \forall integer j; 0 <= j < i && j < CUBIES ==>
          state->p[j] == input[j] - '1';
        loop invariant \forall integer j; 0 <= j < i - CUBIES ==>
          state->o[j] == input[j + CUBIES] - '1';
        loop assigns i, state->p[0..6], state->o[0..6];
        loop variant 14 - i;
     */
    for (int i = 0; i < 14; ++i) {
        int limit = i < 7 ? 7 : 3;
        if (input[i] < '1' || input[i] > '0' + limit)
            return 0;
        (i < 7 ? state->p : state->o)[i % 7] = (uint8_t) (input[i] - '1');
    }
    return input[14] == '\0' && valid(state);
}

/* stdout is fully buffered off a terminal, so a write error surfaces at the
 * flush, not at the printf that queued the bytes. Every exit path that has
 * produced output goes through here.
 */
static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
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

int main(int argc, char **argv)
{
    state_t state;
    uint8_t diameter;

    if (argc == 2 &&
        !strcmp(argv[1], "--export-depth11")) {
        return export_depth11_cases() ? output_failed() : 1;
    }
    
    if (argc == 2 &&
        !strcmp(argv[1], "--export-transition-tables")) {

        build_small_tables();

        if (!export_transition_tables()) {
            fputs("could not export transition tables\n", stderr);
            return 1;
        }

        puts("Exported transition-tables.s");
        return output_failed();
    }

    if (argc == 2 &&
        !strcmp(argv[1], "--export-distance-tables")) {

        build_small_tables();

        if (!export_distance_tables()) {
            fputs("could not export distance tables\n", stderr);
            return 1;
        }

        puts("Exported distance-tables.s");
        return output_failed();
    }

     if (argc == 2 && !strcmp(argv[1], "--verify-h4")) {
        build_small_tables();
        int ok = verify_h4();
        return ok ? output_failed() : 1;
    }

    /* 驗證流程：保留原版完整建表 */
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
    if (!self_test()) {
        fputs("self-test failed\n", stderr);
        return 1;
    }

    build_small_tables();

    uint8_t *table = build_table(&diameter);
    if (!table) {
        fputs("could not build complete state table\n", stderr);
        return 1;
    }

    if (diameter != 11) {
        free(table);
        fputs("BFS check failed\n", stderr);
        return 1;
    }

    int h1_ok = verify_h1(table);
    int h2_ok = verify_h2();

    /* 只計時 H3 */
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    int h3_ok = verify_h3(table);

    QueryPerformanceCounter(&end);

    double h3_seconds =
        (double) (end.QuadPart - start.QuadPart) /
        (double) frequency.QuadPart;

    printf("H3 wall-clock verification time: %.3f seconds\n",
        h3_seconds);

    int h4_ok = verify_h4();

    free(table);

    if (!h1_ok || !h2_ok || !h3_ok || !h4_ok) {
        fputs("Correctness verification failed\n", stderr);
        return 1;
    }

    puts("H1-H4 implemented checks passed");
    return output_failed();
}
        

    /* 一般求解：先解析輸入 */
    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver");
        return 2;
    }

    /* 只建立小型表，再用 IDA* 求解 */
    build_small_tables();

    uint32_t rank = rank_state(&state);
    uint16_t p = rank / ORIENTATIONS;
    uint16_t o = rank % ORIENTATIONS;
    uint8_t moves[11];

    transition_lookups = 0;
    lookups_without_reuse = 0;
    cached_h_evaluations = 0;     
    equivalent_h_evaluations = 0;  

    int length = search_ida(
        p, o,
        permutation, orientation,
        perm_dist, orient_dist,
        moves);

    if (length < 0) {
        fputs("could not find a solution\n", stderr);
        return 1;
    }

    const char *separator = "";

    for (int i = 0; i < length; ++i) {
        printf("%s%s", separator, move_names[moves[i]]);
        separator = " ";
    }

    putchar('\n');
    return output_failed();
}

#include "solver.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

uint16_t permutation[3][PERMUTATIONS];
uint16_t orientation[3][ORIENTATIONS];
uint8_t perm_dist[PERMUTATIONS];
uint8_t orient_dist[ORIENTATIONS];

const char *const move_names[MOVES] = {"R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"};
const uint8_t inverse_move[MOVES] = {2, 1, 0, 5, 4, 3, 8, 7, 6};

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
static const uint8_t move_face[MOVES] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
static const uint8_t face_first_move[3] = {0, 3, 6};

/*@ predicate valid_state(state_t *state) =
      (\forall integer i; 0 <= i < CUBIES ==>
         state->p[i] < CUBIES && state->o[i] < 3) &&
      (\forall integer i, j; 0 <= i < j < CUBIES ==>
         state->p[i] != state->p[j]) &&
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
 */



state_t quarter_turn(state_t state, uint8_t face)
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

state_t apply_move(state_t state, uint8_t move)
{
    uint8_t turns = (uint8_t) (move % 3U + 1U);
    for (uint8_t i = 0; i < turns; ++i)
        state = quarter_turn(state, (uint8_t) (move / 3U));
    return state;
}

uint32_t rank_state(const state_t *state)
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

void unrank_state(uint32_t rank, state_t *state)
{
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t p = rank / ORIENTATIONS, o = rank % ORIENTATIONS, f = 720;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t q = (uint8_t) (p / f);
        p %= f;
        state->p[i] = available[q];
        for (uint8_t j = q; (unsigned) j + 1U < (unsigned) (CUBIES - i); ++j)
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

int valid(const state_t *state)
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

int search_ida(
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

    //Calculate the lower bound for a given input state
    uint8_t bound = perm_dist[start_perm];
    if (orient_dist[start_orient] > bound) {
        bound = orient_dist[start_orient];
    }


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

            // When there is no cache, the lower bound is recalculated every time this point is reached.
        
            //Directly loading the saved bounds.
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

            //Skip consecutive turns of the same face
           if (depth > 0 &&
                face == move_face[path_move[depth - 1]]) {
                next_move[depth] = face_first_move[face] + 3;
                continue;
            }
            //Start a new facet, re-initializing from the parent state.
            if (move == face_first_move[face]) {
                candidate_perm[depth] = p;
                candidate_orient[depth] = o;
            }
            //Continuing from the intermediate results on the same page
 
            candidate_perm[depth] =
                permutation[face][candidate_perm[depth]];

            candidate_orient[depth] =
                orientation[face][candidate_orient[depth]];


            //Saving Sub-states and Actions
            path_move[depth] = move;
            path_perm[depth + 1] = candidate_perm[depth];
            path_orient[depth + 1] = candidate_orient[depth];

            //The lower bound for a new child state is calculated only once.
            uint8_t child_h = perm_dist[candidate_perm[depth]];
            if (orient_dist[candidate_orient[depth]] > child_h) {
                child_h = orient_dist[candidate_orient[depth]];
            }

            path_h[depth + 1] = child_h;

            ++depth;
            next_move[depth] = 0;
        }
    }

    return -1;
}

void build_small_tables(void)
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
    // Scripts for creating four small tables
}

int parse_state(const char *input, state_t *state)
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

int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}

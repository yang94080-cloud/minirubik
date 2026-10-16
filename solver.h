#ifndef MINIRUBIK_SOLVER_H
#define MINIRUBIK_SOLVER_H

#include <stdint.h>

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    STATES = PERMUTATIONS * ORIENTATIONS,
    MOVES = 9
};

typedef struct {
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
} state_t;

extern uint16_t permutation[3][PERMUTATIONS];
extern uint16_t orientation[3][ORIENTATIONS];
extern uint8_t perm_dist[PERMUTATIONS];
extern uint8_t orient_dist[ORIENTATIONS];
extern const char *const move_names[MOVES];
extern const uint8_t inverse_move[MOVES];

state_t quarter_turn(state_t state, uint8_t face);
state_t apply_move(state_t state, uint8_t move);
uint32_t rank_state(const state_t *state);
void unrank_state(uint32_t rank, state_t *state);
int valid(const state_t *state);
int parse_state(const char *input, state_t *state);
void build_small_tables(void);
int search_ida(uint16_t start_perm, uint16_t start_orient,
               const uint16_t permutation[3][PERMUTATIONS],
               const uint16_t orientation[3][ORIENTATIONS],
               const uint8_t perm_dist[PERMUTATIONS],
               const uint8_t orient_dist[ORIENTATIONS],
               uint8_t path_move[11]);
int output_failed(void);

#endif

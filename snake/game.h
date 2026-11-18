/* game.h - the snake game and its autopilot, without any screen
 *
 * A front end (the terminal game, snaked) sets up a board with
 * game_new, turns with game_turn or lets the autopilot steer (ai_on),
 * and calls game_step once per step. The game calls game_cell, which
 * the front end has, for every cell that changed, so it can draw it. */

/* directions, clockwise so that (d + 2) % 4 is the opposite */
enum { dir_up, dir_right, dir_down, dir_left };
extern const int dir_dx[4];
extern const int dir_dy[4];

/* what a step did */
enum { step_moved, step_ate, step_over, step_won };

#define START_LEN 4

extern int board_w, board_h;   /* in cells, both even */
extern int body_cap;           /* board_w * board_h */
extern char *grid;             /* 1 where the snake is */
extern int *body;              /* ring buffer of cell indices */
extern int head;               /* index of the head in body */
extern int length;
extern int dir;
extern int food;               /* cell, or -1 */
extern int ai_on;              /* the autopilot steers */

void rng_seed(void);
unsigned long rng_next(void);

/* A new game on a board of w by h cells (made even, at least 8x2).
 * Returns -1 when out of memory. */
int game_new(int w, int h);
void game_free(void);
void game_turn(int d);
int game_step(void);
/* forget the autopilot's plan (when it is switched on) */
void ai_reset(void);

/* the front end's: cell c has changed */
void game_cell(int c);

/* game.c - the snake game and its autopilot (see game.h) */

#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "game.h"

const int dir_dx[4] = { 0, 1, 0, -1 };
const int dir_dy[4] = { -1, 0, 1, 0 };

int board_w, board_h;
int body_cap;
char *grid;
int *body;
int head;
int length;
int dir;
int food;
int ai_on;

static int queue[2];         /* turns waiting for the next steps */
static int queued;
static int mem_failed;       /* a board_mem failed */
static unsigned long rng;

/* autopilot */
static int *cyc_next;        /* the cycle: the next cell of each */
static int ai_have;          /* there is a cycle for this board */
static int *t_next;          /* a cycle being made */
static int nblk;             /* 2x2 blocks */
static int *uf;              /* union-find over blocks */
static char *e_want;         /* edges: 1 wanted, 2 unwanted */
static char *e_tree;         /* edges in the tree */
static char *blk_mark;       /* blocks reached */
static int *blk_queue;
static int *b_disc, *b_low;  /* for finding bridges */
static int *b_pe;
static char *e_bridge;       /* edges that are bridges */
static int *uf_size;
static int *log_edge, *log_root; /* what the planner did, to undo */
static int log_n;
static int *f_cell, *f_try, *f_log, *f_ord; /* planner frames */
static int *ai_dist;         /* for breadth first search */
static int *ai_queue;
static int *cyc_ord;         /* place of each cell on the cycle */
static int ai_plan_n;        /* length of the planned way */
static int ai_limit;         /* a new way must be shorter */
static int ai_food = -1;     /* the food it is going for */
static int ai_from;          /* where it was when it came up */

void
rng_seed(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    rng = (unsigned long)ts.tv_nsec ^ ((unsigned long)ts.tv_sec << 7) ^
        (unsigned long)getpid();
    rng &= 0xffffffffUL;
    if (rng == 0) {
        rng = 1;
    }
}

/* xorshift32 */
unsigned long
rng_next(void)
{
    rng ^= (rng << 13) & 0xffffffffUL;
    rng ^= rng >> 17;
    rng ^= (rng << 5) & 0xffffffffUL;
    return rng;
}


static void
food_place(void)
{
    int i, k;

    food = -1;
    if (length >= body_cap) {
        return;
    }
    k = rng_next() % (body_cap - length);
    for (i = 0; i < body_cap; i++) {
        if (!grid[i]) {
            if (k == 0) {
                food = i;
                return;
            }
            k--;
        }
    }
}

/* autopilot
 *
 * It goes at the food along the straight line, and it never hits
 * anything and always wins, because the way it takes is always part
 * of a Hamiltonian cycle (a closed path through every cell) that the
 * whole snake lies on, in order. Following such a cycle it can never
 * be trapped, and it reaches every cell, the food too.
 *
 * The cycles are made from 2x2 blocks: any tree that joins all the
 * blocks gives one, going round the tree. Inside a block the cycle
 * goes down the left side, right along the bottom, up the right side
 * and left along the top; where the tree joins two blocks it goes
 * across instead. So each cell has two ways on, and the tree picks
 * one. The snake can therefore only turn every second cell, and its
 * line to the food is a staircase of steps two cells long.
 *
 * At each step it plans the way to the food (near the line), and asks
 * for a tree that agrees with both the body and that way. If there is
 * one, it follows the new cycle; if not, it follows the one it has.
 * Either way the food gets nearer along the cycle with every step, so
 * it is always eaten in the end, and the board fills up. */

static int
nbr(int c, int d)
{
    int x, y;

    x = c % board_w + dir_dx[d];
    y = c / board_w + dir_dy[d];
    if (x < 0 || x >= board_w || y < 0 || y >= board_h) {
        return -1;
    }
    return y * board_w + x;
}

/* the direction from cell a to the next cell b */
static int
dir_between(int a, int b)
{
    if (b == a + 1) {
        return dir_right;
    }
    if (b == a - 1) {
        return dir_left;
    }
    return b > a ? dir_down : dir_up;
}

static int
iabs(int v)
{
    return v < 0 ? -v : v;
}

static int
manhattan(int a, int b)
{
    int d;

    d = iabs(a % board_w - b % board_w) + iabs(a / board_w - b / board_w);
    return d;
}

/* how far cell n is off the line from cell a to cell b (times the
 * length of that line) */
static int
off_line(int n, int a, int b)
{
    int lx, ly, px, py, d;

    lx = b % board_w - a % board_w;
    ly = b / board_w - a / board_w;
    px = n % board_w - a % board_w;
    py = n / board_w - a / board_w;
    d = iabs(lx * py - ly * px);
    return d;
}

static int
tail_cell(void)
{
    return body[(head - length + 1 + body_cap) % body_cap];
}

/* steps along the cycle from cell a to cell b */
static int
cdist(int a, int b)
{
    int d;

    d = (cyc_ord[b] - cyc_ord[a] + body_cap) % body_cap;
    return d;
}

/* The two ways on from cell c: along its block (*in) and across to
 * the next block (*out), with the tree edge going across needs
 * (*edge): a block b and the one right of it is edge b, b and the one
 * below is edge nblk + b; -1 when across would leave the board. */
static void
cell_ways(int c, int *in, int *out, int *edge)
{
    int x, y, b, bw;

    x = c % board_w;
    y = c / board_w;
    bw = board_w / 2;
    b = y / 2 * bw + x / 2;
    if (y % 2 == 0 && x % 2 == 0) {
        *in = dir_down;
        *out = dir_left;
        *edge = x > 0 ? b - 1 : -1;
    } else if (y % 2 == 0) {
        *in = dir_left;
        *out = dir_up;
        *edge = y > 0 ? nblk + b - bw : -1;
    } else if (x % 2 == 0) {
        *in = dir_right;
        *out = dir_down;
        *edge = y < board_h - 1 ? nblk + b : -1;
    } else {
        *in = dir_up;
        *out = dir_right;
        *edge = x < board_w - 1 ? b : -1;
    }
}
/* union-find over blocks, by size and without path compression, so
 * that the planner can undo joins */
static int
uf_find(int a)
{
    while (uf[a] != a) {
        a = uf[a];
    }
    return a;
}

static int
edge_valid(int e)
{
    int bw;

    bw = board_w / 2;
    if (e < nblk) {
        return e % bw != bw - 1;
    }
    return e - nblk < nblk - bw;
}

static void
edge_blocks(int e, int *a, int *b)
{
    if (e < nblk) {
        *a = e;
        *b = e + 1;
    } else {
        *a = e - nblk;
        *b = *a + board_w / 2;
    }
}

/* put edge e in the tree if it joins two parts; 1 if it did */
static int
uf_join(int e)
{
    int a, b, t;

    edge_blocks(e, &a, &b);
    a = uf_find(a);
    b = uf_find(b);
    if (a == b) {
        return 0;
    }
    if (uf_size[a] > uf_size[b]) {
        t = a;
        a = b;
        b = t;
    }
    uf[a] = b;
    uf_size[b] += uf_size[a];
    e_tree[e] = 1;
    log_root[log_n] = a;
    return 1;
}

/* Can cell c go on in direction d, given what is wanted so far? The
 * tree edge it needs must not be unwanted, nor join blocks that are
 * joined already (the tree would get a loop). Sets *e and *want. */
static int
way_ok(int c, int d, int *e, int *want)
{
    int in, out, a, b;

    cell_ways(c, &in, &out, e);
    if (d == in) {
        *want = 2;
        return *e < 0 || e_want[*e] != 1;
    }
    if (d != out || *e < 0) {
        return 0;
    }
    *want = 1;
    if (e_want[*e] == 1) {
        return 1;
    }
    if (e_want[*e] == 2) {
        return 0;
    }
    edge_blocks(*e, &a, &b);
    return uf_find(a) != uf_find(b);
}

/* Go on from cell c in direction d: note what the tree needs, in the
 * log so that it can be undone. */
static int
way_take(int c, int d)
{
    int e, want;

    if (!way_ok(c, d, &e, &want)) {
        return 0;
    }
    if (e >= 0 && !e_want[e]) {
        e_want[e] = want;
        log_edge[log_n] = e;
        log_root[log_n] = -1;
        if (want == 1) {
            uf_join(e);
        }
        log_n++;
    }
    return 1;
}

/* undo the log back to n entries */
static void
log_undo(int n)
{
    int e, a, b;

    while (log_n > n) {
        log_n--;
        e = log_edge[log_n];
        a = log_root[log_n];
        e_want[e] = 0;
        if (a >= 0) {
            b = uf[a];
            uf_size[b] -= uf_size[a];
            uf[a] = a;
            e_tree[e] = 0;
        }
    }
}

/* The edge between block b and its neighbour k (0 right, 1 below,
 * 2 left, 3 above) into *f and the neighbour into *n; 0 when there
 * is none, or it is unwanted, or it is edge skip. */
static int
block_edge(int b, int k, int skip, int *f, int *n)
{
    int bw;

    bw = board_w / 2;
    if (k == 0) {
        *f = b;
        *n = b + 1;
        if (b % bw == bw - 1) {
            return 0;
        }
    } else if (k == 1) {
        *f = nblk + b;
        *n = b + bw;
    } else if (k == 2) {
        *f = b - 1;
        *n = b - 1;
        if (b % bw == 0) {
            return 0;
        }
    } else {
        *f = nblk + b - bw;
        *n = b - bw;
    }
    return *n >= 0 && *n < nblk && *f != skip && e_want[*f] != 2;
}

/* Mark the bridges among the edges not unwanted: the ones that can't
 * be unwanted as well, or the blocks fall in two. Tarjan's way, with
 * blk_queue as the stack instead of recursion. */
static void
bridges(void)
{
    int b, p, k, f, n, sp, t;

    memset(e_bridge, 0, 2 * nblk);
    for (b = 0; b < nblk; b++) {
        b_disc[b] = -1;
    }
    b_disc[0] = 0;
    b_low[0] = 0;
    b_pe[0] = -1;
    blk_mark[0] = 0;
    t = 1;
    blk_queue[0] = 0;
    sp = 1;
    while (sp > 0) {
        b = blk_queue[sp - 1];
        if (blk_mark[b] < 4) {
            k = blk_mark[b];
            blk_mark[b]++;
            if (!block_edge(b, k, -1, &f, &n) || f == b_pe[b]) {
                continue;
            }
            if (b_disc[n] < 0) {
                b_disc[n] = t;
                b_low[n] = t;
                t++;
                b_pe[n] = f;
                blk_mark[n] = 0;
                blk_queue[sp] = n;
                sp++;
            } else if (b_disc[n] < b_low[b]) {
                b_low[b] = b_disc[n];
            }
            continue;
        }
        sp--;
        if (sp > 0) {
            p = blk_queue[sp - 1];
            if (b_low[b] < b_low[p]) {
                b_low[p] = b_low[b];
            }
            if (b_low[b] > b_disc[p]) {
                e_bridge[b_pe[b]] = 1;
            }
        }
    }
}

/* the rest of the tree: any edge not unwanted that joins two parts;
 * 1 when all blocks are joined */
static int
tree_make(void)
{
    int b, e, k, n;

    n = 2 * nblk;
    b = rng_next() % n;
    for (k = 0; k < n; k++) {
        e = (b + k) % n;
        if (!e_want[e] && edge_valid(e)) {
            uf_join(e);
        }
    }
    return uf_size[uf_find(0)] == nblk;
}

/* the cycle round the tree into t_next; it becomes the cycle when the
 * snake lies on it in order */
static int
cycle_commit(void)
{
    int c, in, out, e, i, j, a, b;
    int *tmp;

    for (c = 0; c < body_cap; c++) {
        cell_ways(c, &in, &out, &e);
        t_next[c] = nbr(c, e >= 0 && e_tree[e] ? out : in);
    }
    i = (head - length + 1 + body_cap) % body_cap;
    for (j = 0; j < length - 1; j++) {
        a = body[i];
        i = (i + 1) % body_cap;
        b = body[i];
        if (t_next[a] != b) {
            return 0;
        }
    }
    tmp = cyc_next;
    cyc_next = t_next;
    t_next = tmp;
    a = tail_cell();
    for (j = 0; j < body_cap; j++) {
        cyc_ord[a] = j;
        a = cyc_next[a];
    }
    ai_have = 1;
    return 1;
}

/* the order to try the ways on from cell c: nearer the food first,
 * then near the line (line set) or straight on */
static void
way_order(int c, int prev, int line, int *ord)
{
    long long k[4];
    int i, j, t, n, m;

    for (i = 0; i < 4; i++) {
        ord[i] = i;
        n = nbr(c, i);
        if (n < 0 || ai_dist[n] < 0) {
            k[i] = -1;
            continue;
        }
        m = line ? off_line(n, ai_from, food) : i != prev;
        k[i] = (long long)ai_dist[n] * (4 * body_cap + 1) + m;
    }
    for (i = 1; i < 4; i++) {
        for (j = i; j > 0 && (k[ord[j - 1]] < 0 ||
            (k[ord[j]] >= 0 && k[ord[j]] < k[ord[j - 1]])); j--)
        {
            t = ord[j];
            ord[j] = ord[j - 1];
            ord[j - 1] = t;
        }
    }
    for (i = 0; i < 4; i++) {
        if (k[ord[i]] < 0) {
            ord[i] = -1;
        }
    }
}

/* Plan the way from the head to the food: off the snake, along ways
 * the cycles allow, each nearer the food, with a tree that agrees
 * with it all; trying the best ways first, and going back when one
 * leads nowhere, within a budget. */
static int
plan_way(int line)
{
    int qh, qt, c, d, p, n, e, want, sp, top, budget, cut;

    for (c = 0; c < body_cap; c++) {
        ai_dist[c] = -1;
    }
    ai_dist[food] = 0;
    ai_queue[0] = food;
    qh = 0;
    qt = 1;
    while (qh < qt) {
        c = ai_queue[qh];
        qh++;
        for (d = 0; d < 4; d++) {
            p = nbr(c, d);
            if (p < 0 || grid[p] || ai_dist[p] >= 0 ||
                !way_ok(p, (d + 2) % 4, &e, &want))
            {
                continue;
            }
            ai_dist[p] = ai_dist[c] + 1;
            ai_queue[qt] = p;
            qt++;
        }
    }
    /* no way at all? and a budget for going back, as it costs time */
    budget = -1;
    for (d = 0; d < 4; d++) {
        n = nbr(body[head], d);
        if (n >= 0 && ai_dist[n] >= 0 &&
            (budget < 0 || ai_dist[n] < budget))
        {
            budget = ai_dist[n];
        }
    }
    if (budget < 0) {
        return 0;
    }
    budget = 4 * budget + 64;
    bridges();
    sp = 0;
    f_cell[0] = body[head];
    f_try[0] = 0;
    f_log[0] = log_n;
    way_order(body[head], dir, line, f_ord);
    sp = 1;
    while (sp > 0) {
        c = f_cell[sp - 1];
        if (c == food) {
            ai_plan_n = sp - 1;
            return 1;
        }
        if (f_try[sp - 1] >= 4 || budget <= 0) {
            sp--;
            log_undo(f_log[sp]);
            continue;
        }
        d = f_ord[(sp - 1) * 4 + f_try[sp - 1]];
        f_try[sp - 1]++;
        if (d < 0) {
            continue;
        }
        n = nbr(c, d);
        top = sp == 1 ? body_cap : ai_dist[c];
        if (ai_dist[n] >= top || (ai_limit > 0 &&
            sp + ai_dist[n] > ai_limit) || !way_ok(c, d, &e, &want))
        {
            continue;
        }
        budget--;
        /* would it cut the blocks in two? then no tree (bridges are
         * found once for the body; the rest the tree shows) */
        cut = want == 2 && e >= 0 && !e_want[e] && e_bridge[e];
        if (cut) {
            continue;
        }
        f_log[sp] = log_n;
        way_take(c, d);
        f_cell[sp] = n;
        f_try[sp] = 0;
        way_order(n, d, line, f_ord + sp * 4);
        sp++;
    }
    return 0;
}

/* A new cycle with the body on it and, with way set (1 near the line,
 * 2 fewest turns), a way to the food. */
static int
try_cycle(int way)
{
    int i, j, a, b, n;

    n = 2 * nblk;
    for (j = 0; j < n; j++) {
        e_want[j] = 0;
        e_tree[j] = 0;
    }
    for (b = 0; b < nblk; b++) {
        uf[b] = b;
        uf_size[b] = 1;
    }
    log_n = 0;
    i = (head - length + 1 + body_cap) % body_cap;
    for (j = 0; j < length - 1; j++) {
        a = body[i];
        i = (i + 1) % body_cap;
        b = body[i];
        if (!way_take(a, dir_between(a, b))) {
            return 0;
        }
    }
    if (way && !plan_way(way == 1)) {
        return 0;
    }
    if (!tree_make()) {
        return 0;
    }
    return cycle_commit();
}

/* does the snake lie on the cycle, in order? */
static int
ai_ordered(void)
{
    long long sum;
    int i, j, a, b;

    if (!ai_have) {
        return 0;
    }
    sum = 0;
    i = (head - length + 1 + body_cap) % body_cap;
    for (j = 0; j < length - 1; j++) {
        a = body[i];
        i = (i + 1) % body_cap;
        b = body[i];
        sum += cdist(a, b);
        if (sum > body_cap) {
            return 0;
        }
    }
    sum += cdist(body[head], tail_cell());
    return sum == body_cap;
}

/* breadth first from src over free cells */
static void
bfs(int src)
{
    int qh, qt, c, d, n;

    for (c = 0; c < body_cap; c++) {
        ai_dist[c] = -1;
    }
    ai_dist[src] = 0;
    ai_queue[0] = src;
    qh = 0;
    qt = 1;
    while (qh < qt) {
        c = ai_queue[qh];
        qh++;
        for (d = 0; d < 4; d++) {
            n = nbr(c, d);
            if (n < 0 || ai_dist[n] >= 0 || grid[n]) {
                continue;
            }
            ai_dist[n] = ai_dist[c] + 1;
            ai_queue[qt] = n;
            qt++;
        }
    }
}

/* After moving the head to n, can it still reach its tail? */
static int
safe_after(int n, int tl)
{
    int eat, ntl, ok, d, m;

    eat = n == food;
    ntl = eat ? tl : body[(head - length + 2 + body_cap) % body_cap];
    if (!eat) {
        grid[tl] = 0;
    }
    grid[n] = 1;
    bfs(ntl);
    ok = 0;
    for (d = 0; d < 4; d++) {
        m = nbr(n, d);
        if (m >= 0 && ai_dist[m] >= 0) {
            ok = 1;
        }
    }
    grid[n] = 0;
    if (!eat) {
        grid[tl] = 1;
    }
    return ok;
}

/* No cycle fits the snake (a person handed it over in a tangle): a
 * move that keeps the way to the tail open, one the cycles allow if it
 * can (so that the snake comes to fit one), nearest the food. */
static int
ai_rescue(int hd, int tl)
{
    int d, n, pick, m, pm, in, out, e;

    cell_ways(hd, &in, &out, &e);
    pick = -1;
    pm = 0;
    for (d = 0; d < 4; d++) {
        n = nbr(hd, d);
        if (n < 0 || (grid[n] && n != tl)) {
            continue;
        }
        m = manhattan(n, food);
        if (d != in && (d != out || e < 0)) {
            m += body_cap;
        }
        if (safe_after(n, tl)) {
            m -= 4 * body_cap;
        }
        if (pick < 0 || m < pm) {
            pick = d;
            pm = m;
        }
    }
    return pick < 0 ? dir : pick;
}

static int
ai_dir(void)
{
    int hd, ordered;

    if (food < 0) {
        return dir;
    }
    hd = body[head];
    if (food != ai_food) {
        ai_food = food;
        ai_from = hd;
    }
    ordered = ai_ordered();
    /* a new cycle only when it gets there sooner, so that the food
     * always gets nearer */
    ai_limit = ordered ? cdist(hd, food) : 0;
    if (try_cycle(1) || try_cycle(2)) {
        return dir_between(hd, cyc_next[hd]);
    }
    ai_limit = 0;
    if (ordered) {
        return dir_between(hd, cyc_next[hd]);
    }
    if (try_cycle(0)) {
        return dir_between(hd, cyc_next[hd]);
    }
    return ai_rescue(hd, tail_cell());
}

static void *
board_mem(int n, int size)
{
    void *p;

    p = malloc((n > 0 ? n : 1) * size);
    if (!p) {
        mem_failed = 1;
    }
    return p;
}

void
game_free(void)
{
    free(grid);
    free(body);
    free(cyc_next);
    free(cyc_ord);
    free(t_next);
    free(ai_dist);
    free(ai_queue);
    free(uf);
    free(uf_size);
    free(e_want);
    free(e_tree);
    free(blk_mark);
    free(blk_queue);
    free(b_disc);
    free(b_low);
    free(b_pe);
    free(e_bridge);
    free(log_edge);
    free(log_root);
    free(f_cell);
    free(f_try);
    free(f_log);
    free(f_ord);
    grid = 0;
    body = 0;
    cyc_next = 0;
    cyc_ord = 0;
    t_next = 0;
    ai_dist = 0;
    ai_queue = 0;
    uf = 0;
    uf_size = 0;
    e_want = 0;
    e_tree = 0;
    blk_mark = 0;
    blk_queue = 0;
    b_disc = 0;
    b_low = 0;
    b_pe = 0;
    e_bridge = 0;
    log_edge = 0;
    log_root = 0;
    f_cell = 0;
    f_try = 0;
    f_log = 0;
    f_ord = 0;
}

static int
board_alloc(void)
{
    int n;

    game_free();
    mem_failed = 0;
    body_cap = board_w * board_h;
    nblk = body_cap / 4;
    n = body_cap + 1;
    grid = board_mem(n, 1);
    body = board_mem(n, sizeof *body);
    cyc_next = board_mem(n, sizeof *cyc_next);
    cyc_ord = board_mem(n, sizeof *cyc_ord);
    t_next = board_mem(n, sizeof *t_next);
    ai_dist = board_mem(n, sizeof *ai_dist);
    ai_queue = board_mem(n, sizeof *ai_queue);
    uf = board_mem(nblk, sizeof *uf);
    uf_size = board_mem(nblk, sizeof *uf_size);
    e_want = board_mem(2 * nblk, 1);
    e_tree = board_mem(2 * nblk, 1);
    blk_mark = board_mem(nblk, 1);
    blk_queue = board_mem(nblk, sizeof *blk_queue);
    b_disc = board_mem(nblk, sizeof *b_disc);
    b_low = board_mem(nblk, sizeof *b_low);
    b_pe = board_mem(nblk, sizeof *b_pe);
    e_bridge = board_mem(2 * nblk, 1);
    log_edge = board_mem(2 * n, sizeof *log_edge);
    log_root = board_mem(2 * n, sizeof *log_root);
    f_cell = board_mem(n, sizeof *f_cell);
    f_try = board_mem(n, sizeof *f_try);
    f_log = board_mem(n, sizeof *f_log);
    f_ord = board_mem(4 * n, sizeof *f_ord);
    if (mem_failed) {
        game_free();
        return -1;
    }
    memset(grid, 0, body_cap);
    return 0;
}


void
ai_reset(void)
{
    ai_food = -1;
}

int
game_new(int w, int h)
{
    int i, x, y;

    /* the autopilot's cycles are made of 2x2 blocks */
    board_w = w < 8 ? 8 : w - w % 2;
    board_h = h < 2 ? 2 : h - h % 2;
    if (board_alloc() < 0) {
        return -1;
    }
    /* horizontal snake in the middle, heading right, on an odd row
     * (where the autopilot's cycles go right) */
    y = board_h / 2;
    y -= 1 - y % 2;
    x = board_w / 2 - START_LEN / 2;
    for (i = 0; i < START_LEN; i++) {
        body[i] = y * board_w + x + i;
        grid[body[i]] = 1;
    }
    head = START_LEN - 1;
    length = START_LEN;
    dir = dir_right;
    queued = 0;
    ai_have = 0;
    ai_food = -1;
    food_place();
    return 0;
}

void
game_turn(int d)
{
    int last;

    last = queued ? queue[queued - 1] : dir;
    if (d == last || d == (last + 2) % 4 || queued == 2) {
        return;
    }
    queue[queued] = d;
    queued++;
}

int
game_step(void)
{
    int x, y, cell, tail, old, eat;

    if (ai_on) {
        dir = ai_dir();
        queued = 0;
    } else if (queued) {
        dir = queue[0];
        queue[0] = queue[1];
        queued--;
    }
    x = body[head] % board_w + dir_dx[dir];
    y = body[head] / board_w + dir_dy[dir];
    if (x < 0 || x >= board_w || y < 0 || y >= board_h) {
        return step_over;
    }
    cell = y * board_w + x;
    tail = body[(head - length + 1 + body_cap) % body_cap];
    eat = cell == food;
    if (grid[cell] && cell != tail) {
        return step_over;
    }
    if (eat) {
        food = -1;
    } else {
        grid[tail] = 0;
        game_cell(tail);
    }
    old = body[head];
    head = (head + 1) % body_cap;
    body[head] = cell;
    grid[cell] = 1;
    game_cell(old);
    game_cell(cell);
    if (!eat) {
        return step_moved;
    }
    length++;
    food_place();
    if (food < 0) {
        return step_won;
    }
    game_cell(food);
    return step_ate;
}

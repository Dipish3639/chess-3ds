#include "ai.h"

#include <stdlib.h>

#ifdef __3DS__
#include <3ds.h>
static uint64_t now_ms(void) { return osGetTime(); }
#else
#include <time.h>
static uint64_t now_ms(void) { return (uint64_t)clock() * 1000 / CLOCKS_PER_SEC; }
#endif

#define INF        1000000
#define MATE_SCORE 30000
#define MAX_QDEPTH 6

static const int value[7] = {0, 100, 320, 330, 500, 900, 0};

// Piece-square tables from white's point of view, index a8..h8 first row
// (i.e. table[(7 - rank) * 8 + file]).
static const int pst_pawn[64] = {
	 0,  0,  0,  0,  0,  0,  0,  0,
	50, 50, 50, 50, 50, 50, 50, 50,
	10, 10, 20, 30, 30, 20, 10, 10,
	 5,  5, 10, 25, 25, 10,  5,  5,
	 0,  0,  0, 20, 20,  0,  0,  0,
	 5, -5,-10,  0,  0,-10, -5,  5,
	 5, 10, 10,-20,-20, 10, 10,  5,
	 0,  0,  0,  0,  0,  0,  0,  0,
};
static const int pst_knight[64] = {
	-50,-40,-30,-30,-30,-30,-40,-50,
	-40,-20,  0,  0,  0,  0,-20,-40,
	-30,  0, 10, 15, 15, 10,  0,-30,
	-30,  5, 15, 20, 20, 15,  5,-30,
	-30,  0, 15, 20, 20, 15,  0,-30,
	-30,  5, 10, 15, 15, 10,  5,-30,
	-40,-20,  0,  5,  5,  0,-20,-40,
	-50,-40,-30,-30,-30,-30,-40,-50,
};
static const int pst_bishop[64] = {
	-20,-10,-10,-10,-10,-10,-10,-20,
	-10,  0,  0,  0,  0,  0,  0,-10,
	-10,  0,  5, 10, 10,  5,  0,-10,
	-10,  5,  5, 10, 10,  5,  5,-10,
	-10,  0, 10, 10, 10, 10,  0,-10,
	-10, 10, 10, 10, 10, 10, 10,-10,
	-10,  5,  0,  0,  0,  0,  5,-10,
	-20,-10,-10,-10,-10,-10,-10,-20,
};
static const int pst_rook[64] = {
	 0,  0,  0,  0,  0,  0,  0,  0,
	 5, 10, 10, 10, 10, 10, 10,  5,
	-5,  0,  0,  0,  0,  0,  0, -5,
	-5,  0,  0,  0,  0,  0,  0, -5,
	-5,  0,  0,  0,  0,  0,  0, -5,
	-5,  0,  0,  0,  0,  0,  0, -5,
	-5,  0,  0,  0,  0,  0,  0, -5,
	 0,  0,  0,  5,  5,  0,  0,  0,
};
static const int pst_queen[64] = {
	-20,-10,-10, -5, -5,-10,-10,-20,
	-10,  0,  0,  0,  0,  0,  0,-10,
	-10,  0,  5,  5,  5,  5,  0,-10,
	 -5,  0,  5,  5,  5,  5,  0, -5,
	  0,  0,  5,  5,  5,  5,  0, -5,
	-10,  5,  5,  5,  5,  5,  0,-10,
	-10,  0,  5,  0,  0,  0,  0,-10,
	-20,-10,-10, -5, -5,-10,-10,-20,
};
static const int pst_king[64] = {
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-20,-30,-30,-40,-40,-30,-30,-20,
	-10,-20,-20,-20,-20,-20,-20,-10,
	 20, 20,  0,  0,  0,  0, 20, 20,
	 20, 30, 10,  0,  0, 10, 30, 20,
};
static const int pst_king_end[64] = {
	-50,-40,-30,-20,-20,-30,-40,-50,
	-30,-20,-10,  0,  0,-10,-20,-30,
	-30,-10, 20, 30, 30, 20,-10,-30,
	-30,-10, 30, 40, 40, 30,-10,-30,
	-30,-10, 30, 40, 40, 30,-10,-30,
	-30,-10, 20, 30, 30, 20,-10,-30,
	-30,-30,  0,  0,  0,  0,-30,-30,
	-50,-30,-30,-30,-30,-30,-30,-50,
};
static const int *pst[7] = {0, pst_pawn, pst_knight, pst_bishop, pst_rook, pst_queen, pst_king};

static volatile bool *g_abort;
static unsigned g_nodes;
static uint64_t g_deadline; // 0 = no time limit
static bool g_out_of_time;

static bool stopped(void)
{
	if (*g_abort || g_out_of_time) return true;
	if (g_deadline && (g_nodes & 1023) == 0 && now_ms() >= g_deadline) g_out_of_time = true;
	return g_out_of_time;
}

// Static evaluation from the side to move's point of view.
static int evaluate(const Position *p)
{
	int score = 0, heavy = 0;
	for (int s = 0; s < 64; s++) {
		int t = TYPE_OF(p->sq[s]);
		if (t == QUEEN) heavy += 2;
		else if (t == ROOK || t == BISHOP || t == KNIGHT) heavy += 1;
	}
	bool endgame = heavy <= 6;

	for (int s = 0; s < 64; s++) {
		uint8_t pc = p->sq[s];
		if (!pc) continue;
		int t = TYPE_OF(pc), c = COLOR_OF(pc);
		int idx = c == WHITE ? (7 - RANK_OF(s)) * 8 + FILE_OF(s) : RANK_OF(s) * 8 + FILE_OF(s);
		const int *table = (t == KING && endgame) ? pst_king_end : pst[t];
		int v = value[t] + table[idx];
		score += c == WHITE ? v : -v;
	}
	return p->side == WHITE ? score : -score;
}

static int move_order_score(const Position *p, const Move *m)
{
	int s = 0;
	if (m->flags & MF_CAPTURE) {
		int victim = (m->flags & MF_EP) ? PAWN : TYPE_OF(p->sq[m->to]);
		s += 10 * value[victim] - value[TYPE_OF(p->sq[m->from])] + 10000;
	}
	if (m->promo) s += value[m->promo] + 9000;
	return s;
}

static void sort_moves(const Position *p, Move *moves, int n)
{
	int scores[MAX_MOVES];
	for (int i = 0; i < n; i++) scores[i] = move_order_score(p, &moves[i]);
	for (int i = 1; i < n; i++) {
		Move m = moves[i];
		int sc = scores[i], j = i - 1;
		while (j >= 0 && scores[j] < sc) {
			moves[j + 1] = moves[j];
			scores[j + 1] = scores[j];
			j--;
		}
		moves[j + 1] = m;
		scores[j + 1] = sc;
	}
}

static int quiesce(const Position *p, int alpha, int beta, int qdepth)
{
	g_nodes++;
	int stand = evaluate(p);
	if (stand >= beta) return beta;
	if (stand > alpha) alpha = stand;
	if (qdepth >= MAX_QDEPTH) return alpha;

	Move moves[MAX_MOVES];
	int n = gen_pseudo(p, moves, true);
	sort_moves(p, moves, n);
	for (int i = 0; i < n; i++) {
		Position np;
		make_move(p, moves[i], &np);
		if (in_check(&np, p->side)) continue;
		int score = -quiesce(&np, -beta, -alpha, qdepth + 1);
		if (score >= beta) return beta;
		if (score > alpha) alpha = score;
	}
	return alpha;
}

static int search(const Position *p, int depth, int alpha, int beta, int ply)
{
	if (stopped()) return 0;
	if (depth <= 0) return quiesce(p, alpha, beta, 0);
	g_nodes++;

	if (ply > 0 && (p->halfmove >= 100 || insufficient_material(p))) return 0;

	Move moves[MAX_MOVES];
	int n = gen_pseudo(p, moves, false);
	sort_moves(p, moves, n);
	int legal = 0;
	for (int i = 0; i < n; i++) {
		Position np;
		make_move(p, moves[i], &np);
		if (in_check(&np, p->side)) continue;
		legal++;
		int score = -search(&np, depth - 1, -beta, -alpha, ply + 1);
		if (score >= beta) return beta;
		if (score > alpha) alpha = score;
	}
	if (!legal) return in_check(p, p->side) ? -MATE_SCORE + ply : 0;
	return alpha;
}

bool ai_pick_move(const Position *p, int level, volatile bool *abort, Move *out)
{
	static const int depth_for_level[3] = {1, 3, 4};
	static const int noise_for_level[3] = {80, 12, 0};
	g_abort = abort;
	g_nodes = 0;
	g_deadline = 0;
	g_out_of_time = false;

	Move moves[MAX_MOVES];
	int n = gen_legal(p, moves);
	if (n == 0) return false;
	sort_moves(p, moves, n);

	int depth = depth_for_level[level];
	int noise = noise_for_level[level];
	int best = -INF;
	int best_idx = 0;
	for (int i = 0; i < n; i++) {
		Position np;
		make_move(p, moves[i], &np);
		// Anything that can't beat best even with maximum noise is irrelevant,
		// so the window can be narrowed to (best - noise, INF).
		int alpha = best == -INF ? -INF : best - noise;
		int score = -search(&np, depth - 1, -INF, -alpha, 1);
		if (*abort) break;
		if (noise) score += rand() % noise;
		if (score > best) {
			best = score;
			best_idx = i;
		}
	}
	*out = moves[best_idx];
	return true;
}

bool ai_best_move(const Position *p, int max_ms, volatile bool *abort, Move *out)
{
	g_abort = abort;
	g_nodes = 0;
	g_out_of_time = false;
	g_deadline = now_ms() + max_ms;

	Move moves[MAX_MOVES];
	int n = gen_legal(p, moves);
	if (n == 0) return false;
	sort_moves(p, moves, n);
	*out = moves[0];
	if (n == 1) return true;

	// Iterative deepening: each finished depth gives a better answer, and the
	// best move so far is searched first next time. Stop when time runs out.
	for (int depth = 1; depth <= 64; depth++) {
		int best = -INF, best_idx = -1;
		for (int i = 0; i < n; i++) {
			Position np;
			make_move(p, moves[i], &np);
			int score = -search(&np, depth - 1, -INF, -best, 1);
			if (stopped()) break;
			if (score > best) {
				best = score;
				best_idx = i;
			}
		}
		if (stopped()) {
			// A partial iteration is still usable if it found something better
			// than the previous depth's best (which was searched first).
			if (best_idx > 0) *out = moves[best_idx];
			break;
		}
		*out = moves[best_idx];
		Move m = moves[best_idx];
		for (int i = best_idx; i > 0; i--) moves[i] = moves[i - 1];
		moves[0] = m;
		if (best >= MATE_SCORE - 64) break; // found a forced mate
	}
	return true;
}

// Host-side move generator check: gcc -O2 -Isource tests/perft.c source/chess.c && ./a.out
#include <stdio.h>
#include <string.h>
#include "chess.h"

static unsigned long long perft(const Position *p, int depth)
{
	Move m[MAX_MOVES];
	int n = gen_legal(p, m);
	if (depth == 1) return n;
	unsigned long long total = 0;
	for (int i = 0; i < n; i++) {
		Position np;
		make_move(p, m[i], &np);
		total += perft(&np, depth - 1);
	}
	return total;
}

// Minimal FEN loader for test positions
static void load_fen(Position *p, const char *fen)
{
	memset(p, 0, sizeof(*p));
	int r = 7, f = 0;
	const char *c = fen;
	for (; *c && *c != ' '; c++) {
		if (*c == '/') { r--; f = 0; continue; }
		if (*c >= '1' && *c <= '8') { f += *c - '0'; continue; }
		const char *types = "pnbrqk";
		int color = (*c >= 'a') ? BLACK : WHITE;
		char lc = *c | 32;
		int t = (int)(strchr(types, lc) - types) + 1;
		p->sq[SQ(f, r)] = PIECE(color, t);
		if (t == KING) p->king[color] = SQ(f, r);
		f++;
	}
	c++;
	p->side = *c == 'w' ? WHITE : BLACK;
	c += 2;
	for (; *c != ' '; c++) {
		if (*c == 'K') p->castle |= CASTLE_WK;
		if (*c == 'Q') p->castle |= CASTLE_WQ;
		if (*c == 'k') p->castle |= CASTLE_BK;
		if (*c == 'q') p->castle |= CASTLE_BQ;
	}
	c++;
	p->ep = *c == '-' ? -1 : SQ(c[0] - 'a', c[1] - '1');
	p->fullmove = 1;
}

int main(void)
{
	struct { const char *fen; int depth; unsigned long long expect; } tests[] = {
		{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
		{"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},
		{"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
		{"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
		{"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
	};
	int fails = 0;
	for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		Position p;
		load_fen(&p, tests[i].fen);
		unsigned long long got = perft(&p, tests[i].depth);
		printf("%s perft(%d) = %llu (expected %llu)\n", got == tests[i].expect ? "OK  " : "FAIL",
		       tests[i].depth, got, tests[i].expect);
		if (got != tests[i].expect) fails++;
	}
	Position p;
	pos_init(&p);
	char san[16];
	Move m = {SQ(6, 0), SQ(5, 2), 0, 0};
	move_to_san(&p, m, san);
	printf("SAN Ng1-f3: %s\n", san);
	return fails;
}

#include "chess.h"

#include <stdio.h>
#include <string.h>

static const int knight_d[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
static const int king_d[8][2]   = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
static const int bishop_d[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
static const int rook_d[4][2]   = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

static inline bool on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

void pos_init(Position *p)
{
	static const uint8_t back[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
	memset(p, 0, sizeof(*p));
	for (int f = 0; f < 8; f++) {
		p->sq[SQ(f, 0)] = PIECE(WHITE, back[f]);
		p->sq[SQ(f, 1)] = PIECE(WHITE, PAWN);
		p->sq[SQ(f, 6)] = PIECE(BLACK, PAWN);
		p->sq[SQ(f, 7)] = PIECE(BLACK, back[f]);
	}
	p->side = WHITE;
	p->castle = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ;
	p->ep = -1;
	p->king[WHITE] = SQ(4, 0);
	p->king[BLACK] = SQ(4, 7);
	p->halfmove = 0;
	p->fullmove = 1;
}

bool sq_attacked(const Position *p, int sq, int by)
{
	int f = FILE_OF(sq), r = RANK_OF(sq);

	// Pawns
	int pr = by == WHITE ? r - 1 : r + 1;
	uint8_t pawn = PIECE(by, PAWN);
	if (pr >= 0 && pr < 8) {
		if (f > 0 && p->sq[SQ(f - 1, pr)] == pawn) return true;
		if (f < 7 && p->sq[SQ(f + 1, pr)] == pawn) return true;
	}

	// Knights
	uint8_t knight = PIECE(by, KNIGHT);
	for (int i = 0; i < 8; i++) {
		int nf = f + knight_d[i][0], nr = r + knight_d[i][1];
		if (on_board(nf, nr) && p->sq[SQ(nf, nr)] == knight) return true;
	}

	// King
	uint8_t king = PIECE(by, KING);
	for (int i = 0; i < 8; i++) {
		int nf = f + king_d[i][0], nr = r + king_d[i][1];
		if (on_board(nf, nr) && p->sq[SQ(nf, nr)] == king) return true;
	}

	// Sliders
	uint8_t queen = PIECE(by, QUEEN);
	uint8_t bishop = PIECE(by, BISHOP);
	uint8_t rook = PIECE(by, ROOK);
	for (int i = 0; i < 4; i++) {
		int nf = f + bishop_d[i][0], nr = r + bishop_d[i][1];
		while (on_board(nf, nr)) {
			uint8_t pc = p->sq[SQ(nf, nr)];
			if (pc) {
				if (pc == bishop || pc == queen) return true;
				break;
			}
			nf += bishop_d[i][0];
			nr += bishop_d[i][1];
		}
		nf = f + rook_d[i][0];
		nr = r + rook_d[i][1];
		while (on_board(nf, nr)) {
			uint8_t pc = p->sq[SQ(nf, nr)];
			if (pc) {
				if (pc == rook || pc == queen) return true;
				break;
			}
			nf += rook_d[i][0];
			nr += rook_d[i][1];
		}
	}
	return false;
}

bool in_check(const Position *p, int side)
{
	return sq_attacked(p, p->king[side], side ^ 1);
}

static inline void add(Move *out, int *n, int from, int to, int promo, int flags)
{
	out[*n].from = from;
	out[*n].to = to;
	out[*n].promo = promo;
	out[*n].flags = flags;
	(*n)++;
}

static void add_pawn(Move *out, int *n, int from, int to, int flags)
{
	int r = RANK_OF(to);
	if (r == 0 || r == 7) {
		add(out, n, from, to, QUEEN, flags | MF_PROMO);
		add(out, n, from, to, ROOK, flags | MF_PROMO);
		add(out, n, from, to, BISHOP, flags | MF_PROMO);
		add(out, n, from, to, KNIGHT, flags | MF_PROMO);
	} else {
		add(out, n, from, to, 0, flags);
	}
}

int gen_pseudo(const Position *p, Move *out, bool captures_only)
{
	int n = 0;
	int us = p->side, them = us ^ 1;

	for (int from = 0; from < 64; from++) {
		uint8_t pc = p->sq[from];
		if (!pc || COLOR_OF(pc) != us) continue;
		int f = FILE_OF(from), r = RANK_OF(from);

		switch (TYPE_OF(pc)) {
		case PAWN: {
			int dir = us == WHITE ? 1 : -1;
			int start = us == WHITE ? 1 : 6;
			int nr = r + dir;
			if (nr < 0 || nr > 7) break;
			// Pushes (promotions count as "captures" for quiescence purposes)
			if (!p->sq[SQ(f, nr)] && (!captures_only || nr == 0 || nr == 7)) {
				add_pawn(out, &n, from, SQ(f, nr), 0);
				if (r == start && !p->sq[SQ(f, nr + dir)] && !captures_only)
					add(out, &n, from, SQ(f, nr + dir), 0, MF_DOUBLE);
			}
			for (int df = -1; df <= 1; df += 2) {
				int nf = f + df;
				if (nf < 0 || nf > 7) continue;
				int to = SQ(nf, nr);
				uint8_t t = p->sq[to];
				if (t && COLOR_OF(t) == them)
					add_pawn(out, &n, from, to, MF_CAPTURE);
				else if (to == p->ep)
					add(out, &n, from, to, 0, MF_CAPTURE | MF_EP);
			}
			break;
		}
		case KNIGHT:
		case KING: {
			const int (*d)[2] = TYPE_OF(pc) == KNIGHT ? knight_d : king_d;
			for (int i = 0; i < 8; i++) {
				int nf = f + d[i][0], nr = r + d[i][1];
				if (!on_board(nf, nr)) continue;
				uint8_t t = p->sq[SQ(nf, nr)];
				if (!t) {
					if (!captures_only) add(out, &n, from, SQ(nf, nr), 0, 0);
				} else if (COLOR_OF(t) == them) {
					add(out, &n, from, SQ(nf, nr), 0, MF_CAPTURE);
				}
			}
			if (TYPE_OF(pc) == KING && !captures_only) {
				int home = us == WHITE ? 0 : 7;
				int ks = us == WHITE ? CASTLE_WK : CASTLE_BK;
				int qs = us == WHITE ? CASTLE_WQ : CASTLE_BQ;
				if (from == SQ(4, home) && (p->castle & (ks | qs)) && !sq_attacked(p, from, them)) {
					if ((p->castle & ks) && !p->sq[SQ(5, home)] && !p->sq[SQ(6, home)] &&
					    p->sq[SQ(7, home)] == PIECE(us, ROOK) &&
					    !sq_attacked(p, SQ(5, home), them) && !sq_attacked(p, SQ(6, home), them))
						add(out, &n, from, SQ(6, home), 0, MF_CASTLE);
					if ((p->castle & qs) && !p->sq[SQ(3, home)] && !p->sq[SQ(2, home)] &&
					    !p->sq[SQ(1, home)] && p->sq[SQ(0, home)] == PIECE(us, ROOK) &&
					    !sq_attacked(p, SQ(3, home), them) && !sq_attacked(p, SQ(2, home), them))
						add(out, &n, from, SQ(2, home), 0, MF_CASTLE);
				}
			}
			break;
		}
		default: {
			int t = TYPE_OF(pc);
			for (int pass = 0; pass < 2; pass++) {
				const int (*d)[2];
				if (pass == 0) {
					if (t == ROOK) continue;
					d = bishop_d;
				} else {
					if (t == BISHOP) continue;
					d = rook_d;
				}
				for (int i = 0; i < 4; i++) {
					int nf = f + d[i][0], nr = r + d[i][1];
					while (on_board(nf, nr)) {
						uint8_t tp = p->sq[SQ(nf, nr)];
						if (!tp) {
							if (!captures_only) add(out, &n, from, SQ(nf, nr), 0, 0);
						} else {
							if (COLOR_OF(tp) == them) add(out, &n, from, SQ(nf, nr), 0, MF_CAPTURE);
							break;
						}
						nf += d[i][0];
						nr += d[i][1];
					}
				}
			}
			break;
		}
		}
	}
	return n;
}

int gen_legal(const Position *p, Move *out)
{
	Move tmp[MAX_MOVES];
	int n = gen_pseudo(p, tmp, false), legal = 0;
	for (int i = 0; i < n; i++) {
		Position np;
		make_move(p, tmp[i], &np);
		if (!in_check(&np, p->side)) out[legal++] = tmp[i];
	}
	return legal;
}

static uint8_t castle_mask(int sq)
{
	switch (sq) {
	case SQ(0, 0): return (uint8_t)~CASTLE_WQ;
	case SQ(7, 0): return (uint8_t)~CASTLE_WK;
	case SQ(4, 0): return (uint8_t)~(CASTLE_WK | CASTLE_WQ);
	case SQ(0, 7): return (uint8_t)~CASTLE_BQ;
	case SQ(7, 7): return (uint8_t)~CASTLE_BK;
	case SQ(4, 7): return (uint8_t)~(CASTLE_BK | CASTLE_BQ);
	default: return 0xFF;
	}
}

void make_move(const Position *p, Move m, Position *out)
{
	*out = *p;
	uint8_t pc = out->sq[m.from];
	int us = p->side;

	out->halfmove++;
	if (TYPE_OF(pc) == PAWN || out->sq[m.to]) out->halfmove = 0;

	if (m.flags & MF_EP) out->sq[m.to + (us == WHITE ? -8 : 8)] = EMPTY;

	out->sq[m.to] = m.promo ? PIECE(us, m.promo) : pc;
	out->sq[m.from] = EMPTY;

	if (m.flags & MF_CASTLE) {
		int home = RANK_OF(m.from);
		if (FILE_OF(m.to) == 6) {
			out->sq[SQ(5, home)] = out->sq[SQ(7, home)];
			out->sq[SQ(7, home)] = EMPTY;
		} else {
			out->sq[SQ(3, home)] = out->sq[SQ(0, home)];
			out->sq[SQ(0, home)] = EMPTY;
		}
	}

	if (TYPE_OF(pc) == KING) out->king[us] = m.to;
	out->castle &= castle_mask(m.from) & castle_mask(m.to);
	out->ep = (m.flags & MF_DOUBLE) ? (m.from + m.to) / 2 : -1;
	if (us == BLACK) out->fullmove++;
	out->side = us ^ 1;
}

bool insufficient_material(const Position *p)
{
	int minors = 0, bishops_color[2] = {0, 0}, bishops = 0;
	for (int s = 0; s < 64; s++) {
		int t = TYPE_OF(p->sq[s]);
		if (t == PAWN || t == ROOK || t == QUEEN) return false;
		if (t == KNIGHT) minors++;
		if (t == BISHOP) {
			minors++;
			bishops++;
			bishops_color[(FILE_OF(s) + RANK_OF(s)) & 1]++;
		}
	}
	if (minors <= 1) return true;
	// Only bishops, all on the same square color
	if (minors == bishops && (bishops_color[0] == 0 || bishops_color[1] == 0)) return true;
	return false;
}

GameStatus position_status(const Position *p)
{
	Move moves[MAX_MOVES];
	if (gen_legal(p, moves) == 0) return in_check(p, p->side) ? STATUS_CHECKMATE : STATUS_STALEMATE;
	if (p->halfmove >= 100) return STATUS_DRAW_50;
	if (insufficient_material(p)) return STATUS_DRAW_MATERIAL;
	return STATUS_PLAYING;
}

void move_to_san(const Position *p, Move m, char *buf)
{
	static const char letters[] = " PNBRQK";
	char *o = buf;
	uint8_t pc = p->sq[m.from];
	int t = TYPE_OF(pc);

	if (m.flags & MF_CASTLE) {
		strcpy(o, FILE_OF(m.to) == 6 ? "O-O" : "O-O-O");
		o += strlen(o);
	} else {
		if (t == PAWN) {
			if (m.flags & MF_CAPTURE) *o++ = 'a' + FILE_OF(m.from);
		} else {
			*o++ = letters[t];
			Move moves[MAX_MOVES];
			int n = gen_legal(p, moves);
			bool ambiguous = false, same_file = false, same_rank = false;
			for (int i = 0; i < n; i++) {
				if (moves[i].to != m.to || moves[i].from == m.from) continue;
				if (TYPE_OF(p->sq[moves[i].from]) != t) continue;
				ambiguous = true;
				if (FILE_OF(moves[i].from) == FILE_OF(m.from)) same_file = true;
				if (RANK_OF(moves[i].from) == RANK_OF(m.from)) same_rank = true;
			}
			if (ambiguous) {
				if (!same_file) {
					*o++ = 'a' + FILE_OF(m.from);
				} else if (!same_rank) {
					*o++ = '1' + RANK_OF(m.from);
				} else {
					*o++ = 'a' + FILE_OF(m.from);
					*o++ = '1' + RANK_OF(m.from);
				}
			}
		}
		if (m.flags & MF_CAPTURE) *o++ = 'x';
		*o++ = 'a' + FILE_OF(m.to);
		*o++ = '1' + RANK_OF(m.to);
		if (m.promo) {
			*o++ = '=';
			*o++ = letters[m.promo];
		}
	}

	Position np;
	make_move(p, m, &np);
	if (in_check(&np, np.side)) {
		Move moves[MAX_MOVES];
		*o++ = gen_legal(&np, moves) == 0 ? '#' : '+';
	}
	*o = 0;
}

bool same_position(const Position *a, const Position *b)
{
	return a->side == b->side && a->castle == b->castle && a->ep == b->ep &&
	       memcmp(a->sq, b->sq, sizeof(a->sq)) == 0;
}

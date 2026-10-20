#pragma once

#include <stdbool.h>
#include <stdint.h>

// Squares are numbered 0..63 as rank * 8 + file, a1 = 0, h8 = 63.
#define SQ(file, rank) ((rank) * 8 + (file))
#define FILE_OF(sq)    ((sq) & 7)
#define RANK_OF(sq)    ((sq) >> 3)

enum { WHITE = 0, BLACK = 1 };

enum { EMPTY = 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

// A board cell holds type | (color << 3); 0 means empty.
#define PIECE(color, type) ((uint8_t)((type) | ((color) << 3)))
#define TYPE_OF(pc)        ((pc) & 7)
#define COLOR_OF(pc)       ((pc) >> 3)

enum {
	CASTLE_WK = 1,
	CASTLE_WQ = 2,
	CASTLE_BK = 4,
	CASTLE_BQ = 8,
};

enum {
	MF_CAPTURE = 1,
	MF_EP      = 2,
	MF_CASTLE  = 4,
	MF_DOUBLE  = 8,
	MF_PROMO   = 16,
};

typedef struct {
	uint8_t from, to;
	uint8_t promo; // piece type to promote to, 0 if none
	uint8_t flags;
} Move;

typedef struct {
	uint8_t sq[64];
	uint8_t side;     // side to move
	uint8_t castle;   // CASTLE_* bits
	int8_t ep;        // en passant target square or -1
	uint8_t king[2];  // king squares
	uint16_t halfmove;
	uint16_t fullmove;
} Position;

typedef enum {
	STATUS_PLAYING = 0,
	STATUS_CHECKMATE,
	STATUS_STALEMATE,
	STATUS_DRAW_50,
	STATUS_DRAW_MATERIAL,
	STATUS_DRAW_REPETITION,
} GameStatus;

#define MAX_MOVES 256

void pos_init(Position *p);
bool sq_attacked(const Position *p, int sq, int by);
bool in_check(const Position *p, int side);
int gen_pseudo(const Position *p, Move *out, bool captures_only);
int gen_legal(const Position *p, Move *out);
void make_move(const Position *p, Move m, Position *out);
bool insufficient_material(const Position *p);
// Status ignoring repetition (which needs game history).
GameStatus position_status(const Position *p);
void move_to_san(const Position *p, Move m, char *buf);
bool same_position(const Position *a, const Position *b);

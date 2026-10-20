// Chess 3DS - two-screen chess with pass & play, CPU and local wireless play.
//
// Top screen:    players, captured pieces, game status and move list.
// Bottom screen: the board (touch or D-pad) and a small button panel.

#include <3ds.h>
#include <citro2d.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ai.h"
#include "chess.h"
#include "net.h"

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

#define SQ_PX   30
#define BOARD_X 0
#define BOARD_Y 0
#define PANEL_X 240

#define MAX_PLY 1000

// ---------------------------------------------------------------------------
// Colors

#define COL(r, g, b)     C2D_Color32(r, g, b, 0xFF)
#define COLA(r, g, b, a) C2D_Color32(r, g, b, a)

static u32 c_bg, c_panel, c_panel2, c_text, c_text_dim, c_accent, c_light, c_dark, c_light_hi, c_dark_hi,
	c_select, c_check, c_hint, c_button, c_button_hi, c_button_off, c_shade, c_good, c_bad;

static void init_colors(void)
{
	c_bg = COL(0x22, 0x24, 0x2A);
	c_panel = COL(0x2F, 0x32, 0x3A);
	c_panel2 = COL(0x3A, 0x3E, 0x48);
	c_text = COL(0xF2, 0xF2, 0xF2);
	c_text_dim = COL(0xA8, 0xAC, 0xB4);
	c_accent = COL(0xF0, 0xB4, 0x3C);
	c_light = COL(0xEE, 0xD8, 0xB2);
	c_dark = COL(0xB5, 0x88, 0x63);
	c_light_hi = COL(0xF4, 0xE7, 0x7A);
	c_dark_hi = COL(0xD9, 0xC2, 0x4E);
	c_select = COL(0x86, 0xC8, 0x6A);
	c_check = COLA(0xE8, 0x30, 0x30, 0xC0);
	c_hint = COLA(0x20, 0x30, 0x20, 0x60);
	c_button = COL(0x48, 0x4D, 0x5A);
	c_button_hi = COL(0x5F, 0x8F, 0xD8);
	c_button_off = COL(0x34, 0x37, 0x3F);
	c_shade = COLA(0x00, 0x00, 0x00, 0x99);
	c_good = COL(0x70, 0xD0, 0x70);
	c_bad = COL(0xE8, 0x60, 0x60);
}

// ---------------------------------------------------------------------------
// Graphics helpers

static C3D_RenderTarget *top_target, *bot_target;
static C2D_SpriteSheet sheet;
static C2D_TextBuf tbuf;

static void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.5f, w, h, c); }

static void frame_rect(float x, float y, float w, float h, float t, u32 c)
{
	rect(x, y, w, t, c);
	rect(x, y + h - t, w, t, c);
	rect(x, y, t, h, c);
	rect(x + w - t, y, t, h, c);
}

static void text(float x, float y, float size, u32 color, int flags, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	C2D_Text t;
	C2D_TextParse(&t, tbuf, buf);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, size, size, color);
}

// Text vertically centered on cy
static void text_mid(float x, float cy, float size, u32 color, int flags, const char *s)
{
	text(x, cy - 15.0f * size, size, color, flags, "%s", s);
}

static int sprite_index(uint8_t pc)
{
	static const int order[7] = {0, 5, 4, 3, 2, 1, 0}; // by type: P N B R Q K
	return order[TYPE_OF(pc)] + COLOR_OF(pc) * 6;
}

static void draw_piece(uint8_t pc, float x, float y, float scale)
{
	C2D_Image img = C2D_SpriteSheetGetImage(sheet, sprite_index(pc));
	C2D_DrawImageAt(img, x, y, 0.5f, NULL, scale, scale);
}

typedef struct {
	float x, y, w, h;
} Box;

static bool in_box(Box b, int px, int py) { return px >= b.x && px < b.x + b.w && py >= b.y && py < b.y + b.h; }

static void button(Box b, const char *label, bool highlighted, bool enabled, float size)
{
	u32 bg = !enabled ? c_button_off : highlighted ? c_button_hi : c_button;
	rect(b.x, b.y, b.w, b.h, bg);
	rect(b.x, b.y + b.h - 3, b.w, 3, COLA(0, 0, 0, 0x50));
	text_mid(b.x + b.w / 2, b.y + b.h / 2 - 1, size, enabled ? c_text : c_text_dim, C2D_AlignCenter, label);
}

// ---------------------------------------------------------------------------
// App state

typedef enum { MODE_LOCAL, MODE_CPU, MODE_NET } GameMode;
typedef enum { SCR_MENU, SCR_CPU_SETUP, SCR_HOST_WAIT, SCR_JOIN, SCR_CONNECTING, SCR_GAME } Screen;
typedef enum { OVL_NONE, OVL_PROMO, OVL_PAUSE, OVL_GAMEOVER, OVL_DRAW_OFFER, OVL_DISCONNECT } Overlay;

static Screen screen = SCR_MENU;
static GameMode mode;
static Overlay overlay = OVL_NONE;
static u32 frame_count;

// Menus
static int menu_sel;
static int setup_row, setup_color = 0, setup_level = 1; // color: 0 white 1 black 2 random
static int pause_sel;
static int over_sel;
static int promo_sel;
static int join_sel;
static NetGameInfo join_games[NET_MAX_GAMES];
static int join_count;
static int join_scan_timer;
static char error_msg[96];
static int connect_timer;

// Game
static Position hist[MAX_PLY + 1];
static char hist_san[MAX_PLY][10];
static Move hist_move[MAX_PLY];
static int ply;
static Move legal[MAX_MOVES];
static int nlegal;

static bool game_over;
static int winner; // WHITE, BLACK or -1 for draw
static char result_text[64];
static char result_reason[64];

static int human_color; // CPU mode
static int cpu_level;
static int my_color;    // net mode
static bool flipped;
static bool auto_rotate = true;

// Board interaction
static int sel = -1;
static bool cursor_visible;
static int cursor_sq = SQ(4, 1);
static bool dragging;
static int drag_x, drag_y;
static int promo_from, promo_to;

// Move animation
static int anim_from = -1, anim_to = -1;
static float anim_t = 1.0f;

// Toast message on the top screen
static char toast[64];
static int toast_timer;

// Net game extra state
static bool draw_offer_sent;
static bool want_rematch, peer_wants_rematch;
static bool peer_left;

static const char *level_names[3] = {"Easy", "Medium", "Hard"};

static void show_toast(const char *msg)
{
	snprintf(toast, sizeof(toast), "%s", msg);
	toast_timer = 150;
}

static const Position *cur(void) { return &hist[ply]; }

// ---------------------------------------------------------------------------
// CPU thread

static Thread ai_thread;
static volatile bool ai_running, ai_done, ai_abort, ai_ok;
static Position ai_pos;
static Move ai_result;
static int ai_level_arg;
static int ai_start_frame;

static void ai_entry(void *arg)
{
	(void)arg;
	Move m;
	ai_ok = ai_pick_move(&ai_pos, ai_level_arg, &ai_abort, &m);
	ai_result = m;
	ai_done = true;
}

static void ai_start(void)
{
	if (ai_running) return;
	ai_pos = *cur();
	ai_level_arg = cpu_level;
	ai_done = false;
	ai_abort = false;
	ai_start_frame = frame_count;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	ai_thread = threadCreate(ai_entry, NULL, 128 * 1024, prio + 1, -2, false);
	ai_running = ai_thread != NULL;
	if (!ai_running) {
		// Couldn't make a thread: think on the main thread instead.
		ai_entry(NULL);
		ai_running = true;
	}
}

static void ai_stop(void)
{
	if (!ai_running) return;
	ai_abort = true;
	if (ai_thread) {
		threadJoin(ai_thread, U64_MAX);
		threadFree(ai_thread);
		ai_thread = NULL;
	}
	ai_running = false;
	ai_done = false;
}

// ---------------------------------------------------------------------------
// Game logic

static bool local_turn(void)
{
	if (game_over) return false;
	switch (mode) {
	case MODE_CPU: return cur()->side == human_color && !ai_running;
	case MODE_NET: return cur()->side == my_color;
	default: return true;
	}
}

static bool can_undo(void)
{
	if (mode == MODE_NET || ply == 0 || ai_running) return false;
	if (mode == MODE_CPU) {
		// Need a human move to take back.
		if (ply == 1 && hist[0].side != human_color) return false;
	}
	return true;
}

static void refresh_legal(void) { nlegal = gen_legal(cur(), legal); }

static void update_orientation(void)
{
	if (mode == MODE_LOCAL && auto_rotate) flipped = cur()->side == BLACK;
}

static void set_result(int win, const char *reason)
{
	game_over = true;
	winner = win;
	if (win < 0)
		snprintf(result_text, sizeof(result_text), "Draw");
	else if (mode == MODE_CPU)
		snprintf(result_text, sizeof(result_text), win == human_color ? "You win!" : "CPU wins");
	else if (mode == MODE_NET)
		snprintf(result_text, sizeof(result_text), win == my_color ? "You win!" : "You lose");
	else
		snprintf(result_text, sizeof(result_text), win == WHITE ? "White wins!" : "Black wins!");
	snprintf(result_reason, sizeof(result_reason), "%s", reason);
	overlay = OVL_GAMEOVER;
	over_sel = 0;
	sel = -1;
	dragging = false;
}

static void check_game_end(void)
{
	const Position *p = cur();
	GameStatus st = position_status(p);
	if (st == STATUS_PLAYING) {
		int reps = 1;
		for (int i = ply - 2; i >= 0 && i >= ply - p->halfmove; i -= 2)
			if (same_position(&hist[i], p)) reps++;
		if (reps >= 3) st = STATUS_DRAW_REPETITION;
	}
	switch (st) {
	case STATUS_CHECKMATE: set_result(p->side ^ 1, "by checkmate"); break;
	case STATUS_STALEMATE: set_result(-1, "by stalemate"); break;
	case STATUS_DRAW_50: set_result(-1, "by the 50-move rule"); break;
	case STATUS_DRAW_MATERIAL: set_result(-1, "insufficient material"); break;
	case STATUS_DRAW_REPETITION: set_result(-1, "by threefold repetition"); break;
	default: break;
	}
}

static void new_game(void)
{
	ai_stop();
	ply = 0;
	pos_init(&hist[0]);
	game_over = false;
	winner = -1;
	overlay = OVL_NONE;
	sel = -1;
	dragging = false;
	anim_from = anim_to = -1;
	anim_t = 1.0f;
	draw_offer_sent = false;
	want_rematch = peer_wants_rematch = false;
	toast_timer = 0;
	refresh_legal();

	switch (mode) {
	case MODE_LOCAL: flipped = false; break;
	case MODE_CPU: flipped = human_color == BLACK; break;
	case MODE_NET: flipped = my_color == BLACK; break;
	}
	cursor_sq = flipped ? SQ(4, 6) : SQ(4, 1);
	screen = SCR_GAME;
}

// Applies a move to the game. from_net / animate control side effects.
static void apply_move(Move m, bool animate, bool send)
{
	if (ply >= MAX_PLY) return;
	move_to_san(cur(), m, hist_san[ply]);
	hist_move[ply] = m;
	make_move(cur(), m, &hist[ply + 1]);
	ply++;

	if (send && mode == MODE_NET) net_send(NET_MSG_MOVE, m.from, m.to, m.promo, (u16)(ply - 1));

	sel = -1;
	if (animate) {
		anim_from = m.from;
		anim_to = m.to;
		anim_t = 0.0f;
	} else {
		anim_t = 1.0f;
	}
	draw_offer_sent = false;
	refresh_legal();
	check_game_end();
	update_orientation();

	if (mode == MODE_CPU && !game_over && cur()->side != human_color) ai_start();
}

static void undo(void)
{
	if (!can_undo()) return;
	ply--;
	if (mode == MODE_CPU && cur()->side != human_color && ply > 0) ply--;
	game_over = false;
	sel = -1;
	anim_t = 1.0f;
	refresh_legal();
	update_orientation();
	if (mode == MODE_CPU && cur()->side != human_color) ai_start();
}

static bool is_target(int from, int to)
{
	for (int i = 0; i < nlegal; i++)
		if (legal[i].from == from && legal[i].to == to) return true;
	return false;
}

static bool has_moves_from(int from)
{
	for (int i = 0; i < nlegal; i++)
		if (legal[i].from == from) return true;
	return false;
}

static void try_user_move(int from, int to, bool animate)
{
	int count = 0;
	Move found = {0};
	for (int i = 0; i < nlegal; i++) {
		if (legal[i].from == from && legal[i].to == to) {
			found = legal[i];
			count++;
		}
	}
	if (count == 0) return;
	if (count > 1) {
		promo_from = from;
		promo_to = to;
		promo_sel = 0;
		overlay = OVL_PROMO;
		return;
	}
	apply_move(found, animate, true);
}

static void finish_promotion(int type)
{
	for (int i = 0; i < nlegal; i++) {
		if (legal[i].from == promo_from && legal[i].to == promo_to && legal[i].promo == type) {
			overlay = OVL_NONE;
			apply_move(legal[i], false, true);
			return;
		}
	}
	overlay = OVL_NONE;
}

// Tap / A-press on a board square
static void click_square(int sq, bool animate)
{
	if (!local_turn()) {
		sel = -1;
		return;
	}
	if (sel >= 0 && is_target(sel, sq)) {
		try_user_move(sel, sq, animate);
		return;
	}
	uint8_t pc = cur()->sq[sq];
	if (pc && COLOR_OF(pc) == cur()->side && sq != sel && has_moves_from(sq))
		sel = sq;
	else
		sel = -1;
}

// ---------------------------------------------------------------------------
// Board geometry

static void sq_to_screen(int sq, int *x, int *y)
{
	int f = FILE_OF(sq), r = RANK_OF(sq);
	int col = flipped ? 7 - f : f;
	int row = flipped ? r : 7 - r;
	*x = BOARD_X + col * SQ_PX;
	*y = BOARD_Y + row * SQ_PX;
}

static int screen_to_sq(int x, int y)
{
	if (x < BOARD_X || y < BOARD_Y || x >= BOARD_X + 8 * SQ_PX || y >= BOARD_Y + 8 * SQ_PX) return -1;
	int col = (x - BOARD_X) / SQ_PX, row = (y - BOARD_Y) / SQ_PX;
	int f = flipped ? 7 - col : col;
	int r = flipped ? row : 7 - row;
	return SQ(f, r);
}

// ---------------------------------------------------------------------------
// Net game flow

static void leave_net(void)
{
	if (net_peer_connected()) {
		net_send(NET_MSG_BYE, 0, 0, 0, 0);
		// Give the message a moment to get through.
		for (int i = 0; i < 20; i++) {
			net_update();
			gspWaitForVBlank();
		}
	}
	net_close();
}

static void go_menu(void)
{
	ai_stop();
	if (mode == MODE_NET) leave_net();
	screen = SCR_MENU;
	overlay = OVL_NONE;
	error_msg[0] = 0;
}

static void start_rematch_if_ready(void)
{
	if (want_rematch && peer_wants_rematch) {
		my_color ^= 1;
		new_game();
		show_toast(my_color == WHITE ? "Rematch! You play White" : "Rematch! You play Black");
	}
}

static void handle_net_events(void)
{
	NetEvent ev;
	while (net_poll(&ev)) {
		switch (ev.type) {
		case NET_MSG_HELLO:
			if (screen == SCR_CONNECTING) {
				my_color = ev.a ^ 1;
				mode = MODE_NET;
				new_game();
				show_toast(my_color == WHITE ? "Connected! You play White" : "Connected! You play Black");
			}
			break;
		case NET_MSG_MOVE:
			if (screen != SCR_GAME || game_over || ev.d != ply || cur()->side == my_color) break;
			for (int i = 0; i < nlegal; i++) {
				if (legal[i].from == ev.a && legal[i].to == ev.b && legal[i].promo == ev.c) {
					if (overlay == OVL_PROMO) overlay = OVL_NONE;
					apply_move(legal[i], true, false);
					break;
				}
			}
			break;
		case NET_MSG_RESIGN:
			if (screen == SCR_GAME && !game_over) set_result(my_color, "opponent resigned");
			break;
		case NET_MSG_DRAW_OFFER:
			if (screen == SCR_GAME && !game_over) {
				overlay = OVL_DRAW_OFFER;
				over_sel = 0;
			}
			break;
		case NET_MSG_DRAW_ACCEPT:
			if (screen == SCR_GAME && !game_over) set_result(-1, "by agreement");
			break;
		case NET_MSG_DRAW_DECLINE:
			draw_offer_sent = false;
			show_toast("Draw offer declined");
			break;
		case NET_MSG_REMATCH:
			peer_wants_rematch = true;
			if (!want_rematch) show_toast("Opponent wants a rematch!");
			start_rematch_if_ready();
			break;
		case NET_MSG_BYE:
			peer_left = true;
			break;
		}
	}
}

static void update_net(void)
{
	net_update();
	handle_net_events();

	if (screen == SCR_HOST_WAIT && net_peer_connected()) {
		mode = MODE_NET;
		my_color = WHITE;
		net_send(NET_MSG_HELLO, WHITE, 0, 0, 0);
		new_game();
		show_toast("Opponent joined! You play White");
	}

	if (screen == SCR_CONNECTING) {
		if (net_peer_lost() || ++connect_timer > 60 * 15) {
			net_close();
			screen = SCR_JOIN;
			snprintf(error_msg, sizeof(error_msg), "Couldn't connect to that game.");
		}
	}

	if (screen == SCR_GAME && mode == MODE_NET && (net_peer_lost() || peer_left) && overlay != OVL_DISCONNECT) {
		if (!game_over) {
			game_over = true;
			winner = -1;
			snprintf(result_text, sizeof(result_text), "Opponent left");
			snprintf(result_reason, sizeof(result_reason), "The connection was lost");
		}
		overlay = OVL_DISCONNECT;
		dragging = false;
		sel = -1;
	}
}

// ---------------------------------------------------------------------------
// Drawing: top screen

static void captured_by(int color, int counts[7])
{
	// Pieces of the opponent missing from the board.
	static const int start[7] = {0, 8, 2, 2, 2, 1, 1};
	int on_board[7] = {0};
	const Position *p = cur();
	for (int s = 0; s < 64; s++)
		if (p->sq[s] && COLOR_OF(p->sq[s]) != color) on_board[TYPE_OF(p->sq[s])]++;
	for (int t = 1; t < 7; t++) counts[t] = start[t] > on_board[t] ? start[t] - on_board[t] : 0;
}

static int material(int color)
{
	static const int v[7] = {0, 1, 3, 3, 5, 9, 0};
	int m = 0;
	const Position *p = cur();
	for (int s = 0; s < 64; s++)
		if (p->sq[s] && COLOR_OF(p->sq[s]) == color) m += v[TYPE_OF(p->sq[s])];
	return m;
}

static const char *player_name(int color)
{
	static char buf[48];
	switch (mode) {
	case MODE_CPU:
		if (color == human_color) return "You";
		snprintf(buf, sizeof(buf), "CPU (%s)", level_names[cpu_level]);
		return buf;
	case MODE_NET:
		if (color == my_color) return "You";
		return net_peer_name();
	default: return color == WHITE ? "White" : "Black";
	}
}

static void draw_player_card(float x, float y, float w, int color)
{
	bool turn = !game_over && cur()->side == color;
	rect(x, y, w, 52, c_panel);
	if (turn) frame_rect(x, y, w, 52, 2, c_accent);

	// Color swatch
	rect(x + 8, y + 8, 16, 16, color == WHITE ? COL(0xF5, 0xF3, 0xEA) : COL(0x18, 0x18, 0x1C));
	frame_rect(x + 8, y + 8, 16, 16, 1, c_text_dim);
	text(x + 32, y + 5, 0.55f, c_text, 0, "%s", player_name(color));
	if (turn) {
		const char *t = (mode == MODE_CPU && color != human_color) ? "thinking..." : "to move";
		text(x + w - 8, y + 7, 0.45f, c_accent, C2D_AlignRight, "%s", t);
	}

	// Captured pieces
	int counts[7];
	captured_by(color, counts);
	float cx = x + 8;
	static const int show_order[] = {QUEEN, ROOK, BISHOP, KNIGHT, PAWN};
	for (int i = 0; i < 5; i++) {
		int t = show_order[i];
		for (int k = 0; k < counts[t]; k++) {
			draw_piece(PIECE(color ^ 1, t), cx, y + 30, 0.6f);
			cx += (t == PAWN) ? 9 : 12;
		}
		if (counts[t]) cx += 5;
	}
	int diff = material(color) - material(color ^ 1);
	if (diff > 0) text(cx + 4, y + 32, 0.45f, c_text_dim, 0, "+%d", diff);
}

static void draw_status_box(float x, float y, float w, float h)
{
	rect(x, y, w, h, c_panel2);
	const Position *p = cur();
	char line1[64], line2[64];
	u32 col1 = c_text;
	line2[0] = 0;

	if (game_over) {
		snprintf(line1, sizeof(line1), "%s", result_text);
		snprintf(line2, sizeof(line2), "%s", result_reason);
		col1 = c_accent;
	} else {
		bool check = in_check(p, p->side);
		const char *side = p->side == WHITE ? "White" : "Black";
		if (mode == MODE_LOCAL) {
			snprintf(line1, sizeof(line1), "%s to move%s", side, check ? " - Check!" : "");
		} else if (local_turn() || (mode == MODE_CPU && p->side == human_color)) {
			snprintf(line1, sizeof(line1), check ? "Your move - Check!" : "Your move");
		} else if (mode == MODE_CPU) {
			int dots = (frame_count / 20) % 4;
			snprintf(line1, sizeof(line1), "CPU is thinking%.*s", dots, "...");
		} else {
			snprintf(line1, sizeof(line1), "Waiting for opponent%s", check ? " (Check!)" : "");
		}
		if (check) col1 = c_bad;
		if (ply > 0) snprintf(line2, sizeof(line2), "Last move: %s", hist_san[ply - 1]);
		else snprintf(line2, sizeof(line2), "White moves first");
		if (draw_offer_sent) snprintf(line2, sizeof(line2), "Draw offered...");
	}
	if (toast_timer > 0) {
		snprintf(line2, sizeof(line2), "%s", toast);
	}
	text(x + w / 2, y + 8, 0.7f, col1, C2D_AlignCenter, "%s", line1);
	text(x + w / 2, y + 36, 0.5f, toast_timer > 0 ? c_accent : c_text_dim, C2D_AlignCenter, "%s", line2);
}

static void draw_move_list(float x, float y, float w, float h)
{
	rect(x, y, w, h, c_panel);
	text(x + 8, y + 4, 0.5f, c_accent, 0, "Moves");
	int start_ply = hist[0].side == BLACK ? 1 : 0; // always 0 for a normal game
	int full = (ply + start_ply + 1) / 2;
	int rows = (int)((h - 26) / 16);
	int first = full > rows ? full - rows : 0;
	for (int i = first; i < full; i++) {
		float ry = y + 24 + (i - first) * 16;
		int wp = i * 2, bp = i * 2 + 1;
		text(x + 6, ry, 0.42f, c_text_dim, 0, "%d.", i + 1);
		if (wp < ply) text(x + 32, ry, 0.42f, wp == ply - 1 ? c_accent : c_text, 0, "%s", hist_san[wp]);
		if (bp < ply) text(x + 82, ry, 0.42f, bp == ply - 1 ? c_accent : c_text, 0, "%s", hist_san[bp]);
	}
	if (ply == 0) text(x + w / 2, y + h / 2 - 8, 0.42f, c_text_dim, C2D_AlignCenter, "No moves yet");
}

static void draw_top_game(void)
{
	rect(0, 0, TOP_W, 22, c_panel2);
	text(8, 2, 0.55f, c_accent, 0, "Chess 3DS");
	const char *mode_name = mode == MODE_LOCAL ? "Pass & Play" : mode == MODE_CPU ? "vs CPU" : "Local Wireless";
	text(TOP_W - 8, 3, 0.5f, c_text_dim, C2D_AlignRight, "%s", mode_name);

	int top_color = flipped ? WHITE : BLACK;
	draw_player_card(6, 28, 250, top_color);
	draw_status_box(6, 86, 250, 62);
	draw_player_card(6, 154, 250, top_color ^ 1);
	text(131, 214, 0.4f, c_text_dim, C2D_AlignCenter, "Touch or D-Pad + A to move");
	text(131, 226, 0.4f, c_text_dim, C2D_AlignCenter, "START: Menu   X: Flip   Y: Undo");

	draw_move_list(262, 28, 132, 206);
}

static void draw_title_board(float x, float y, float sq)
{
	for (int r = 0; r < 4; r++)
		for (int f = 0; f < 8; f++) rect(x + f * sq, y + r * sq, sq, sq, ((r + f) & 1) ? c_dark : c_light);
	static const int back[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
	float s = sq / 30.0f;
	for (int f = 0; f < 8; f++) {
		draw_piece(PIECE(BLACK, back[f]), x + f * sq, y, s);
		draw_piece(PIECE(BLACK, PAWN), x + f * sq, y + sq, s);
		draw_piece(PIECE(WHITE, PAWN), x + f * sq, y + 2 * sq, s);
		draw_piece(PIECE(WHITE, back[f]), x + f * sq, y + 3 * sq, s);
	}
}

static void draw_top_title(const char *subtitle, const char *subtitle2)
{
	text(TOP_W / 2, 14, 1.3f, c_accent, C2D_AlignCenter, "Chess 3DS");
	draw_title_board(TOP_W / 2 - 4 * 26, 64, 26);
	if (subtitle) text(TOP_W / 2, 182, 0.6f, c_text, C2D_AlignCenter, "%s", subtitle);
	if (subtitle2) text(TOP_W / 2, 206, 0.5f, c_text_dim, C2D_AlignCenter, "%s", subtitle2);
}

// ---------------------------------------------------------------------------
// Drawing: bottom screen

static Box panel_btn(int i) { return (Box){PANEL_X + 6, 62 + i * 44, 68, 38}; }

static void draw_board(void)
{
	const Position *p = cur();
	int last_from = ply > 0 ? hist_move[ply - 1].from : -1;
	int last_to = ply > 0 ? hist_move[ply - 1].to : -1;

	for (int s = 0; s < 64; s++) {
		int x, y;
		sq_to_screen(s, &x, &y);
		bool light = (FILE_OF(s) + RANK_OF(s)) & 1;
		u32 c = light ? c_light : c_dark;
		if (s == last_from || s == last_to) c = light ? c_light_hi : c_dark_hi;
		if (s == sel) c = c_select;
		rect(x, y, SQ_PX, SQ_PX, c);
	}

	// Coordinates
	for (int i = 0; i < 8; i++) {
		int fsq = SQ(i, flipped ? 7 : 0), x, y;
		sq_to_screen(fsq, &x, &y);
		bool light = (FILE_OF(fsq) + RANK_OF(fsq)) & 1;
		text(x + SQ_PX - 2, y + SQ_PX - 11, 0.32f, light ? c_dark : c_light, C2D_AlignRight, "%c", 'a' + i);
		int rsq = SQ(flipped ? 7 : 0, i);
		sq_to_screen(rsq, &x, &y);
		light = (FILE_OF(rsq) + RANK_OF(rsq)) & 1;
		text(x + 2, y + 1, 0.32f, light ? c_dark : c_light, 0, "%d", i + 1);
	}

	// King in check
	if (in_check(p, p->side)) {
		int x, y;
		sq_to_screen(p->king[p->side], &x, &y);
		C2D_DrawCircleSolid(x + SQ_PX / 2, y + SQ_PX / 2, 0.5f, 14, c_check);
	}

	// Pieces
	for (int s = 0; s < 64; s++) {
		uint8_t pc = p->sq[s];
		if (!pc) continue;
		if (dragging && s == sel) continue;
		int x, y;
		sq_to_screen(s, &x, &y);
		if (anim_t < 1.0f && s == anim_to) {
			int fx, fy;
			sq_to_screen(anim_from, &fx, &fy);
			float t = anim_t * anim_t * (3 - 2 * anim_t);
			draw_piece(pc, fx + (x - fx) * t, fy + (y - fy) * t, 1.0f);
			continue;
		}
		draw_piece(pc, x, y, 1.0f);
	}

	// Move hints
	if (sel >= 0) {
		for (int i = 0; i < nlegal; i++) {
			if (legal[i].from != sel) continue;
			int x, y;
			sq_to_screen(legal[i].to, &x, &y);
			if (legal[i].flags & MF_CAPTURE) {
				// corner triangles
				float a = 9;
				C2D_DrawTriangle(x, y, c_hint, x + a, y, c_hint, x, y + a, c_hint, 0.5f);
				C2D_DrawTriangle(x + SQ_PX, y, c_hint, x + SQ_PX - a, y, c_hint, x + SQ_PX, y + a, c_hint, 0.5f);
				C2D_DrawTriangle(x, y + SQ_PX, c_hint, x + a, y + SQ_PX, c_hint, x, y + SQ_PX - a, c_hint, 0.5f);
				C2D_DrawTriangle(x + SQ_PX, y + SQ_PX, c_hint, x + SQ_PX - a, y + SQ_PX, c_hint, x + SQ_PX,
				                 y + SQ_PX - a, c_hint, 0.5f);
			} else {
				C2D_DrawCircleSolid(x + SQ_PX / 2, y + SQ_PX / 2, 0.5f, 5, c_hint);
			}
		}
	}

	// Cursor
	if (cursor_visible && overlay == OVL_NONE) {
		int x, y;
		sq_to_screen(cursor_sq, &x, &y);
		frame_rect(x, y, SQ_PX, SQ_PX, 2, COL(0x30, 0x70, 0xE0));
	}

	// Dragged piece follows the stylus
	if (dragging && sel >= 0) draw_piece(p->sq[sel], drag_x - 18, drag_y - 22, 1.2f);
}

static void draw_panel(void)
{
	rect(PANEL_X, 0, BOT_W - PANEL_X, SCR_H, c_panel);
	const Position *p = cur();

	// Turn indicator
	int side = game_over ? -1 : p->side;
	if (side >= 0) {
		C2D_DrawCircleSolid(PANEL_X + 40, 22, 0.5f, 13, c_text_dim);
		C2D_DrawCircleSolid(PANEL_X + 40, 22, 0.5f, 11, side == WHITE ? COL(0xF5, 0xF3, 0xEA) : COL(0x18, 0x18, 0x1C));
		text(PANEL_X + 40, 38, 0.42f, c_text, C2D_AlignCenter, side == WHITE ? "White" : "Black");
	} else {
		text(PANEL_X + 40, 16, 0.5f, c_accent, C2D_AlignCenter, "Game");
		text(PANEL_X + 40, 34, 0.5f, c_accent, C2D_AlignCenter, "over");
	}

	button(panel_btn(0), "Menu", false, true, 0.5f);
	button(panel_btn(1), "Undo", false, can_undo(), 0.5f);
	button(panel_btn(2), "Flip", false, true, 0.5f);
	if (game_over) button(panel_btn(3), "Result", false, true, 0.5f);
}

static const char *pause_items[6];
static bool pause_enabled[6];
static int pause_count;

enum { P_RESUME, P_UNDO, P_DRAW, P_RESIGN, P_FLIP, P_ROTATE, P_QUIT };
static int pause_ids[8];

static void build_pause_menu(void)
{
	pause_count = 0;
#define ADD(id, label, en)                    \
	do {                                      \
		pause_ids[pause_count] = id;          \
		pause_items[pause_count] = label;     \
		pause_enabled[pause_count] = en;      \
		pause_count++;                        \
	} while (0)
	ADD(P_RESUME, "Resume", true);
	if (mode == MODE_NET)
		ADD(P_DRAW, draw_offer_sent ? "Draw offered" : "Offer draw", !game_over && !draw_offer_sent);
	else
		ADD(P_UNDO, "Undo move", can_undo());
	ADD(P_RESIGN, "Resign", !game_over);
	if (mode == MODE_LOCAL) ADD(P_ROTATE, auto_rotate ? "Auto-rotate: On" : "Auto-rotate: Off", true);
	else ADD(P_FLIP, "Flip board", true);
	ADD(P_QUIT, "Quit to menu", true);
#undef ADD
}

static Box pause_btn(int i) { return (Box){70, 22 + i * 40, 180, 34}; }

static Box over_btn(int i) { return (Box){40 + i * 82, 150, 76, 36}; }

static const char *over_labels(int i)
{
	static const char *labels[3];
	labels[0] = mode == MODE_NET ? (want_rematch ? "Waiting" : "Rematch") : "New game";
	labels[1] = "View board";
	labels[2] = "Menu";
	return labels[i];
}

static Box promo_btn(int i) { return (Box){52 + i * 56, 96, 48, 48}; }

static void draw_overlay(void)
{
	static const int promo_types[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
	switch (overlay) {
	case OVL_PROMO:
		rect(0, 0, BOT_W, SCR_H, c_shade);
		rect(40, 60, 240, 100, c_panel);
		text(160, 66, 0.55f, c_text, C2D_AlignCenter, "Promote to:");
		for (int i = 0; i < 4; i++) {
			Box b = promo_btn(i);
			rect(b.x, b.y, b.w, b.h, i == promo_sel ? c_button_hi : c_light);
			draw_piece(PIECE(cur()->side, promo_types[i]), b.x + 1, b.y + 1, 1.5f);
		}
		break;
	case OVL_PAUSE:
		rect(0, 0, BOT_W, SCR_H, c_shade);
		build_pause_menu();
		for (int i = 0; i < pause_count; i++) button(pause_btn(i), pause_items[i], i == pause_sel, pause_enabled[i], 0.55f);
		break;
	case OVL_GAMEOVER:
		rect(0, 0, BOT_W, SCR_H, c_shade);
		rect(24, 50, 272, 148, c_panel);
		frame_rect(24, 50, 272, 148, 2, c_accent);
		text(160, 62, 0.9f, c_accent, C2D_AlignCenter, "%s", result_text);
		text(160, 100, 0.55f, c_text, C2D_AlignCenter, "%s", result_reason);
		if (mode == MODE_NET && peer_wants_rematch && !want_rematch)
			text(160, 124, 0.45f, c_good, C2D_AlignCenter, "Opponent wants a rematch!");
		else if (mode == MODE_NET && want_rematch)
			text(160, 124, 0.45f, c_text_dim, C2D_AlignCenter, "Waiting for opponent...");
		for (int i = 0; i < 3; i++) button(over_btn(i), over_labels(i), i == over_sel, true, 0.45f);
		break;
	case OVL_DRAW_OFFER:
		rect(0, 0, BOT_W, SCR_H, c_shade);
		rect(30, 60, 260, 120, c_panel);
		frame_rect(30, 60, 260, 120, 2, c_accent);
		text(160, 72, 0.6f, c_text, C2D_AlignCenter, "%s offers a draw", net_peer_name());
		button((Box){50, 120, 100, 40}, "Accept (A)", over_sel == 0, true, 0.5f);
		button((Box){170, 120, 100, 40}, "Decline (B)", over_sel == 1, true, 0.5f);
		break;
	case OVL_DISCONNECT:
		rect(0, 0, BOT_W, SCR_H, c_shade);
		rect(30, 60, 260, 120, c_panel);
		frame_rect(30, 60, 260, 120, 2, c_bad);
		text(160, 72, 0.6f, c_text, C2D_AlignCenter, "Opponent disconnected");
		if (game_over && winner >= 0) text(160, 96, 0.5f, c_text_dim, C2D_AlignCenter, "%s", result_text);
		button((Box){100, 124, 120, 40}, "Main menu (A)", true, true, 0.5f);
		break;
	default: break;
	}
}

// Menus on the bottom screen

#define MENU_ITEMS 5
static const char *menu_labels[MENU_ITEMS] = {"Pass & Play (2 players)", "Play vs CPU", "Host Wireless Game",
                                              "Join Wireless Game", "Exit"};

static Box menu_btn(int i)
{
	if (i == MENU_ITEMS - 1) return (Box){110, 200, 100, 30};
	return (Box){40, 14 + i * 46, 240, 40};
}

static Box setup_color_btn(int i) { return (Box){20 + i * 96, 38, 88, 36}; }
static Box setup_level_btn(int i) { return (Box){20 + i * 96, 112, 88, 36}; }
static Box setup_start_btn(void) { return (Box){90, 170, 140, 40}; }
static Box back_btn(void) { return (Box){8, 200, 80, 32}; }
static Box join_btn(int i) { return (Box){30, 30 + i * 40, 260, 34}; }

static void draw_bottom_menu(void)
{
	for (int i = 0; i < MENU_ITEMS; i++) button(menu_btn(i), menu_labels[i], i == menu_sel, true, i == MENU_ITEMS - 1 ? 0.5f : 0.6f);
}

static void draw_bottom_setup(void)
{
	static const char *colors[3] = {"White", "Black", "Random"};
	text(160, 12, 0.55f, setup_row == 0 ? c_accent : c_text, C2D_AlignCenter, "Your color");
	for (int i = 0; i < 3; i++) button(setup_color_btn(i), colors[i], i == setup_color, true, 0.55f);
	text(160, 86, 0.55f, setup_row == 1 ? c_accent : c_text, C2D_AlignCenter, "Difficulty");
	for (int i = 0; i < 3; i++) button(setup_level_btn(i), level_names[i], i == setup_level, true, 0.55f);
	Box s = setup_start_btn();
	button(s, "Start!", setup_row == 2, true, 0.65f);
	if (setup_row == 2) frame_rect(s.x - 2, s.y - 2, s.w + 4, s.h + 4, 2, c_accent);
	button(back_btn(), "Back", false, true, 0.5f);
}

static void draw_bottom_wait(const char *msg)
{
	int dots = (frame_count / 20) % 4;
	text(160, 90, 0.6f, c_text, C2D_AlignCenter, "%s%.*s", msg, dots, "...");
	if (error_msg[0]) text(160, 130, 0.45f, c_bad, C2D_AlignCenter, "%s", error_msg);
	button(back_btn(), "Cancel", false, true, 0.5f);
}

static void draw_bottom_join(void)
{
	text(160, 6, 0.55f, c_text, C2D_AlignCenter, "Nearby games");
	if (join_count == 0) {
		int dots = (frame_count / 20) % 4;
		text(160, 90, 0.55f, c_text_dim, C2D_AlignCenter, "Searching%.*s", dots, "...");
		text(160, 116, 0.42f, c_text_dim, C2D_AlignCenter, "Ask your friend to pick \"Host Wireless Game\"");
	}
	for (int i = 0; i < join_count; i++) {
		char label[64];
		snprintf(label, sizeof(label), "%.40s's game", join_games[i].host_name);
		button(join_btn(i), label, i == join_sel, true, 0.55f);
	}
	if (error_msg[0]) text(160, 176, 0.45f, c_bad, C2D_AlignCenter, "%s", error_msg);
	button(back_btn(), "Back", false, true, 0.5f);
}

static void render(void)
{
	C2D_TextBufClear(tbuf);
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

	C2D_TargetClear(top_target, c_bg);
	C2D_SceneBegin(top_target);
	switch (screen) {
	case SCR_MENU: draw_top_title("Choose a mode on the touch screen", "Wireless play needs two 3DS systems with Chess 3DS"); break;
	case SCR_CPU_SETUP: draw_top_title("Play against the CPU", "Pick your color and difficulty"); break;
	case SCR_HOST_WAIT: draw_top_title("Hosting a wireless game", "Your friend should choose \"Join Wireless Game\""); break;
	case SCR_JOIN: draw_top_title("Join a wireless game", "Games hosted nearby will appear below"); break;
	case SCR_CONNECTING: draw_top_title("Connecting...", NULL); break;
	case SCR_GAME: draw_top_game(); break;
	}

	C2D_TargetClear(bot_target, c_bg);
	C2D_SceneBegin(bot_target);
	switch (screen) {
	case SCR_MENU: draw_bottom_menu(); break;
	case SCR_CPU_SETUP: draw_bottom_setup(); break;
	case SCR_HOST_WAIT: draw_bottom_wait("Waiting for a player"); break;
	case SCR_JOIN: draw_bottom_join(); break;
	case SCR_CONNECTING: draw_bottom_wait("Connecting"); break;
	case SCR_GAME:
		draw_board();
		draw_panel();
		draw_overlay();
		break;
	}

	C3D_FrameEnd(0);
}

// ---------------------------------------------------------------------------
// Input

static bool quit_requested;

static void start_cpu_game(void)
{
	mode = MODE_CPU;
	human_color = setup_color == 2 ? rand() & 1 : setup_color;
	cpu_level = setup_level;
	new_game();
	if (human_color == BLACK) ai_start();
}

static void menu_activate(int i)
{
	error_msg[0] = 0;
	switch (i) {
	case 0:
		mode = MODE_LOCAL;
		auto_rotate = true;
		new_game();
		break;
	case 1:
		screen = SCR_CPU_SETUP;
		setup_row = 2;
		break;
	case 2:
		screen = SCR_HOST_WAIT;
		mode = MODE_NET;
		peer_left = false;
		if (!net_init() || !net_host_start())
			snprintf(error_msg, sizeof(error_msg), "Couldn't start wireless. Is wireless turned on?");
		break;
	case 3:
		screen = SCR_JOIN;
		mode = MODE_NET;
		peer_left = false;
		join_count = 0;
		join_sel = 0;
		join_scan_timer = 0;
		if (!net_init()) snprintf(error_msg, sizeof(error_msg), "Couldn't start wireless. Is wireless turned on?");
		break;
	case 4: quit_requested = true; break;
	}
}

static void join_activate(int i)
{
	if (i < 0 || i >= join_count) return;
	error_msg[0] = 0;
	if (net_join(join_games[i].index)) {
		screen = SCR_CONNECTING;
		connect_timer = 0;
	} else {
		snprintf(error_msg, sizeof(error_msg), "Couldn't join that game.");
	}
}

static void pause_activate(int i)
{
	build_pause_menu();
	if (i < 0 || i >= pause_count || !pause_enabled[i]) return;
	switch (pause_ids[i]) {
	case P_RESUME: overlay = OVL_NONE; break;
	case P_UNDO:
		undo();
		overlay = OVL_NONE;
		break;
	case P_DRAW:
		net_send(NET_MSG_DRAW_OFFER, 0, 0, 0, 0);
		draw_offer_sent = true;
		overlay = OVL_NONE;
		show_toast("Draw offered");
		break;
	case P_RESIGN:
		overlay = OVL_NONE;
		ai_stop();
		if (mode == MODE_NET) {
			net_send(NET_MSG_RESIGN, 0, 0, 0, 0);
			set_result(my_color ^ 1, "you resigned");
		} else if (mode == MODE_CPU) {
			set_result(human_color ^ 1, "you resigned");
		} else {
			int loser = cur()->side;
			set_result(loser ^ 1, loser == WHITE ? "White resigned" : "Black resigned");
		}
		break;
	case P_FLIP:
		flipped = !flipped;
		overlay = OVL_NONE;
		break;
	case P_ROTATE:
		auto_rotate = !auto_rotate;
		update_orientation();
		break;
	case P_QUIT: go_menu(); break;
	}
}

static void over_activate(int i)
{
	switch (i) {
	case 0:
		if (mode == MODE_NET) {
			if (!want_rematch) {
				want_rematch = true;
				net_send(NET_MSG_REMATCH, 0, 0, 0, 0);
				start_rematch_if_ready();
			}
		} else if (mode == MODE_CPU) {
			start_cpu_game();
		} else {
			new_game();
		}
		break;
	case 1: overlay = OVL_NONE; break;
	case 2: go_menu(); break;
	}
}

static void flip_pressed(void)
{
	if (mode == MODE_LOCAL && auto_rotate) {
		auto_rotate = false;
		show_toast("Auto-rotate off");
	}
	flipped = !flipped;
}

static void move_cursor(int dx, int dy)
{
	// dx/dy in screen terms
	if (flipped) {
		dx = -dx;
		dy = -dy;
	}
	int f = FILE_OF(cursor_sq) + dx, r = RANK_OF(cursor_sq) - dy;
	if (f < 0) f = 0;
	if (f > 7) f = 7;
	if (r < 0) r = 0;
	if (r > 7) r = 7;
	cursor_sq = SQ(f, r);
}

static void input_game(u32 down, u32 held, u32 up, touchPosition touch)
{
	bool touched = down & KEY_TOUCH;

	switch (overlay) {
	case OVL_PROMO: {
		static const int promo_types[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
		if (down & KEY_LEFT) promo_sel = (promo_sel + 3) % 4;
		if (down & KEY_RIGHT) promo_sel = (promo_sel + 1) % 4;
		if (down & KEY_A) finish_promotion(promo_types[promo_sel]);
		if (down & KEY_B) overlay = OVL_NONE;
		if (touched)
			for (int i = 0; i < 4; i++)
				if (in_box(promo_btn(i), touch.px, touch.py)) finish_promotion(promo_types[i]);
		return;
	}
	case OVL_PAUSE:
		build_pause_menu();
		if (down & KEY_UP) pause_sel = (pause_sel + pause_count - 1) % pause_count;
		if (down & KEY_DOWN) pause_sel = (pause_sel + 1) % pause_count;
		if (down & KEY_A) pause_activate(pause_sel);
		if (down & (KEY_B | KEY_START)) overlay = OVL_NONE;
		if (touched) {
			bool hit = false;
			for (int i = 0; i < pause_count; i++)
				if (in_box(pause_btn(i), touch.px, touch.py)) {
					pause_activate(i);
					hit = true;
				}
			if (!hit) overlay = OVL_NONE;
		}
		return;
	case OVL_GAMEOVER:
		if (down & KEY_LEFT) over_sel = (over_sel + 2) % 3;
		if (down & KEY_RIGHT) over_sel = (over_sel + 1) % 3;
		if (down & KEY_A) over_activate(over_sel);
		if (down & KEY_B) overlay = OVL_NONE;
		if (touched)
			for (int i = 0; i < 3; i++)
				if (in_box(over_btn(i), touch.px, touch.py)) over_activate(i);
		return;
	case OVL_DRAW_OFFER:
		if (down & KEY_LEFT) over_sel = 0;
		if (down & KEY_RIGHT) over_sel = 1;
		{
			int choice = -1;
			if (down & KEY_A) choice = over_sel;
			if (down & KEY_B) choice = 1;
			if (touched && in_box((Box){50, 120, 100, 40}, touch.px, touch.py)) choice = 0;
			if (touched && in_box((Box){170, 120, 100, 40}, touch.px, touch.py)) choice = 1;
			if (choice == 0) {
				net_send(NET_MSG_DRAW_ACCEPT, 0, 0, 0, 0);
				set_result(-1, "by agreement");
			} else if (choice == 1) {
				net_send(NET_MSG_DRAW_DECLINE, 0, 0, 0, 0);
				overlay = OVL_NONE;
			}
		}
		return;
	case OVL_DISCONNECT:
		if ((down & (KEY_A | KEY_B | KEY_START)) || (touched && in_box((Box){100, 124, 120, 40}, touch.px, touch.py)))
			go_menu();
		return;
	default: break;
	}

	// No overlay: board and panel
	if (down & KEY_START) {
		overlay = game_over ? OVL_GAMEOVER : OVL_PAUSE;
		pause_sel = 0;
		over_sel = 0;
		return;
	}
	if (down & KEY_X) flip_pressed();
	if ((down & KEY_Y) && can_undo()) undo();

	if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
		if (!cursor_visible) {
			cursor_visible = true;
			if (sel >= 0) cursor_sq = sel;
		} else {
			move_cursor((down & KEY_RIGHT) ? 1 : (down & KEY_LEFT) ? -1 : 0,
			            (down & KEY_DOWN) ? 1 : (down & KEY_UP) ? -1 : 0);
		}
	}
	if (down & KEY_A) {
		if (!cursor_visible) {
			cursor_visible = true;
		} else if (game_over) {
			overlay = OVL_GAMEOVER;
		} else {
			click_square(cursor_sq, true);
		}
	}
	if (down & KEY_B) sel = -1;

	if (touched) {
		cursor_visible = false;
		if (touch.px >= PANEL_X) {
			if (in_box(panel_btn(0), touch.px, touch.py)) {
				overlay = game_over ? OVL_GAMEOVER : OVL_PAUSE;
				pause_sel = 0;
				over_sel = 0;
			} else if (in_box(panel_btn(1), touch.px, touch.py)) {
				undo();
			} else if (in_box(panel_btn(2), touch.px, touch.py)) {
				flip_pressed();
			} else if (game_over && in_box(panel_btn(3), touch.px, touch.py)) {
				overlay = OVL_GAMEOVER;
				over_sel = 0;
			}
			return;
		}
		int sq = screen_to_sq(touch.px, touch.py);
		if (sq < 0) return;
		if (sel >= 0 && is_target(sel, sq) && local_turn()) {
			try_user_move(sel, sq, true);
		} else {
			click_square(sq, true);
			if (sel == sq) {
				dragging = true;
				drag_x = touch.px;
				drag_y = touch.py;
			}
		}
		return;
	}

	if (dragging) {
		if (held & KEY_TOUCH) {
			drag_x = touch.px;
			drag_y = touch.py;
		} else if (up & KEY_TOUCH) {
			dragging = false;
			int sq = screen_to_sq(drag_x, drag_y);
			if (sq >= 0 && sel >= 0 && sq != sel && is_target(sel, sq) && local_turn()) try_user_move(sel, sq, false);
		} else {
			dragging = false;
		}
	}
}

static void input_menus(u32 down, touchPosition touch)
{
	bool touched = down & KEY_TOUCH;
	switch (screen) {
	case SCR_MENU:
		if (down & KEY_UP) menu_sel = (menu_sel + MENU_ITEMS - 1) % MENU_ITEMS;
		if (down & KEY_DOWN) menu_sel = (menu_sel + 1) % MENU_ITEMS;
		if (down & KEY_A) menu_activate(menu_sel);
		if (down & KEY_START) quit_requested = true;
		if (touched)
			for (int i = 0; i < MENU_ITEMS; i++)
				if (in_box(menu_btn(i), touch.px, touch.py)) {
					menu_sel = i;
					menu_activate(i);
				}
		break;
	case SCR_CPU_SETUP:
		if (down & KEY_UP) setup_row = (setup_row + 2) % 3;
		if (down & KEY_DOWN) setup_row = (setup_row + 1) % 3;
		if (down & (KEY_LEFT | KEY_RIGHT)) {
			int d = (down & KEY_LEFT) ? 2 : 1;
			if (setup_row == 0) setup_color = (setup_color + d) % 3;
			if (setup_row == 1) setup_level = (setup_level + d) % 3;
		}
		if (down & KEY_A) {
			if (setup_row == 2) start_cpu_game();
			else setup_row++;
		}
		if (down & KEY_B) screen = SCR_MENU;
		if (touched) {
			for (int i = 0; i < 3; i++) {
				if (in_box(setup_color_btn(i), touch.px, touch.py)) {
					setup_color = i;
					setup_row = 0;
				}
				if (in_box(setup_level_btn(i), touch.px, touch.py)) {
					setup_level = i;
					setup_row = 1;
				}
			}
			if (in_box(setup_start_btn(), touch.px, touch.py)) start_cpu_game();
			if (in_box(back_btn(), touch.px, touch.py)) screen = SCR_MENU;
		}
		break;
	case SCR_HOST_WAIT:
	case SCR_CONNECTING:
		if ((down & KEY_B) || (touched && in_box(back_btn(), touch.px, touch.py))) {
			net_close();
			screen = screen == SCR_HOST_WAIT ? SCR_MENU : SCR_JOIN;
			error_msg[0] = 0;
		}
		break;
	case SCR_JOIN:
		if (join_count > 0) {
			if (down & KEY_UP) join_sel = (join_sel + join_count - 1) % join_count;
			if (down & KEY_DOWN) join_sel = (join_sel + 1) % join_count;
			if (down & KEY_A) join_activate(join_sel);
		}
		if (touched)
			for (int i = 0; i < join_count; i++)
				if (in_box(join_btn(i), touch.px, touch.py)) join_activate(i);
		if (screen == SCR_JOIN && ((down & KEY_B) || (touched && in_box(back_btn(), touch.px, touch.py)))) {
			screen = SCR_MENU;
			error_msg[0] = 0;
		}
		break;
	default: break;
	}
}

// ---------------------------------------------------------------------------

static void update(void)
{
	frame_count++;
	if (toast_timer > 0) toast_timer--;
	if (anim_t < 1.0f) {
		anim_t += 0.125f;
		if (anim_t > 1.0f) anim_t = 1.0f;
	}

	if (mode == MODE_NET && screen != SCR_MENU && screen != SCR_CPU_SETUP) update_net();

	if (screen == SCR_JOIN && --join_scan_timer <= 0) {
		// Scanning blocks for a moment, so only do it every couple of seconds.
		join_count = net_scan(join_games);
		if (join_sel >= join_count) join_sel = 0;
		join_scan_timer = 120;
	}

	// Finished CPU move (keep it on screen for a short moment at least)
	if (ai_running && ai_done && frame_count - ai_start_frame > 25) {
		if (ai_thread) {
			threadJoin(ai_thread, U64_MAX);
			threadFree(ai_thread);
			ai_thread = NULL;
		}
		ai_running = false;
		if (ai_ok && screen == SCR_GAME && !game_over) apply_move(ai_result, true, false);
	}
}

int main(void)
{
	romfsInit();
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2);
	C2D_Prepare();

	top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	bot_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	tbuf = C2D_TextBufNew(4096);
	sheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
	if (!sheet) svcBreak(USERBREAK_PANIC);

	init_colors();
	srand(time(NULL));
	pos_init(&hist[0]);

	while (aptMainLoop() && !quit_requested) {
		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp();
		touchPosition touch;
		hidTouchRead(&touch);

		if (screen == SCR_GAME)
			input_game(down, held, up, touch);
		else
			input_menus(down, touch);

		update();
		render();
	}

	ai_stop();
	if (mode == MODE_NET) leave_net();
	net_exit();

	C2D_SpriteSheetFree(sheet);
	C2D_TextBufDelete(tbuf);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	romfsExit();
	return 0;
}

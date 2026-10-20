#pragma once

#include <3ds.h>
#include <stdbool.h>

// Local wireless (UDS) 3DS-to-3DS play.

enum {
	NET_MSG_HELLO = 1, // host -> client: a = host color, b = game number
	NET_MSG_MOVE,      // a = from, b = to, c = promo, d = ply index
	NET_MSG_RESIGN,
	NET_MSG_DRAW_OFFER,
	NET_MSG_DRAW_ACCEPT,
	NET_MSG_DRAW_DECLINE,
	NET_MSG_REMATCH,   // "I want a rematch"
	NET_MSG_BYE,       // leaving the game
};

typedef struct {
	u8 type;
	u8 a, b, c;
	u16 d;
} NetEvent;

#define NET_MAX_GAMES 8

typedef struct {
	char host_name[32];
	int index;
} NetGameInfo;

bool net_init(void);
void net_exit(void);

bool net_host_start(void);
// Returns number of games found (0..NET_MAX_GAMES). Blocks for ~a second.
int net_scan(NetGameInfo *games);
bool net_join(int index);
void net_close(void);

bool net_is_host(void);
// True once the other console is in the network.
bool net_peer_connected(void);
// True if the peer was connected and then went away (or timed out).
bool net_peer_lost(void);
const char *net_peer_name(void);

// Queue a message for reliable, in-order delivery.
void net_send(u8 type, u8 a, u8 b, u8 c, u16 d);
// Call once per frame. Returns true and fills *ev for each received message.
void net_update(void);
bool net_poll(NetEvent *ev);

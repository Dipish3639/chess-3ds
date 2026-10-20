#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WLAN_COMM_ID  0x43485333 // "CHS3"
#define PASSPHRASE    "chess3ds local play v1"
#define DATA_CHANNEL  1
#define NET_MAGIC     0x43483344

#define PKT_DATA 0
#define PKT_ACK  1
#define PKT_PING 2

#define QUEUE_LEN       16
#define RESEND_FRAMES   20  // ~1/3 second
#define PING_FRAMES     30
#define TIMEOUT_FRAMES  (60 * 12)

typedef struct {
	u32 magic;
	u8 kind;
	u8 type;
	u8 a, b, c;
	u8 pad;
	u16 d;
	u32 id;
} Packet;

static bool inited;
static bool active;
static bool host;
static bool peer_seen;
static bool lost;
static char peer_name[32];
static udsBindContext bindctx;
static udsNetworkScanInfo *scan_results;
static size_t scan_count;

static Packet outq[QUEUE_LEN];
static int outq_head, outq_len;
static u32 next_tx_id;    // id assigned to next queued message
static u32 last_rx_id;    // highest in-order id received
static int resend_timer;
static int ping_timer;
static int silent_frames;

static NetEvent inq[QUEUE_LEN];
static int inq_head, inq_len;

bool net_init(void)
{
	if (inited) return true;
	inited = R_SUCCEEDED(udsInit(0x3000, NULL));
	return inited;
}

void net_exit(void)
{
	net_close();
	free(scan_results);
	scan_results = NULL;
	if (inited) udsExit();
	inited = false;
}

static void reset_state(void)
{
	outq_head = outq_len = 0;
	inq_head = inq_len = 0;
	next_tx_id = 1;
	last_rx_id = 0;
	resend_timer = 0;
	ping_timer = 0;
	silent_frames = 0;
	peer_seen = false;
	lost = false;
	peer_name[0] = 0;
}

bool net_host_start(void)
{
	if (!inited) return false;
	net_close();
	reset_state();

	udsNetworkStruct ns;
	udsGenerateDefaultNetworkStruct(&ns, WLAN_COMM_ID, 0, 2);
	Result rc = udsCreateNetwork(&ns, PASSPHRASE, strlen(PASSPHRASE) + 1, &bindctx, DATA_CHANNEL,
	                             UDS_DEFAULT_RECVBUFSIZE);
	if (R_FAILED(rc)) return false;
	active = true;
	host = true;
	return true;
}

int net_scan(NetGameInfo *games)
{
	if (!inited) return 0;
	size_t bufsize = 0x4000;
	u32 *buf = malloc(bufsize);
	if (!buf) return 0;
	memset(buf, 0, bufsize);

	free(scan_results);
	scan_results = NULL;
	scan_count = 0;
	Result rc = udsScanBeacons(buf, bufsize, &scan_results, &scan_count, WLAN_COMM_ID, 0, NULL, false);
	free(buf);
	if (R_FAILED(rc)) {
		scan_count = 0;
		return 0;
	}

	int n = 0;
	for (size_t i = 0; i < scan_count && n < NET_MAX_GAMES; i++) {
		// Skip games that already have two players
		if (scan_results[i].network.total_nodes >= 2) continue;
		games[n].index = (int)i;
		strcpy(games[n].host_name, "3DS");
		if (udsCheckNodeInfoInitialized(&scan_results[i].nodes[0])) {
			char name[64] = {0};
			if (R_SUCCEEDED(udsGetNodeInfoUsername(&scan_results[i].nodes[0], name))) {
				strncpy(games[n].host_name, name, sizeof(games[n].host_name) - 1);
				games[n].host_name[sizeof(games[n].host_name) - 1] = 0;
			}
		}
		n++;
	}
	return n;
}

bool net_join(int index)
{
	if (!inited || !scan_results || index < 0 || (size_t)index >= scan_count) return false;
	net_close();
	reset_state();

	udsNetworkScanInfo *info = &scan_results[index];
	Result rc = -1;
	for (int tries = 0; tries < 5 && R_FAILED(rc); tries++)
		rc = udsConnectNetwork(&info->network, PASSPHRASE, strlen(PASSPHRASE) + 1, &bindctx,
		                       UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, DATA_CHANNEL,
		                       UDS_DEFAULT_RECVBUFSIZE);
	if (R_FAILED(rc)) return false;

	if (udsCheckNodeInfoInitialized(&info->nodes[0])) {
		char name[64] = {0};
		if (R_SUCCEEDED(udsGetNodeInfoUsername(&info->nodes[0], name))) {
			snprintf(peer_name, sizeof(peer_name), "%.31s", name);
		}
	}
	active = true;
	host = false;
	return true;
}

void net_close(void)
{
	if (!active) return;
	if (host)
		udsDestroyNetwork();
	else
		udsDisconnectNetwork();
	udsUnbind(&bindctx);
	active = false;
}

bool net_is_host(void) { return host; }
bool net_peer_lost(void) { return lost; }
const char *net_peer_name(void) { return peer_name[0] ? peer_name : "Opponent"; }

static int connected_nodes(void)
{
	udsConnectionStatus st;
	if (R_FAILED(udsGetConnectionStatus(&st))) return 0;
	return st.total_nodes;
}

bool net_peer_connected(void)
{
	return active && peer_seen && !lost;
}

static void send_raw(const Packet *p)
{
	udsSendTo(UDS_BROADCAST_NETWORKNODEID, DATA_CHANNEL, UDS_SENDFLAG_Default, p, sizeof(*p));
}

void net_send(u8 type, u8 a, u8 b, u8 c, u16 d)
{
	if (outq_len >= QUEUE_LEN) return;
	Packet *p = &outq[(outq_head + outq_len) % QUEUE_LEN];
	memset(p, 0, sizeof(*p));
	p->magic = NET_MAGIC;
	p->kind = PKT_DATA;
	p->type = type;
	p->a = a;
	p->b = b;
	p->c = c;
	p->d = d;
	p->id = next_tx_id++;
	outq_len++;
	if (outq_len == 1) resend_timer = 0; // send right away
}

static void handle_packet(const Packet *p)
{
	if (p->magic != NET_MAGIC) return;
	silent_frames = 0;

	if (p->kind == PKT_ACK) {
		while (outq_len > 0 && outq[outq_head].id <= p->id) {
			outq_head = (outq_head + 1) % QUEUE_LEN;
			outq_len--;
			resend_timer = 0;
		}
		return;
	}
	if (p->kind != PKT_DATA) return;

	if (p->id == last_rx_id + 1 && inq_len < QUEUE_LEN) {
		NetEvent *ev = &inq[(inq_head + inq_len) % QUEUE_LEN];
		ev->type = p->type;
		ev->a = p->a;
		ev->b = p->b;
		ev->c = p->c;
		ev->d = p->d;
		inq_len++;
		last_rx_id = p->id;
	}
	// Acknowledge everything received in order so far (covers duplicates).
	Packet ack = {0};
	ack.magic = NET_MAGIC;
	ack.kind = PKT_ACK;
	ack.id = last_rx_id;
	send_raw(&ack);
}

void net_update(void)
{
	if (!active) return;

	int nodes = connected_nodes();
	if (nodes >= 2) {
		if (!peer_seen) {
			peer_seen = true;
			silent_frames = 0;
			if (host) {
				udsNodeInfo node;
				char name[64] = {0};
				if (R_SUCCEEDED(udsGetNodeInformation(2, &node)) &&
				    R_SUCCEEDED(udsGetNodeInfoUsername(&node, name)))
					snprintf(peer_name, sizeof(peer_name), "%.31s", name);
				// Don't let anyone else join mid-game.
				udsSetNewConnectionsBlocked(true, true, false);
			}
		}
	} else if (peer_seen) {
		lost = true;
	}

	// Receive
	static u32 rxbuf[UDS_DATAFRAME_MAXSIZE / 4 + 1];
	for (int i = 0; i < 8; i++) {
		size_t size = 0;
		u16 src = 0;
		if (R_FAILED(udsPullPacket(&bindctx, rxbuf, sizeof(rxbuf), &size, &src)) || size == 0) break;
		if (size == sizeof(Packet)) handle_packet((const Packet *)rxbuf);
	}

	if (!peer_seen) return;

	if (++silent_frames > TIMEOUT_FRAMES) lost = true;

	// (Re)send the oldest unacknowledged message
	if (outq_len > 0) {
		if (resend_timer-- <= 0) {
			send_raw(&outq[outq_head]);
			resend_timer = RESEND_FRAMES;
		}
	}

	// Keep-alive
	if (++ping_timer >= PING_FRAMES) {
		ping_timer = 0;
		Packet ping = {0};
		ping.magic = NET_MAGIC;
		ping.kind = PKT_PING;
		send_raw(&ping);
	}
}

bool net_poll(NetEvent *ev)
{
	if (inq_len == 0) return false;
	*ev = inq[inq_head];
	inq_head = (inq_head + 1) % QUEUE_LEN;
	inq_len--;
	return true;
}

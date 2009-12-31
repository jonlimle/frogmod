// server.cpp: little more than enhanced multicaster
// runs dedicated or as client coroutine

#include "cube.h"

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/dns.h>

#include "evirc.h"

void conoutfv(int type, const char *fmt, va_list args) {
	string sf, sp;

	vformatstring(sf, fmt, args);
	filtertext(sp, sf);
	puts(sp);
}

void conoutf(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	conoutfv(CON_INFO, fmt, args);
	va_end(args);
}

void conoutf(int type, const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	conoutfv(type, fmt, args);
	va_end(args);
}

void fatal(const char *s, ...) {
	void cleanupserver();

	cleanupserver();
	defvformatstring(msg, s, s);
	server::log("Server Error: %s", msg);
	exit(EXIT_FAILURE);
}

// all network traffic is in 32bit ints, which are then compressed using the following simple scheme (assumes that most values are small).

template < class T > static inline void putint_(T & p, int n) {
	if(n < 128 && n > -127)
		p.put(n);
	else if(n < 0x8000 && n >= -0x8000) {
		p.put(0x80);
		p.put(n);
		p.put(n >> 8);
	} else {
		p.put(0x81);
		p.put(n);
		p.put(n >> 8);
		p.put(n >> 16);
		p.put(n >> 24);
	}
}
void putint(ucharbuf & p, int n) {
	putint_(p, n);
}
void putint(packetbuf & p, int n) {
	putint_(p, n);
}

int getint(ucharbuf & p) {
	int c = (char) p.get();

	if(c == -128) {
		int n = p.get();

		n |= char (p.get()) << 8;

		return n;
	} else if(c == -127) {
		int n = p.get();

		n |= p.get() << 8;
		n |= p.get() << 16;
		return n | (p.get() << 24);
	} else
		return c;
}

// much smaller encoding for unsigned integers up to 28 bits, but can handle signed
template < class T > static inline void putuint_(T & p, int n) {
	if(n < 0 || n >= (1 << 21)) {
		p.put(0x80 | (n & 0x7F));
		p.put(0x80 | ((n >> 7) & 0x7F));
		p.put(0x80 | ((n >> 14) & 0x7F));
		p.put(n >> 21);
	} else if(n < (1 << 7))
		p.put(n);
	else if(n < (1 << 14)) {
		p.put(0x80 | (n & 0x7F));
		p.put(n >> 7);
	} else {
		p.put(0x80 | (n & 0x7F));
		p.put(0x80 | ((n >> 7) & 0x7F));
		p.put(n >> 14);
	}
}
void putuint(ucharbuf & p, int n) {
	putuint_(p, n);
}
void putuint(packetbuf & p, int n) {
	putuint_(p, n);
}

int getuint(ucharbuf & p) {
	int n = p.get();

	if(n & 0x80) {
		n += (p.get() << 7) - 0x80;
		if(n & (1 << 14))
			n += (p.get() << 14) - (1 << 14);
		if(n & (1 << 21))
			n += (p.get() << 21) - (1 << 21);
		if(n & (1 << 28))
			n |= 0xF0000000;
	}
	return n;
}

template < class T > static inline void putfloat_(T & p, float f) {
	lilswap(&f, 1);
	p.put((uchar *) & f, sizeof(float));
}
void putfloat(ucharbuf & p, float f) {
	putfloat_(p, f);
}
void putfloat(packetbuf & p, float f) {
	putfloat_(p, f);
}

float getfloat(ucharbuf & p) {
	float f;

	p.get((uchar *) & f, sizeof(float));
	return lilswap(f);
}

template < class T > static inline void sendstring_(const char *t, T & p) {
	while(*t) putint(p, *t++);
	putint(p, 0);
}
void sendstring(const char *t, ucharbuf & p) {
	sendstring_(t, p);
}
void sendstring(const char *t, packetbuf & p) {
	sendstring_(t, p);
}

void getstring(char *text, ucharbuf & p, int len) {
	char *t = text;

	do {
		if(t >= &text[len]) {
			text[len - 1] = 0;
			return;
		}
		if(!p.remaining()) {
			*t = 0;
			return;
		}
		*t = getint(p);
	}
	while(*t++);
}

void filtertext(char *dst, const char *src, bool whitespace, int len) {
	for(int c = *src; c; c = *++src) {
		switch (c) {
		  case '\f':
			  ++src;
			  continue;
		}
		if(isspace(c) ? whitespace : isprint(c)) {
			*dst++ = c;
			if(!--len)
				break;
		}
	}
	*dst = '\0';
}

enum { ST_EMPTY, ST_LOCAL, ST_TCPIP };

struct client { // server side version of "dynent" type
	int type;
	int num;
	ENetPeer *peer;
	string ipstr, hostname;
#ifdef HAVE_GEOIP
	string country;
#endif
	void *info;
};

vector <client *>clients;
ENetHost *serverhost = NULL;
size_t bsend = 0, brec = 0;
size_t tx_packets = 0, rx_packets = 0, tx_bytes = 0, rx_bytes = 0;
int laststatus = 0;
ENetSocket pongsock = ENET_SOCKET_NULL, lansock = ENET_SOCKET_NULL;

#ifdef HAVE_GEOIP
GeoIP *geoip;
#endif

event_base *evbase;
evdns_base *dnsbase;
event serverhost_input_event;
event pongsock_input_event;
event lansock_input_event;
event update_event;
event netstats_event;
event stdin_event;
IRC::Client irc;

void cleanupserver() {
	server::log("Cleaning up...");
	server::writecfg();

	if(serverhost)
		enet_host_destroy(serverhost);
	serverhost = NULL;

	if(pongsock != ENET_SOCKET_NULL)
		enet_socket_destroy(pongsock);
	if(lansock != ENET_SOCKET_NULL)
		enet_socket_destroy(lansock);
	pongsock = lansock = ENET_SOCKET_NULL;

#ifdef HAVE_GEOIP
	GeoIP_delete(geoip);
#endif
}

void cleanupsig(int sig) {
	server::log("Caught %s signal (%d)\n", sys_siglist[sig], sig);
// no need to call cleanupserver() cause it's handled by atexit();
	exit(0);
}

void process(ENetPacket * packet, int sender, int chan);

//void disconnect_client(int n, int reason);

void *getclientinfo(int i) {
	return !clients.inrange(i) || clients[i]->type == ST_EMPTY ? NULL : clients[i]->info;
}
int getnumclients() {
	return clients.length();
}
uint getclientip(int n) {
	return clients.inrange(n)
		&& clients[n]->type == ST_TCPIP ? clients[n]->peer->address.host : 0;
}
char *getclientipstr(int n) {
	return clients.inrange(n) && clients[n]->type == ST_TCPIP ? clients[n]->ipstr : 0;
}
char *getclienthostname(int n) {
	return clients.inrange(n) && clients[n]->type == ST_TCPIP ? clients[n]->hostname : 0;
}
#ifdef HAVE_GEOIP
const char *getclientcountrynul(int n) {
	return clients.inrange(n) && clients[n]->type == ST_TCPIP ? (clients[n]->country[0] ? clients[n]->country : 0) : 0;
}
const char *getclientcountry(int n) {
	return clients.inrange(n) && clients[n]->type == ST_TCPIP ? (clients[n]->country[0] ? clients[n]->country : "Unknown") : 0;
}
#endif

void sendpacket(int n, int chan, ENetPacket * packet, int exclude) {
	if(n < 0) {
		server::recordpacket(chan, packet->data, packet->dataLength);
		loopv(clients) if(i != exclude && server::allowbroadcast(i))
			sendpacket(i, chan, packet);
		return;
	}
	switch (clients[n]->type) {
	  case ST_TCPIP:
		  {
			  enet_peer_send(clients[n]->peer, chan, packet);
			  bsend += packet->dataLength;
			  break;
		  }
	}
}

void sendf(int cn, int chan, const char *format, ...) {
	int exclude = -1;

	bool reliable = false;

	if(*format == 'r') {
		reliable = true;
		++format;
	}
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS,
											reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

	ucharbuf p(packet->data, packet->dataLength);

	va_list args;

	va_start(args, format);
	while(*format)
		switch (*format++) {
		  case 'x':
			  exclude = va_arg(args, int);

			  break;

		  case 'v':
			  {
				  int n = va_arg(args, int);

				  int *v = va_arg(args, int *);

				  loopi(n) putint(p, v[i]);
				  break;
			  }

		  case 'i':
			  {
				  int n = isdigit(*format) ? *format++ - '0' : 1;

				  loopi(n) putint(p, va_arg(args, int));

				  break;
			  }
		  case 'f':
			  {
				  int n = isdigit(*format) ? *format++ - '0' : 1;

				  loopi(n) putfloat(p, (float) va_arg(args, double));

				  break;
			  }
		  case 's':
			  sendstring(va_arg(args, const char *), p);

			  break;
		  case 'm':
			  {
				  int n = va_arg(args, int);

				  enet_packet_resize(packet, packet->dataLength + n);
				  p.buf = packet->data;
				  p.maxlen += n;
				  p.put(va_arg(args, uchar *), n);
				  break;
			  }
		}
	va_end(args);
	enet_packet_resize(packet, p.length());
	sendpacket(cn, chan, packet, exclude);
	if(packet->referenceCount == 0)
		enet_packet_destroy(packet);
}

void sendfile(int cn, int chan, stream * file, const char *format, ...) {
	if(cn < 0)
		return;
	else if(!clients.inrange(cn))
		return;

	int len = file->size();

	if(len <= 0)
		return;

	bool reliable = false;

	if(*format == 'r') {
		reliable = true;
		++format;
	}
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS + len, ENET_PACKET_FLAG_RELIABLE);

	ucharbuf p(packet->data, packet->dataLength);

	va_list args;

	va_start(args, format);
	while(*format)
		switch (*format++) {
		  case 'i':
			  {
				  int n = isdigit(*format) ? *format++ - '0' : 1;

				  loopi(n) putint(p, va_arg(args, int));

				  break;
			  }
		  case 's':
			  sendstring(va_arg(args, const char *), p);

			  break;
		  case 'l':
			  putint(p, len);
			  break;
		}
	va_end(args);
	enet_packet_resize(packet, p.length() + len);

	file->seek(0, SEEK_SET);
	file->read(&packet->data[p.length()], len);
	enet_packet_resize(packet, p.length() + len);

	if(cn >= 0)
		sendpacket(cn, chan, packet, -1);
	if(!packet->referenceCount)
		enet_packet_destroy(packet);
}

const char *disc_reasons[] = {
	"normal", "end of packet", "client num", "kicked/banned", "tag type",
	"ip is banned", "server is in private mode", "server FULL (maxclients)",
	"connection timed out"
};

void disconnect_client(int n, int reason) {
	if(clients[n]->type != ST_TCPIP)
		return;

	server::message("Client \f3%s\f7 disconnected because: \f6%s\f7.", clients[n]->ipstr, disc_reasons[reason]);
	irc.speak(1, "\00314Client %s disconnected because: \00305%s\00315.", clients[n]->ipstr, disc_reasons[reason]);

	enet_peer_disconnect(clients[n]->peer, reason);
	server::clientdisconnect(n, reason);
	clients[n]->type = ST_EMPTY;
	clients[n]->peer->data = NULL;
	server::deleteclientinfo(clients[n]->info);
	clients[n]->info = NULL;
}

void kicknonlocalclients(int reason) {
	loopv(clients) if(clients[i]->type == ST_TCPIP)
		disconnect_client(i, reason);
}

void process(ENetPacket * packet, int sender, int chan)	// sender may be -1
{
	packetbuf p(packet);

	server::parsepacket(sender, chan, p);
	if(p.overread()) {
		disconnect_client(sender, DISC_EOP);
		return;
	}
}

void localclienttoserver(int chan, ENetPacket * packet) {
	client *c = NULL;

	loopv(clients) if(clients[i]->type == ST_LOCAL) {
		c = clients[i];
		break;
	}
	if(c)
		process(packet, c->num, chan);
}

client & addclient() {
	loopv(clients) if(clients[i]->type == ST_EMPTY) {
		clients[i]->info = server::newclientinfo();
		return *clients[i];
	}
	client *c = new client;

	c->num = clients.length();
	c->info = server::newclientinfo();
	clients.add(c);
	return *c;
}

int localclients = 0, nonlocalclients = 0;

bool hasnonlocalclients() {
	return nonlocalclients != 0;
}
bool haslocalclients() {
	return localclients != 0;
}

bool resolverwait(const char *name, ENetAddress * address) {
	return enet_address_set_host(address, name) >= 0;
}

int connectwithtimeout(ENetSocket sock, const char *hostname, const ENetAddress & remoteaddress) {
	int result = enet_socket_connect(sock, &remoteaddress);

	if(result < 0)
		enet_socket_destroy(sock);
	return result;
}

ENetAddress serveraddress = {ENET_HOST_ANY, ENET_PORT_ANY};

static ENetAddress pongaddr;

void sendserverinforeply(ucharbuf & p) {
	ENetBuffer buf;

	buf.data = p.buf;
	buf.dataLength = p.length();
	enet_socket_send(pongsock, &pongaddr, &buf, 1);
}

#define DEFAULTCLIENTS 8

VARF(maxclients, 0, DEFAULTCLIENTS, MAXCLIENTS, {
	 if(!maxclients) maxclients = DEFAULTCLIENTS;}

);
VAR(serveruprate, 0, 0, INT_MAX);
SVAR(serverip, "");
VARF(serverport, 0, server::serverport(), 0xFFFF, {
	 if(!serverport) serverport = server::serverport();}

);

int64_t curtime = 0, lastmillis = 0, totalmillis = 0;

void update_server(int fd, short e, void *arg) {
	timeval to;

	to.tv_sec = 0;
	to.tv_usec = 5000;
	evtimer_add(&update_event, &to);

	localclients = nonlocalclients = 0;
	loopv(clients) switch (clients[i]->type) {
	  case ST_LOCAL:
		  localclients++;
		  break;
	  case ST_TCPIP:
		  nonlocalclients++;
		  break;
	}

	int64_t millis = get_ticks();

	curtime = millis - totalmillis;
	lastmillis = totalmillis = millis;

	server::serverupdate();
}

void rdnscb(int result, char type, int count, int ttl, void *addresses, void *arg) {
	unsigned long ip = (unsigned long)arg;
	if(result == DNS_ERR_NONE) {
		if(type == DNS_PTR && count >= 1) {
			for(int i = clients.length() - 1; i >= 0; i--) {
				if(clients[i]->type == ST_TCPIP && clients[i]->peer->address.host == ip) {
					copystring(clients[i]->hostname, ((char **)addresses)[0]);
					server::gothostname(clients[i]->info);
				}
			}
		}
	}
}

void serverhost_process_event(ENetEvent & event) {
	switch (event.type) {
	  case ENET_EVENT_TYPE_CONNECT:
		  {
			  client & c = addclient();
			  c.type = ST_TCPIP;
			  c.peer = event.peer;
			  c.peer->data = &c;
			  char hn[1024];

			  copystring(c.ipstr, (enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn)) == 0) ? hn : "");
#ifdef HAVE_GEOIP
			  const char *country = GeoIP_country_name_by_ipnum(geoip, endianswap32(c.peer->address.host));
			  if(country) copystring(c.country, country);
			  else c.country[0] = 0;
#endif
			  c.hostname[0] = 0; // FIXME: reverse lookup
			  evdns_base_resolve_reverse(dnsbase, (in_addr *)&c.peer->address.host, 0, rdnscb, (void *)c.peer->address.host);
			  printf("Client connected (%s)\n", c.ipstr);
			  int reason = server::clientconnect(c.num, c.peer->address.host);

			  if(!reason)
				  nonlocalclients++;
			  else
				  disconnect_client(c.num, reason);
			  break;
		  }
	  case ENET_EVENT_TYPE_RECEIVE:
		  {
			  brec += event.packet->dataLength;
			  rx_bytes += event.packet->dataLength;
			  rx_packets++;
			  client *c = (client *) event.peer->data;

			  if(c)
				  process(event.packet, c->num, event.channelID);
			  if(event.packet->referenceCount == 0)
				  enet_packet_destroy(event.packet);
			  break;
		  }
	  case ENET_EVENT_TYPE_DISCONNECT:
		  {
			  client *c = (client *) event.peer->data;

			  if(!c)
				  break;
			  server::clientdisconnect(c->num);
			  nonlocalclients--;
			  c->type = ST_EMPTY;
			  event.peer->data = NULL;
			  server::deleteclientinfo(c->info);
			  c->info = NULL;
			  break;
		  }
	  default:
		  break;
	}
}

static void serverhost_input(int fd, short e, void *arg) {
	if(!(e & EV_READ)) return;
	ENetEvent event;
	while(enet_host_service(serverhost, &event, 0) == 1) serverhost_process_event(event);
	if(server::sendpackets()) enet_host_flush(serverhost); //treat EWOULDBLOCK as packet loss
}

static void serverinfo_input(int fd, short e, void *arg) {
	if(!(e & EV_READ)) return;
	ENetBuffer buf;
	uchar pong[MAXTRANS];
	buf.data = pong;
	buf.dataLength = sizeof(pong);
	int len = enet_socket_receive(fd, &pongaddr, &buf, 1);
	if(len < 0) return;
	ucharbuf req(pong, len), p(pong, sizeof(pong));
	p.len += len;
	server::serverinforeply(req, p);
}

void netstats_event_handler(int, short, void *) {
	if(nonlocalclients || bsend || brec)
		printf("status: %d remote clients, %.1f send, %.1f rec (K/sec)\n", nonlocalclients, bsend / 60.0f / 1024, brec / 60.0f / 1024);
	bsend = brec = 0;
	timeval one_min;
	one_min.tv_sec = 60;
	one_min.tv_usec = 0;
	DEBUGF(event_add(&netstats_event, &one_min));
}


static bufferevent *stdinbuf;
static void stdinreadcb(struct bufferevent *buf, void *arg) {
	char *ln;
	while((ln = evbuffer_readln(bufferevent_get_input(buf), NULL, EVBUFFER_EOL_ANY))) {
		execute(ln);
		free(ln);
	}
}
static void stdinwritecb(struct bufferevent *buf, void *arg) {
}
void initconsole();
static void stdineventcb(struct bufferevent *buf, short what, void *arg) {
	if(what & BEV_EVENT_EOF) {
		printf("EOF ignored. Press CTRL+C or CTRL+\\ to exit.\n");
		initconsole();
	}
}
void initconsole() {
	if(stdinbuf) DEBUGF(bufferevent_free(stdinbuf))
	else printf("Console initialized. You may type commands now.\n");
	DEBUGF(stdinbuf = bufferevent_socket_new(evbase, 0, (bufferevent_options)0));
	DEBUGF(bufferevent_enable(stdinbuf, EV_READ));
	DEBUGF(bufferevent_setcb(stdinbuf, stdinreadcb, stdinwritecb, stdineventcb, NULL));
}

bool servererror(const char *desc) {
	fatal(desc);
	return false;
}

static const char *initfile = "server-init.cfg";
bool serveroption(char *opt) {
	switch (opt[1]) {
	  case 'u':
		  setvar("serveruprate", atoi(opt + 2));
		  return true;
	  case 'c':
		  setvar("maxclients", atoi(opt + 2));
		  return true;
	  case 'i':
		  setsvar("serverip", opt + 2);
		  return true;
	  case 'j':
		  setvar("serverport", atoi(opt + 2));
		  return true;
	  case 'm':
		  setsvar("mastername", opt + 2);
		  setvar("updatemaster", (opt + 2) ? 1 : 0);
		  return true;
	  case 'q':
		  server::log("Using home directory: %s", opt + 2);
		  sethomedir(opt + 2);
		  return true;
	  case 'k':
		  server::log("Adding package directory: %s", opt + 2);
		  addpackagedir(opt + 2);
		  return true;
	  case 'f':
		initfile = opt + 2;
		return true;
	  default:
		  return false;
	}
}

int main(int argc, char *argv[]) {
	if(enet_initialize() < 0)
		fatal("Unable to initialise network module");
	atexit(enet_deinitialize);
	enet_time_set(0);
	reset_ticks();

	atexit(cleanupserver);
	signal(SIGINT, cleanupsig);
	signal(SIGQUIT, cleanupsig);

	for(int i = 1; i < argc; i++)
		if(!serveroption(argv[i]) && !server::serveroption(argv[i]))
			server::log("WARNING: Unknown command-line option: %s", argv[i]);

	printf("Initializing server...\n");

	evbase = event_base_new();
	dnsbase = evdns_base_new(evbase, 1);
	event_base_priority_init(evbase, 10);
	irc.base = evbase;
	irc.dnsbase = dnsbase;

	printf("Executing [%s]\n", initfile);
	execfile(initfile, false);

	initconsole();

	printf("Setting up listen server...\n");
	ENetAddress address = { ENET_HOST_ANY,
		serverport <= 0 ? server::serverport() : serverport
	};
	if(*serverip) {
		if(enet_address_set_host(&address, serverip) < 0)
			server::log("WARNING: server ip not resolved");
		else
			serveraddress.host = address.host;
	}
	serverhost = enet_host_create(&address, min(maxclients + server::reserveclients(), MAXCLIENTS), 0, serveruprate);
	if(!serverhost)
		return servererror("Could not create server host. Please check for an already running server on the same port.");
	loopi(maxclients) serverhost->peers[i].data = NULL;
	address.port = server::serverinfoport(serverport > 0 ? serverport : -1);
	pongsock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if(pongsock != ENET_SOCKET_NULL && enet_socket_bind(pongsock, &address) < 0) {
		enet_socket_destroy(pongsock);
		pongsock = ENET_SOCKET_NULL;
	}
	if(pongsock == ENET_SOCKET_NULL)
		return servererror("Could not create server info socket.");
	else
		enet_socket_set_option(pongsock, ENET_SOCKOPT_NONBLOCK, 1);
	address.port = server::laninfoport();
	lansock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if(lansock != ENET_SOCKET_NULL
	   && (enet_socket_set_option(lansock, ENET_SOCKOPT_REUSEADDR, 1) < 0 || enet_socket_bind(lansock, &address) < 0)) {
		enet_socket_destroy(lansock);
		lansock = ENET_SOCKET_NULL;
	}
	if(lansock == ENET_SOCKET_NULL)
		server::log("WARNING: Could not create LAN server info socket.");
	else
		enet_socket_set_option(lansock, ENET_SOCKOPT_NONBLOCK, 1);

	event_assign(&serverhost_input_event, evbase, serverhost->socket, EV_READ | EV_PERSIST, &serverhost_input, NULL);
	event_add(&serverhost_input_event, NULL);
	event_priority_set(&serverhost_input_event, 1);

	event_assign(&pongsock_input_event, evbase, pongsock, EV_READ | EV_PERSIST, &serverinfo_input, NULL);
	event_add(&pongsock_input_event, NULL);

	event_assign(&lansock_input_event, evbase, lansock, EV_READ | EV_PERSIST, &serverinfo_input, NULL);
	event_add(&lansock_input_event, NULL);

#ifdef HAVE_GEOIP
	printf("Initializing GeoIP\n");
	geoip = GeoIP_new(GEOIP_STANDARD);
#endif

	printf("Initializing game server...\n");
	server::serverinit();

	initmasterserver();

	printf("Running dedicated server...\n");
#ifdef WIN32
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

	printf("Dedicated server started, waiting for clients...\n\n");
	fflush(stdout);
	fflush(stderr);

	timeval five_ms;
	five_ms.tv_sec = 0;
	five_ms.tv_usec = 5000;
	evtimer_assign(&update_event, evbase, &update_server, NULL);
	evtimer_add(&update_event, &five_ms);

	timeval one_min;
	one_min.tv_sec = 60;
	one_min.tv_usec = 0;
	evtimer_assign(&netstats_event, evbase, &netstats_event_handler, NULL);
	event_add(&netstats_event, &one_min);

	event_base_dispatch(evbase);

	return 0;
}

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/dns.h>
#include <errno.h>

#include "cube.h"

VARN(updatemaster, allowupdatemaster, 0, 1, 1);
SVAR(mastername, server::defaultmaster());

bufferevent *masterbuf = NULL;
in_addr_t masteraddr = 0;
event registermaster_timer;
event masterdns_timer;

extern int serverport;
void updatemasterserver() {
	if(!mastername[0] || !allowupdatemaster) return;
	printf("Updating master server...\n");
	requestmasterf("regserv %d\n", serverport);
}
COMMAND(updatemasterserver, "");

void registermaster_timer_handler(int, short, void *) {
	updatemasterserver();
	timeval one_hr; one_hr.tv_sec = 3600; one_hr.tv_usec = 0;
	DEBUGF(event_add(&registermaster_timer, &one_hr));
}

static void masterreadcb(struct bufferevent *buf, void *arg) {
	char *ln;
	while((ln = evbuffer_readln(bufferevent_get_input(buf), NULL, EVBUFFER_EOL_ANY))) {
		char *args = ln;
		while(*args && !isspace(*args)) args++;
		int cmdlen = args - ln;
		while(*args && isspace(*args)) args++;
		if(!strncmp(ln, "failreg", cmdlen)) {
			server::log("Master server registration failed: %s", args);
			irc.speak("\00314Master server registration failed: %s", args);
		} else if(!strncmp(ln, "succreg", cmdlen)) {
			server::log("Master server registration succeeded.");
			irc.speak("\00314Master server registration succeeded.");
		} else
			server::processmasterinput(ln, cmdlen, args);
		free(ln);
	}
}

static void masterwritecb(struct bufferevent *buf, void *arg) {
}

static void mastereventcb(struct bufferevent *buf, short what, void *arg) {
	if(what == BEV_EVENT_CONNECTED) {
		printf("Connected to masterserver\n");
		DEBUGF(bufferevent_enable(masterbuf, EV_READ));
	} else {
		if(what != (BEV_EVENT_EOF & EV_READ)) bufferevent_print_error(what, "Disconnected from \"%s\" master server:", mastername);
		struct sockaddr_in addr;
		addr.sin_addr.s_addr = masteraddr;
		addr.sin_port = htons(server::masterport());
		addr.sin_family = AF_INET;

		DEBUGF(bufferevent_free(masterbuf));
		DEBUGF(masterbuf = bufferevent_socket_new(evbase, -1, BEV_OPT_CLOSE_ON_FREE));
		DEBUGF(bufferevent_setcb(masterbuf, masterreadcb, masterwritecb, mastereventcb, NULL));
		DEBUGF(bufferevent_socket_connect(masterbuf, (sockaddr *)&addr, sizeof(struct sockaddr_in)));
	}
}

bool requestmasterf(const char *fmt, ...) {
	if(!mastername[0] || !allowupdatemaster) return false;

	va_list ap;
	va_start(ap, fmt);
	bufferevent_write_vprintf(masterbuf, fmt, ap);
//	vprintf(fmt, ap);
	va_end(ap);

	return true;
}

static void masterdnscb(int result, char type, int count, int ttl, void *addresses, void *arg) {
	if(result == DNS_ERR_NONE) {
		if(type == DNS_IPv4_A) {
			masteraddr = ((in_addr_t *)addresses)[0];
			struct sockaddr_in addr;
			addr.sin_addr.s_addr = masteraddr;
			addr.sin_port = htons(server::masterport());
			addr.sin_family = AF_INET;
			DEBUGF(bufferevent_socket_connect(masterbuf, (sockaddr *)&addr, sizeof(struct sockaddr_in)));
		}
	} else {
		evdns_print_error(result, "Error resolving %s:", mastername);
		timeval ten_secs;
		ten_secs.tv_sec = 10;
		ten_secs.tv_usec = 0;
		DEBUGF(event_add(&masterdns_timer, &ten_secs));
	}
}

void masterdns_timer_handler(int, short, void *) {
	printf("Resolving \"%s\"...\n", mastername);
	evdns_base_resolve_ipv4(dnsbase, mastername, 0, masterdnscb, NULL);
}

void initmasterserver() {
	if(!mastername[0] || !allowupdatemaster) {
		printf("Not registering with master server.\n");
		return;
	}

	DEBUGF(evtimer_assign(&registermaster_timer, evbase, &registermaster_timer_handler, NULL));
	DEBUGF(evtimer_assign(&masterdns_timer, evbase, &masterdns_timer_handler, NULL));
	DEBUGF(masterbuf = bufferevent_socket_new(evbase, -1, BEV_OPT_CLOSE_ON_FREE));
	DEBUGF(bufferevent_setcb(masterbuf, masterreadcb, masterwritecb, mastereventcb, NULL));
	DEBUGF(registermaster_timer_handler(0, 0, NULL));
	DEBUGF(masterdns_timer_handler(0, 0, NULL));
}

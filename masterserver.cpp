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

extern ENetAddress serveraddress;
static int mkmastersock() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd && evutil_make_socket_nonblocking(fd)>=0 && serveraddress.host) {
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = serveraddress.host;
		addr.sin_port = 0;
		bind(fd, (sockaddr *)&addr, sizeof(addr));
	}
	return fd;
}

static void mastereventcb(struct bufferevent *buf, short what, void *arg) {
	printf("mastereventcb\n");
	if(what == BEV_EVENT_CONNECTED) {
		printf("Connected to masterserver\n");
	} else {
		if(what != (BEV_EVENT_EOF & EV_READ)) bufferevent_print_error(what, "Disconnected from \"%s\" master server:", mastername);
		DEBUGF(bufferevent_free(masterbuf));
		if(errno == ECONNREFUSED) {
			allowupdatemaster = false;
			return; // don't try to reconnect if connection is refused.
		}

		DEBUGF(masterbuf = bufferevent_socket_new(evbase, mkmastersock(), BEV_OPT_CLOSE_ON_FREE));
		DEBUGF(bufferevent_enable(masterbuf, EV_READ));
		DEBUGF(bufferevent_setcb(masterbuf, masterreadcb, masterwritecb, mastereventcb, NULL));
		bufferevent_socket_connect_hostname(masterbuf, dnsbase, AF_UNSPEC, mastername, server::masterport());
	}
}

bool requestmasterf(const char *fmt, ...) {
	if(!mastername[0] || !allowupdatemaster) return false;

	va_list ap;
	va_start(ap, fmt);
	bufferevent_write_vprintf(masterbuf, fmt, ap);
	va_end(ap);

	return true;
}

void initmasterserver() {
	if(!mastername[0] || !allowupdatemaster) {
		printf("Not registering with master server.\n");
		return;
	}

	DEBUGF(evtimer_assign(&registermaster_timer, evbase, &registermaster_timer_handler, NULL));

	DEBUGF(masterbuf = bufferevent_socket_new(evbase, mkmastersock(), BEV_OPT_CLOSE_ON_FREE));
	DEBUGF(bufferevent_setcb(masterbuf, masterreadcb, masterwritecb, mastereventcb, NULL));
	DEBUGF(bufferevent_enable(masterbuf, EV_READ));
	DEBUGF(registermaster_timer_handler(0, 0, NULL));
	bufferevent_socket_connect_hostname(masterbuf, dnsbase, AF_UNSPEC, mastername, server::masterport());
}

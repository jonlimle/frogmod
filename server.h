#define MAXCLIENTS 128                 // DO NOT set this any higher
#define MAXTRANS 5000                  // max amount of data to swallow in 1 go


extern int maxclients;

enum { DISC_NONE = 0, DISC_EOP, DISC_CN, DISC_KICK, DISC_TAGT, DISC_IPBAN, DISC_PRIVATE, DISC_MAXCLIENTS, DISC_TIMEOUT, DISC_NUM };
extern const char *disc_reasons[];

extern void *getclientinfo(int i);
extern void sendf(int cn, int chan, const char *format, ...);
extern void sendfile(int cn, int chan, stream *file, const char *format = "", ...);
extern void sendpacket(int cn, int chan, ENetPacket *packet, int exclude = -1);
extern int getnumclients();

extern uint getclientip(int n);
extern char *getclientipstr(int n);
extern char *getclienthostname(int n);
#ifdef HAVE_GEOIP
extern const char *getclientcountrynul(int n); // returns "" if country is not found
extern const char *getclientcountry(int n); // returns Unknown if country is not found
#endif

extern void putint(ucharbuf &p, int n);
extern void putint(packetbuf &p, int n);
extern int getint(ucharbuf &p);
extern void putuint(ucharbuf &p, int n);
extern void putuint(packetbuf &p, int n);
extern int getuint(ucharbuf &p);
extern void putfloat(ucharbuf &p, float f);
extern void putfloat(packetbuf &p, float f);
extern float getfloat(ucharbuf &p);
extern void sendstring(const char *t, ucharbuf &p);
extern void sendstring(const char *t, packetbuf &p);
extern void getstring(char *t, ucharbuf &p, int len = MAXTRANS);
extern void filtertext(char *dst, const char *src, bool whitespace = true, int len = sizeof(string)-1);
extern void localconnect();
extern void disconnect_client(int n, int reason);
extern void kicknonlocalclients(int reason = DISC_NONE);
extern bool hasnonlocalclients();
extern bool haslocalclients();
extern void sendserverinforeply(ucharbuf &p);
extern bool requestmaster(const char *req);
extern bool requestmasterf(const char *fmt, ...);

#ifdef HAVE_GEOIP
#include <GeoIP.h>
extern GeoIP *geoip;
#endif

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/dns.h>
extern event_base *evbase;
extern evdns_base *dnsbase;

#include "evirc.h"
extern IRC::Client irc;

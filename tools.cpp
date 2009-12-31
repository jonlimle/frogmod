// implementation of generic tools

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>

#include "cube.h"
#include "sha1.h"

////////////////////////// rnd numbers ////////////////////////////////////////

#define N              (624)             
#define M              (397)                
#define K              (0x9908B0DFU)       
#define hiBit(u)       ((u) & 0x80000000U)  
#define loBit(u)       ((u) & 0x00000001U)  
#define loBits(u)      ((u) & 0x7FFFFFFFU)  
#define mixBits(u, v)  (hiBit(u)|loBits(v)) 

static uint state[N+1];     
static uint *next;          
static int left = -1;     

void seedMT(uint seed)
{
    register uint x = (seed | 1U) & 0xFFFFFFFFU, *s = state;
    register int j;
    for(left=0, *s++=x, j=N; --j; *s++ = (x*=69069U) & 0xFFFFFFFFU);
}

uint reloadMT(void)
{
    register uint *p0=state, *p2=state+2, *pM=state+M, s0, s1;
    register int j;
    if(left < -1) seedMT(time(NULL));
    left=N-1, next=state+1;
    for(s0=state[0], s1=state[1], j=N-M+1; --j; s0=s1, s1=*p2++) *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    for(pM=state, j=M; --j; s0=s1, s1=*p2++) *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    s1=state[0], *p0 = *pM ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
    s1 ^= (s1 >> 11);
    s1 ^= (s1 <<  7) & 0x9D2C5680U;
    s1 ^= (s1 << 15) & 0xEFC60000U;
    return(s1 ^ (s1 >> 18));
}

uint randomMT(void)
{
    uint y;
    if(--left < 0) return(reloadMT());
    y  = *next++;
    y ^= (y >> 11);
    y ^= (y <<  7) & 0x9D2C5680U;
    y ^= (y << 15) & 0xEFC60000U;
    return(y ^ (y >> 18));
}

static int64_t time_base = 0;

void reset_ticks() {
	time_base = 0;
	time_base = get_ticks();
}

int64_t get_ticks() {
	struct timeval tv;
	if(gettimeofday(&tv, NULL) < 0) return 0;

	return tv.tv_usec / 1000LL + tv.tv_sec * 1000LL - time_base;
}

void Wrapper::append(const char *fmt, ...) {
	defvformatstring(str, fmt, fmt);

	if(lines.length() == 0) { lines.add(); lines[0].s[0] = 0; }

	char *line = lines[lines.length() - 1].s;

	int sl = strlen(sep);
	int l = strlen(str);

	if(line[0]) l += sl;

	if(l + (int)strlen(line) >= maxlen) {
		lines.add(); line = lines[lines.length() - 1].s;
		line[0] = 0;
	}

	if(l < maxlen) {
		if(line[0]) strcat(line, sep);
			strcat(line, str);
	} else { // line too long, so split it
		char *c = str;
		while(l > 0) {
			strncat(line, c, maxlen-1);
			lines.add(); line = lines[lines.length() - 1].s;
			line[0] = 0;
			l -= maxlen-1;
			c += maxlen-1;
		}
	}
}

// convert a ip string to an ip
int ipint(char *str) {
	ENetAddress adr;
	if(enet_address_set_host(&adr, str) > -1) return adr.host;
	return 0;
}

char *ipstr(unsigned int ip) {
	static int n = 0;
	static string t[3];
	n = (n + 1)%3;
	ENetAddress adr;
	adr.host = ip;
	enet_address_get_host_ip(&adr, t[n], MAXSTRLEN);
	return t[n];
}

char *timestr(int64_t time) {
	static int n = 0;
	static string t[3];
	n = (n + 1)%3;
	time /= 1000LL; // miliseconds
	if(time / 3600LL >= 24LL) // more than a day
		sprintf(t[n], "%lldd%lld:%02lld:%02lld", time / 3600 / 24, (time / 3600) % 24, (time / 60) % 60, time % 60);
	else
		sprintf(t[n], "%lld:%02lld:%02lld", time / 3600, (time / 60) % 60, time % 60);
	return t[n];
}

char *trim(char *str) {
	if(!str) return NULL;
	while(*str && isspace(*str)) str++;
	int l = strlen(str) - 1;
	while(l >= 0 && isspace(str[l])) str[l--] = 0;
	return str;
}

int strcmpf(char *s1, const char *s2fmt, ...) {
	defvformatstring(s2, s2fmt, s2fmt);
	return strncmp(s1, s2, MAXSTRLEN-1);
}

void getsha1(char *pwd, char *hash, int maxlen) {
	SHA1 sha;
	sha.Reset();
	sha.Input(pwd, min((int)strlen(pwd), MAXSTRLEN));
	unsigned message_digest[5];
	sha.Result(message_digest);
	snprintf(hash, maxlen, "%08X%08X%08X%08X%08X", message_digest[0], message_digest[1], message_digest[2], message_digest[3], message_digest[4]);
}

#ifndef _GNU_SOURCE
char *strndup(const char *s, size_t n) {
	size_t l = strlen(s);
	if(l <= n) return strdup(s);
	char *s2 = (char *)malloc(n + 1);
	strncpy(s2, s, n);
	s2[n] = 0;
	return s2;
}
#endif

char *evbuffer_readln_nul(struct evbuffer *buffer, size_t *n_read_out, enum evbuffer_eol_style eol_style) {
	size_t len;
	char *result = evbuffer_readln(buffer, n_read_out, eol_style);
	if(result) return result;
	len = evbuffer_get_length(buffer);
	if(len == 0) return NULL;
	if(!(result = (char *)malloc(len+1))) return NULL;
	evbuffer_remove(buffer, result, len);
	result[len] = '\0';
	if(n_read_out) *n_read_out = len;
	return result;
}

static void froghttp_reqcb(evhttp_request *req, void *arg) {
	HttpQuery *q = (HttpQuery *)arg;
	if(q->cb) q->cb(req, q->arg);
	delete q;
}

static void froghttp_dnscb(int result, char type, int count, int ttl, void *addresses, void *arg) {
	HttpQuery *q = (HttpQuery *)arg;
	if(result == DNS_ERR_NONE) {
		if(type == DNS_IPv4_A) {
			char *ipstr = inet_ntoa(((in_addr *)addresses)[0]);
			evhttp_request *req = evhttp_request_new(froghttp_reqcb, arg);
			evkeyvalq *headers = evhttp_request_get_output_headers(req);
			evhttp_add_header(headers, "Host", q->url.hostname); // fix for HTTP/1.1
			evhttp_connection *con = evhttp_connection_base_new(q->base, ipstr, q->url.port);
			evhttp_make_request(con, req, EVHTTP_REQ_GET, q->url.full);
			return;
		} else printf("%s: type != DNS_IPv4_A\n", __func__);
	} else {
		printf("DNS error:");
#define DNSERR(x) if(result == DNS_ERR_##x) printf(" DNS_ERR_" #x);
		DNSERR(NONE); DNSERR(FORMAT); DNSERR(SERVERFAILED); DNSERR(NOTEXIST); DNSERR(NOTIMPL); DNSERR(REFUSED);
		DNSERR(TRUNCATED); DNSERR(UNKNOWN); DNSERR(TIMEOUT); DNSERR(SHUTDOWN); DNSERR(CANCEL);
		printf("\n");
	}

	delete q; // error case only
}

void froghttp_get(event_base *base, evdns_base *dnsbase, char *url, void(*cb)(evhttp_request *, void *), void *arg) {
	HttpQuery *q = new HttpQuery;
	q->base = base;
	q->dnsbase = dnsbase;
	q->url.parse(url);
	q->arg = arg;
	q->cb = cb;
	evdns_base_resolve_ipv4(dnsbase, q->url.hostname, 0, froghttp_dnscb, q);
}

void bufferevent_print_error(short what, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

#define checkerr(s) { if(what & BEV_EVENT_##s) printf(" %s", #s); }
	checkerr(CONNECTED);
	checkerr(READING);
	checkerr(WRITING);
	checkerr(EOF);
	checkerr(ERROR);
	checkerr(TIMEOUT);
	printf(" errno=%d \"%s\"\n", errno, strerror(errno));
}

void evdns_print_error(int result, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

#define DNSERR(x) if(result == DNS_ERR_##x) printf(" DNS_ERR_" #x);
	DNSERR(NONE); DNSERR(FORMAT); DNSERR(SERVERFAILED);
	DNSERR(NOTEXIST); DNSERR(NOTIMPL); DNSERR(REFUSED);
	DNSERR(TRUNCATED); DNSERR(UNKNOWN); DNSERR(TIMEOUT);
	DNSERR(SHUTDOWN); DNSERR(CANCEL);
	printf(" errno=%d \"%s\"\n", errno, strerror(errno));
}

void bufferevent_write_vprintf(struct bufferevent *be, const char *fmt, va_list ap) {
	struct evbuffer *eb = evbuffer_new();
	if(!eb) return;
	evbuffer_add_vprintf(eb, fmt, ap);
	bufferevent_write_buffer(be, eb);
	evbuffer_free(eb);
}

void bufferevent_write_printf(struct bufferevent *be, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	bufferevent_write_vprintf(be, fmt, ap);
	va_end(ap);
}

#ifdef HAVE_PROC
bool proc_get_mem_usage(int64_t *vmrss, int64_t *vmsize) {
	int64_t vsz = -1, rss = -1;
	FILE *f = fopen("/proc/self/status", "r");
	if(f) {
		char buf[256];
		while(fgets(buf, 256, f)) {
			if(1 == sscanf(buf, "VmSize: %lld kB\n", &vsz)) {
				if(rss > -1) {
					if(vmrss) *vmrss = rss;
					if(vmsize) *vmsize = vsz;
					fclose(f);
					return true;
				}
			}
			if(1 == sscanf(buf, "VmRSS: %lld kB\n", &rss)) {
				if(vsz > -1) {
					if(vmrss) *vmrss = rss;
					if(vmsize) *vmsize = vsz;
					fclose(f);
					return true;
				}
			}
		}
	}

	if(vmrss) *vmrss = 0;
	if(vmsize) *vmsize = 0;
	return false;
}

void print_mem_usage(const char *pfx) {
	int64_t vsz, rss;
	proc_get_mem_usage(&vsz, &rss);
	printf("print_mem_usage [%s] %lld %lld\n", pfx, vsz, rss);
}
#endif

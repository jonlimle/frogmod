// generic useful stuff for any C++ program

#ifndef _TOOLS_H
#define _TOOLS_H

#include <enet/enet.h>

#ifdef NULL
#undef NULL
#endif
#define NULL 0

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

#ifdef _MSC_VER
typedef __int64 int64_t;
typedef __uint64 uint64_t;
#else
#include <stdint.h>
#endif

#ifdef _DEBUG
#ifdef __GNUC__
#define ASSERT(c) if(!(c)) { asm("int $3"); }
#else
#define ASSERT(c) if(!(c)) { __asm int 3 }
#endif
#else
#define ASSERT(c) if(c) {}
#endif

#ifdef swap
#undef swap
#endif
template<class T>
static inline void swap(T &a, T &b)
{
    T t = a;
    a = b;
    b = t;
}
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
template<class T>
static inline T max(T a, T b)
{
    return a > b ? a : b;
}
template<class T>
static inline T min(T a, T b)
{
    return a < b ? a : b;
}

#define clamp(a,b,c) (max(b, min(a, c)))
#define rnd(x) ((int)(randomMT()&0xFFFFFF)%(x))
#define rndscale(x) (float((randomMT()&0xFFFFFF)*double(x)/double(0xFFFFFF)))
#define detrnd(s, x) ((int)(((((uint)(s))*1103515245+12345)>>16)%(x)))

#define loop(v,m) for(int v = 0; v<int(m); v++)
#define loopi(m) loop(i,m)
#define loopj(m) loop(j,m)
#define loopk(m) loop(k,m)
#define loopl(m) loop(l,m)


#define DELETEP(p) if(p) { delete   p; p = 0; }
#define DELETEA(p) if(p) { delete[] p; p = 0; }

#define PI  (3.1415927f)
#define PI2 (2*PI)
#define SQRT2 (1.4142136f)
#define SQRT3 (1.7320508f)
#define RAD (PI / 180.0f)

#ifdef WIN32
#ifdef M_PI
#undef M_PI
#endif
#define M_PI 3.14159265

#ifndef __GNUC__
#pragma warning (3: 4189)       // local variable is initialized but not referenced
#pragma warning (disable: 4244) // conversion from 'int' to 'float', possible loss of data
#pragma warning (disable: 4267) // conversion from 'size_t' to 'int', possible loss of data
#pragma warning (disable: 4355) // 'this' : used in base member initializer list
#pragma warning (disable: 4996) // 'strncpy' was declared deprecated
#endif

#define strcasecmp _stricmp
#define PATHDIV '\\'
#else
#define __cdecl
#define _vsnprintf vsnprintf
#define PATHDIV '/'
#endif

// easy safe strings

#define MAXSTRLEN 512
typedef char string[MAXSTRLEN];

inline void vformatstring(char *d, const char *fmt, va_list v, int len = MAXSTRLEN) { _vsnprintf(d, len, fmt, v); d[len-1] = 0; }
inline char *copystring(char *d, const char *s, size_t len = MAXSTRLEN) { strncpy(d, s, len); d[len-1] = 0; return d; }
inline char *concatstring(char *d, const char *s) { size_t len = strlen(d); return copystring(d+len, s, MAXSTRLEN-len); }

struct stringformatter
{
    char *buf;
    stringformatter(char *buf): buf((char *)buf) {}
    void operator()(const char *fmt, ...)
    {
        va_list v;
        va_start(v, fmt);
        vformatstring(buf, fmt, v);
        va_end(v);
    }
};

#define formatstring(d) stringformatter((char *)d)
#define defformatstring(d) string d; formatstring(d)
#define defvformatstring(d,last,fmt) string d; { va_list ap; va_start(ap, last); vformatstring(d, fmt, ap); va_end(ap); }

#define loopv(v)    for(int i = 0; i<(v).length(); i++)
#define loopvj(v)   for(int j = 0; j<(v).length(); j++)
#define loopvk(v)   for(int k = 0; k<(v).length(); k++)
#define loopvrev(v) for(int i = (v).length()-1; i>=0; i--)

template <class T>
struct databuf
{
    enum
    {
        OVERREAD  = 1<<0,
        OVERWROTE = 1<<1
    };

    T *buf;
    int len, maxlen;
    uchar flags;

    databuf() : buf(NULL), len(0), maxlen(0), flags(0) {}

    template<class U> 
    databuf(T *buf, U maxlen) : buf(buf), len(0), maxlen((int)maxlen), flags(0) {}

    const T &get()
    {
        static T overreadval;
        if(len<maxlen) return buf[len++];
        flags |= OVERREAD;
        return overreadval;
    }

    databuf subbuf(int sz)
    {
        sz = min(sz, maxlen-len);
        len += sz;
        return databuf(&buf[len-sz], sz);
    }

    void put(const T &val)
    {
        if(len<maxlen) buf[len++] = val;
        else flags |= OVERWROTE;
    }

    void put(const T *vals, int numvals)
    {
        if(maxlen-len<numvals) flags |= OVERWROTE;
        memcpy(&buf[len], vals, min(maxlen-len, numvals)*sizeof(T));
        len += min(maxlen-len, numvals);
    }

    int get(T *vals, int numvals)
    {
        int read = min(maxlen-len, numvals);
        if(read<numvals) flags |= OVERREAD;
        memcpy(vals, &buf[len], read*sizeof(T));
        len += read;
        return read;
    }

    int length() const { return len; }
    int remaining() const { return maxlen-len; }
    bool overread() const { return flags&OVERREAD; }
    bool overwrote() const { return flags&OVERWROTE; }

    void forceoverread()
    {
        len = maxlen;
        flags |= OVERREAD;
    }
};

typedef databuf<char> charbuf;
typedef databuf<uchar> ucharbuf;

struct packetbuf : ucharbuf
{
    ENetPacket *packet;
    int growth;

    packetbuf(ENetPacket *packet) : ucharbuf(packet->data, packet->dataLength), packet(packet), growth(0) {}
    packetbuf(int growth, int pflags = 0) : growth(growth)
    {
        packet = enet_packet_create(NULL, growth, pflags);
        buf = (uchar *)packet->data;
        maxlen = packet->dataLength;
    }
    ~packetbuf() { cleanup(); }

    void reliable() { packet->flags |= ENET_PACKET_FLAG_RELIABLE; }

    void resize(int n)
    {
        enet_packet_resize(packet, n);
        buf = (uchar *)packet->data;
        maxlen = packet->dataLength;
    }

    void checkspace(int n)
    {
        if(len + n > maxlen && packet && growth > 0) resize(max(len + n, maxlen + growth));
    }

    ucharbuf subbuf(int sz)
    {
        checkspace(sz);
        return ucharbuf::subbuf(sz);
    }

    void put(const uchar &val)
    {
        checkspace(1);
        ucharbuf::put(val);
    }

    void put(const uchar *vals, int numvals)
    {
        checkspace(numvals);
        ucharbuf::put(vals, numvals);
    }
    
    ENetPacket *finalize()
    {
        resize(len);
        return packet;
    }

    void cleanup()
    {
        if(growth > 0 && packet && !packet->referenceCount) { enet_packet_destroy(packet); packet = NULL; buf = NULL; len = maxlen = 0; }
    }
};

template <class T> struct vector
{
    static const int MINSIZE = 8;

    T *buf;
    int alen, ulen;

    vector() : buf(NULL), alen(0), ulen(0)
    {
    }

    vector(const vector &v) : buf(NULL), alen(0), ulen(0)
    {
        *this = v;
    }

    ~vector() { setsize(0); if(buf) delete[] (uchar *)buf; }

    vector<T> &operator=(const vector<T> &v)
    {
        setsize(0);
        if(v.length() > alen) vrealloc(v.length());
        loopv(v) add(v[i]);
        return *this;
    }

    T &add(const T &x)
    {
        if(ulen==alen) vrealloc(ulen+1);
        new (&buf[ulen]) T(x);
        return buf[ulen++];
    }

    T &add()
    {
        if(ulen==alen) vrealloc(ulen+1);
        new (&buf[ulen]) T;
        return buf[ulen++];
    }

    T &dup()
    {
        if(ulen==alen) vrealloc(ulen+1);
        new (&buf[ulen]) T(buf[ulen-1]);
        return buf[ulen++];
    }

    void move(vector<T> &v)
    {
        if(!ulen)
        {
            swap(buf, v.buf);
            swap(ulen, v.ulen);
            swap(alen, v.alen);
        }
        else
        {
            vrealloc(ulen+v.ulen);
            if(v.ulen) memcpy(&buf[ulen], v.buf, v.ulen*sizeof(T));
            ulen += v.ulen;
            v.ulen = 0;
        }
    }

    bool inrange(size_t i) const { return i<size_t(ulen); }
    bool inrange(int i) const { return i>=0 && i<ulen; }

    T &pop() { return buf[--ulen]; }
    T &last() { return buf[ulen-1]; }
    void drop() { buf[--ulen].~T(); }
    bool empty() const { return ulen==0; }

    int capacity() const { return alen; }
    int length() const { return ulen; }
    T &operator[](int i) { ASSERT(i>=0 && i<ulen); return buf[i]; }
    const T &operator[](int i) const { ASSERT(i >= 0 && i<ulen); return buf[i]; }
    
    void setsize(int i)         { ASSERT(i<=ulen); while(ulen>i) drop(); }
    void setsizenodelete(int i) { ASSERT(i<=ulen); ulen = i; }
    
    void deletecontentsp() { while(!empty()) delete   pop(); }
    void deletecontentsa() { while(!empty()) delete[] pop(); }

    T *getbuf() { return buf; }
    const T *getbuf() const { return buf; }
    bool inbuf(const T *e) const { return e >= buf && e < &buf[ulen]; }

    template<class ST>
    void sort(int (__cdecl *cf)(ST *, ST *), int i = 0, int n = -1) 
    { 
        qsort(&buf[i], n<0 ? ulen : n, sizeof(T), (int (__cdecl *)(const void *,const void *))cf); 
    }

    void vrealloc(int sz)
    {
        int olen = alen;
        if(!alen) alen = max(MINSIZE, sz);
        else while(alen < sz) alen *= 2;
        if(alen <= olen) return;
        uchar *newbuf = new uchar[alen*sizeof(T)];
        if(olen > 0)
        {
            memcpy(newbuf, buf, olen*sizeof(T));
            delete[] (uchar *)buf;
        }
        buf = (T *)newbuf;
    }

    databuf<T> reserve(int sz)
    {
        if(ulen+sz > alen) vrealloc(ulen+sz);
        return databuf<T>(&buf[ulen], sz);
    }

    void advance(int sz)
    {
        ulen += sz;
    }

    void addbuf(const databuf<T> &p)
    {
        advance(p.length());
    }

    void put(const T *v, int n)
    {
        databuf<T> buf = reserve(n);
        buf.put(v, n);
        addbuf(buf);
    }

    void remove(int i, int n)
    {
        for(int p = i+n; p<ulen; p++) buf[p-n] = buf[p];
        ulen -= n;
    }

    T remove(int i)
    {
        T e = buf[i];
        for(int p = i+1; p<ulen; p++) buf[p-1] = buf[p];
        ulen--;
        return e;
    }

    T removeunordered(int i)
    {
        T e = buf[i];
        ulen--;
        if(ulen>0) buf[i] = buf[ulen];
        return e;
    }

    template<class U>
    int find(const U &o)
    {
        loopi(ulen) if(buf[i]==o) return i;
        return -1;
    }
    
    void removeobj(const T &o)
    {
        loopi(ulen) if(buf[i]==o) remove(i--);
    }

    void replacewithlast(const T &o)
    {
        if(!ulen) return;
        loopi(ulen-1) if(buf[i]==o)
        {
            buf[i] = buf[ulen-1];
        }
        ulen--;
    }

    T &insert(int i, const T &e)
    {
        add(T());
        for(int p = ulen-1; p>i; p--) buf[p] = buf[p-1];
        buf[i] = e;
        return buf[i];
    }

    T *insert(int i, const T *e, int n)
    {
        if(ulen+n>alen) vrealloc(ulen+n);
        loopj(n) add(T());
        for(int p = ulen-1; p>=i+n; p--) buf[p] = buf[p-n];
        loopj(n) buf[i+j] = e[j];
        return &buf[i];
    }

    void reverse()
    {
        loopi(ulen/2) swap(buf[i], buf[ulen-1-i]);
    }
};

typedef vector<char *> cvector;
typedef vector<int> ivector;
typedef vector<ushort> usvector;

static inline uint hthash(const char *key)
{
    uint h = 5381;
    for(int i = 0, k; (k = key[i]); i++) h = ((h<<5)+h)^k;    // bernstein k=33 xor
    return h;
}

static inline bool htcmp(const char *x, const char *y)
{
    return !strcmp(x, y);
}

static inline uint hthash(int key)
{   
    return key;
}

static inline bool htcmp(int x, int y)
{
    return x==y;
}

template <class K, class T> struct hashtable
{
    typedef K key;
    typedef const K const_key;
    typedef T value;
    typedef const T const_value;

    enum { CHUNKSIZE = 64 };

    struct chain      { T data; K key; chain *next; };
    struct chainchunk { chain chains[CHUNKSIZE]; chainchunk *next; };

    int size;
    int numelems;
    chain **table;
    chain *enumc;

    chainchunk *chunks;
    chain *unused;

    hashtable(int size = 1<<10)
      : size(size)
    {
        numelems = 0;
        chunks = NULL;
        unused = NULL;
        table = new chain *[size];
        loopi(size) table[i] = NULL;
    }

    ~hashtable()
    {
        DELETEA(table);
        deletechunks();
    }

    chain *insert(const K &key, uint h)
    {
        if(!unused)
        {
            chainchunk *chunk = new chainchunk;
            chunk->next = chunks;
            chunks = chunk;
            loopi(CHUNKSIZE-1) chunk->chains[i].next = &chunk->chains[i+1];
            chunk->chains[CHUNKSIZE-1].next = unused;
            unused = chunk->chains;
        }
        chain *c = unused;
        unused = unused->next;
        c->key = key;
        c->next = table[h]; 
        table[h] = c;
        numelems++;
        return c;
    }

    #define HTFIND(success, fail) \
        uint h = hthash(key)&(size-1); \
        for(chain *c = table[h]; c; c = c->next) \
        { \
            if(htcmp(key, c->key)) return (success); \
        } \
        return (fail);

    T *access(const K &key)
    {
        HTFIND(&c->data, NULL);
    }

    T &access(const K &key, const T &data)
    {
        HTFIND(c->data, insert(key, h)->data = data);
    }

    T &operator[](const K &key)
    {
        HTFIND(c->data, insert(key, h)->data);
    }

    #undef HTFIND
   
    bool remove(const K &key)
    {
        uint h = hthash(key)&(size-1); 
        for(chain **p = &table[h], *c = table[h]; c; p = &c->next, c = c->next)
        {
            if(htcmp(key, c->key))
            {
                *p = c->next;
                c->data.~T();
                c->key.~K();
                new (&c->data) T;
                new (&c->key) K;
                c->next = unused;
                unused = c;
                numelems--;
                return true;
            }
        }
        return false;
    }

    void deletechunks()
    {
        for(chainchunk *nextchunk; chunks; chunks = nextchunk)
        {
            nextchunk = chunks->next;
            delete chunks;
        }
    }

    void clear()
    {
        if(!numelems) return;
        loopi(size) table[i] = NULL;
        numelems = 0;
        unused = NULL;
        deletechunks();
    }
};

#define enumeratekt(ht,k,e,t,f,b) loopi((ht).size)  for(hashtable<k,t>::chain *enumc = (ht).table[i]; enumc;) { hashtable<k,t>::const_key &e = enumc->key; t &f = enumc->data; enumc = enumc->next; b; }
#define enumerate(ht,t,e,b)       loopi((ht).size) for((ht).enumc = (ht).table[i]; (ht).enumc;) { t &e = (ht).enumc->data;  (ht).enumc = (ht).enumc->next; b; }

struct unionfind
{
    struct ufval
    {
        int rank, next;

        ufval() : rank(0), next(-1) {}
    };
    
    vector<ufval> ufvals;

    int find(int k)
    {
        if(k>=ufvals.length()) return k;
        while(ufvals[k].next>=0) k = ufvals[k].next;
        return k;
    }
    
    int compressfind(int k)
    {
        if(ufvals[k].next<0) return k;
        return ufvals[k].next = compressfind(ufvals[k].next);
    }
    
    void unite (int x, int y)
    {
        while(ufvals.length() <= max(x, y)) ufvals.add();
        x = compressfind(x);
        y = compressfind(y);
        if(x==y) return;
        ufval &xval = ufvals[x], &yval = ufvals[y];
        if(xval.rank < yval.rank) xval.next = y;
        else
        {
            yval.next = x;
            if(xval.rank==yval.rank) yval.rank++;
        }
    }
};

template <class T, int SIZE> struct ringbuf
{
    int index, len;
    T data[SIZE];

    ringbuf() { clear(); }

    void clear()
    {
        index = len = 0;
    }

    bool empty() const { return !len; }

    const int length() const { return len; }

    T &add(const T &e)
    {
        T &t = (data[index] = e);
        index++;
        if(index >= SIZE) index -= SIZE;
        if(len < SIZE) len++;
        return t;
    }

    T &add() { return add(T()); }

    T &operator[](int i)
    {
        i += index - len;
        return data[i < 0 ? i + SIZE : i%SIZE];
    }

    const T &operator[](int i) const
    {
        i += index - len;
        return data[i < 0 ? i + SIZE : i%SIZE];
    }
};

template <class T, int SIZE> struct queue
{
    int head, tail, len;
    T data[SIZE];
    
    queue() { clear(); }
    
    void clear() { head = tail = len = 0; }

    int length() const { return len; }
    bool empty() const { return !len; }
    bool full() const { return len == SIZE; }

    T &added() { return data[tail > 0 ? tail-1 : SIZE-1]; }
    T &added(int offset) { return data[tail-offset > 0 ? tail-offset-1 : tail-offset-1 + SIZE]; }
    T &adding() { return data[tail]; }
    T &adding(int offset) { return data[tail+offset >= SIZE ? tail+offset - SIZE : tail+offset]; }
    T &add()
    {
        ASSERT(len < SIZE);    
        T &t = data[tail];
        tail = (tail + 1)%SIZE;
        len++;
        return t;
    }

    T &removing() { return data[head]; }
    T &removing(int offset) { return data[head+offset >= SIZE ? head+offset - SIZE : head+offset]; }
    T &remove()
    {
        ASSERT(len > 0);
        T &t = data[head];
        head = (head + 1)%SIZE;
        len--;
        return t;
    }
};

#define mkstring(d) string d; d[0] = 0;
inline char *newstring(size_t l)                { return new char[l+1]; }
inline char *newstring(const char *s, size_t l) { return copystring(newstring(l), s, l+1); }
inline char *newstring(const char *s)           { return newstring(s, strlen(s));          }
inline char *newstringbuf(const char *s)        { return newstring(s, MAXSTRLEN-1);       }

#if defined(WIN32) && !defined(__GNUC__)
#ifdef _DEBUG
//#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
inline void *__cdecl operator new(size_t n, const char *fn, int l) { return ::operator new(n, 1, fn, l); }
inline void __cdecl operator delete(void *p, const char *fn, int l) { ::operator delete(p, 1, fn, l); }
#define new new(__FILE__,__LINE__)
#endif 
#endif

const int islittleendian = 1;
#ifdef SDL_BYTEORDER
#define endianswap16 SDL_Swap16
#define endianswap32 SDL_Swap32
#else
inline ushort endianswap16(ushort n) { return (n<<8) | (n>>8); }
inline uint endianswap32(uint n) { return (n<<24) | (n>>24) | ((n>>8)&0xFF00) | ((n<<8)&0xFF0000); }
#endif
template<class T> inline T endianswap(T n) { union { T t; uint i; } conv; conv.t = n; conv.i = endianswap32(conv.i); return conv.t; }
template<> inline ushort endianswap<ushort>(ushort n) { return endianswap16(n); }
template<> inline short endianswap<short>(short n) { return endianswap16(n); }
template<> inline uint endianswap<uint>(uint n) { return endianswap32(n); }
template<> inline int endianswap<int>(int n) { return endianswap32(n); }
template<class T> inline void endianswap(T *buf, int len) { for(T *end = &buf[len]; buf < end; buf++) *buf = endianswap(*buf); }
template<> inline void endianswap(float *buf, int len) { uint *src = (uint *)buf; for(uint *end = &src[len]; src < end; src++) *src = endianswap(*src); }
template<class T> inline T endiansame(T n) { return n; }
template<class T> inline void endiansame(T *buf, int len) {}
#ifdef SDL_BYTEORDER
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define lilswap endiansame
#define bigswap endianswap
#else
#define lilswap endianswap
#define bigswap endiansame
#endif
#else
template<class T> inline T lilswap(T n) { return *(const uchar *)&islittleendian ? n : endianswap(n); }
template<class T> inline void lilswap(T *buf, int len) { if(!*(const uchar *)&islittleendian) endianswap(buf, len); }
template<class T> inline T bigswap(T n) { return *(const uchar *)&islittleendian ? endianswap(n) : n; }
template<class T> inline void bigswap(T *buf, int len) { if(*(const uchar *)&islittleendian) endianswap(buf, len); }
#endif

extern void seedMT(uint seed);
extern uint randomMT(void);

/*
 * vampi
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <event2/event.h>
#include <event2/dns.h>
#include <event2/http.h>
#include <event2/buffer.h>

extern void reset_ticks(void);
extern int64_t get_ticks(void); // gets time in miliseconds

// Debug a function call (print it out and execute it)
// #define DEBUGF(a) { printf("%s: %d: %s\n", __FILE__, __LINE__, #a); a; }
#define DEBUGF(a) { a; }

/* Use this to wrap long lines of text */
struct Wrapper {
	int maxlen;
	struct WrapLine { string s; };
	vector <WrapLine> lines;
	const char *sep;

	Wrapper(int m = 256) { init(m); }
	Wrapper(const char *separator) { init(256, separator); }
	~Wrapper() {};

	void init(int ml = 256, const char *separator = " ") {
		maxlen = ml;
		if(maxlen > MAXSTRLEN - 1) maxlen = MAXSTRLEN - 1;
		sep = separator;
	}

	void append(const char *fmt, ...);
};

int ipint(char *str);
char *ipstr(unsigned int ip);
char *timestr(int64_t time);
char *trim(char *str);
int strcmpf(char *s1, const char *s2fmt, ...);
void getsha1(char *pwd, char *hash, int maxlen=MAXSTRLEN);
#ifndef _GNU_SOURCE
char *strndup(const char *s, size_t n);
#endif
struct URL {
	char *full;
	char *proto;
	char *username, *password;
	char *hostname;
	char *portstr; int port; // portstr is useful for getaddrinfo()
	char *path;

	URL() { nullify(); }
	URL(char *str) { nullify(); parse(str); }
	~URL() { clear(); }

	void nullify() {
		full = NULL;
		proto = NULL;
		username = NULL;
		password = NULL;
		hostname = NULL;
		portstr = NULL; port = 0;
		path = NULL;
	}

	void clear() {
		if(full) { free(full); full = NULL; }
		if(proto) { free(proto); proto = NULL; }
		if(username) { free(username); username = NULL; }
		if(password) { free(password); password = NULL; }
		if(hostname) { free(hostname); hostname = NULL; }
		if(portstr) { free(portstr); portstr = NULL; }
		if(path) { free(path); path = NULL; }
	}

	void dump() {
		printf("URL::dump();\n");
#define DUMP(x) if(x) printf(#x "=\"%s\"\n", x)
		DUMP(full); DUMP(proto); DUMP(username); DUMP(password); DUMP(hostname); DUMP(portstr); DUMP(path);
#undef DUMP
	}

	bool parse(char *str) {
		clear(); nullify();

		if(!str || !*str) return false;

		char *c = str;

		char *p = strstr(str, "://");
		if(p && p >= str) {
			full = strdup(str); // we have the protocol, we can just copy the full url
			int l = (int)(p - str);
			proto = strndup(str, l);
			c = p+3;
		} else {
			proto = (char *)malloc(5); strcpy(proto, "http");
			// we don't have the protocol, so prepend it
			full = (char *)malloc(8 + strlen(str));
			strcpy(full, "http://");
			strcpy(full + 7, str); // faster than strcat :D
		}

		p = index(c, '@');
		if(p) {
			char *q = index(c, ':');
			if(q && q < p) {
				username = strndup(c, (int)(q - c));
				password = strndup(q + 1, (int)(p - q) - 1);
			} else {
				username = strndup(c, (int)(p - c));
				password = NULL;
			}
			c = p + 1;
		}
		p = index(c, '/');
		char *q = index(c, ':');
		if(q) {
			hostname = strndup(c, (int)(q - c));
			if(p) {
				portstr = strndup(q + 1, (int)(p - q) - 1);
			} else {
				portstr = strdup(q + 1);
			}
			port = strtol(portstr, NULL, 10);
		} else {
			if(p) hostname = strndup(c, (int)(p - c));
			else hostname = strdup(c);
			portstr = strdup("80");
			port = 80;
		}
		if(p) path = strdup(p);
		else path = strdup("/");

		return true;
	}

};

struct HttpQuery {
	event_base *base;
	evdns_base *dnsbase;
	URL url;
	void (*cb)(evhttp_request *req, void *arg);
	char *filename;
	void *arg;
};

void froghttp_get(event_base *base, evdns_base *dnsbase, char *url, void(*cb)(evhttp_request *, void *), void *arg);
void bufferevent_print_error(short what, const char *fmt, ...);
void evdns_print_error(int result, const char *fmt, ...);
void bufferevent_write_printf(struct bufferevent *be, const char *fmt, ...);
void bufferevent_write_vprintf(struct bufferevent *be, const char *fmt, va_list ap);
char *evbuffer_readln_nul(struct evbuffer *buffer, size_t *n_read_out, enum evbuffer_eol_style eol_style);

# ifdef HAVE_PROC
bool proc_get_mem_usage(int64_t *vmrss, int64_t *vmsize);
void print_mem_usage(const char *pfx = "");
# endif

#endif

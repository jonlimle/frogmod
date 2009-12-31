// the interface the game uses to access the engine

extern int64_t curtime;                     // current frame time
extern int64_t lastmillis;                  // last time
extern int64_t totalmillis;                 // total elapsed time

// octaedit
struct selinfo
{
    int corner;
    int cx, cxs, cy, cys;
    ivec o, s;
    int grid, orient;
    int size() const    { return s.x*s.y*s.z; }
    int us(int d) const { return s[d]*grid; }
    bool operator==(const selinfo &sel) const { return o==sel.o && s==sel.s && grid==sel.grid && orient==sel.orient; }
};

// console
enum
{
    CON_INFO  = 1<<0,
    CON_WARN  = 1<<1,
    CON_ERROR = 1<<2,
    CON_DEBUG = 1<<3,
    CON_INIT  = 1<<4,
    CON_ECHO  = 1<<5
};

extern void conoutf(const char *s, ...);
extern void conoutf(int type, const char *s, ...);

// main
extern void fatal(const char *s, ...);

// renderparticles
enum
{
    PART_BLOOD = 0,
    PART_WATER,
    PART_SMOKE,
    PART_STEAM,
    PART_FLAME,
    PART_FIREBALL1, PART_FIREBALL2, PART_FIREBALL3,
    PART_STREAK, PART_LIGHTNING,
    PART_EXPLOSION, PART_EXPLOSION_NO_GLARE,
    PART_SPARK, PART_EDIT,
    PART_MUZZLE_FLASH1, PART_MUZZLE_FLASH2, PART_MUZZLE_FLASH3,
    PART_TEXT,
    PART_METER, PART_METER_VS,
    PART_LENS_FLARE
};

#include "server.h"

#include "crypto.h"

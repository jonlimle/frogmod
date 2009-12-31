#define MAXBOTS 32

enum { AI_NONE = 0, AI_BOT, AI_MAX };
#define isaitype(a) (a >= 0 && a <= AI_MAX-1)


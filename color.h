#ifndef COLOR_H_
#define COLOR_H_

extern char irc2sauer[];
extern char sauer2irc[];
extern char sauer2console[];


void color_irc2sauer(char *src, char *dst);
void color_sauer2irc(char *src, char *dst);
void color_sauer2console(char *src, char *dst);

#endif /* COLOR_H_ */

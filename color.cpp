#include <stdio.h>
#include <string.h>
#include "cube.h"
// thanks to Catelite for making this list
char irc2sauer[] = {
	'7', // 0 = white/default
	'4', // 1 = black/default
	'1', // 2
	'0', // 3
	'3', // 4
	'3', // 5
	'5', // 6
	'6', // 7
	'2', // 8
	'0', // 9
	'1', // 10
	'1', // 11
	'1', // 12
	'5', // 13
	'7', // 14
	'4'  // 15
};

void color_irc2sauer(char *src, char *dst) {
	char *c = src;
	char *d = dst;
	//FIXME: use FSM logic instead
	while(*c) {
		if(*c == 3) {
			c++;
			int color = 0;
			for(int i = 0; i < 2; i++)
				if(*(c) >= '0' && *(c) <= '9') { color *= 10; color += *c - '0'; c++; }
			if(*(c) == ',') { // strip background color
				c++;
				for(int i = 0; i < 2; i++) if(*(c) >= '0' && *(c) <= '9') c++;
			}
			*d++ = '\f';
			if(color < 16) *d++ = irc2sauer[color];
		} else if (*c == 2 || *c == 0x1F || *c == 0x16) c++; // skip bold, underline and italic
		else if(*c == 0x0f) { *d++ = '\f'; *d++ = '7'; }
		*d++ = *c++;
		*d = 0;
	}
}

char sauer2irc[] = {
	3,
	2,
	8,
	4,
	15,
	6,
	7,
	14
};
void color_sauer2irc(char *src, char *dst) {
	char *c = src, *d = dst;
	while(*c) {
		if(*c == '\f') {
			c++;
			int col = *c++ - '0';
			if(col < 0) col = 0;
			if(col > 7) col = 7;
			col = sauer2irc[col];
			if(col == 7) *d++ = 15;
			else {
				*d++ = 3;
				*d++ = '0' + col / 10;
				*d++ = '0' + col % 10;
			}
		} else *d++ = *c++;
		*d = 0;
	}
}

char sauer2console[] = {
	2, // 0 green
	4, // 1 blue
	3, // 2 yellow
	1, // 3 red
	0, // 4 gray
	5, // 5 magenta
	6, // 6 orange -> cyan (no better replacement)
	7, // 7 white
};

void color_sauer2console(char *src, char *dst) {
	copystring(dst, "\033[1;37m");
	for(char *c = src; *c; c++) {
		if(*c == '\f') {
			c++;
			if(*c >= '0' && *c <= '7') sprintf(dst + strlen(dst), "\033[1;%02dm", 30 + sauer2console[*c - '0']);
		} else sprintf(dst + strlen(dst), "%c", *c);
	}
	strcat(dst, "\033[0m");
}

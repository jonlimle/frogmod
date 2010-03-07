#include <event2/http.h>
#include <event2/buffer.h>
#include "game.h"
#include "json.h"
#include "color.h"

namespace server
{
	struct server_entity            // server side version of "entity" type
	{
		int type;
		int64_t spawntime;
		char spawned;
	};

	static const int64_t DEATHMILLIS = 300;

	struct clientinfo;

	struct gameevent
	{
		virtual ~gameevent() {}

		virtual bool flush(clientinfo *ci, int64_t fmillis);
		virtual void process(clientinfo *ci) {}

		virtual bool keepable() const { return false; }
	};

	struct timedevent : gameevent
	{
		int64_t millis;

		bool flush(clientinfo *ci, int64_t fmillis);
	};

	struct hitinfo
	{
		int target;
		int lifesequence;
		union
		{
			int rays;
			float dist;
		};
		vec dir;
	};

	struct shotevent : timedevent
	{
		int id, gun;
		vec from, to;
		vector<hitinfo> hits;

		void process(clientinfo *ci);
	};

	struct explodeevent : timedevent
	{
		int id, gun;
		vector<hitinfo> hits;

		bool keepable() const { return true; }

		void process(clientinfo *ci);
	};

	struct suicideevent : gameevent
	{
		void process(clientinfo *ci);
	};

	struct pickupevent : gameevent
	{
		int ent;

		void process(clientinfo *ci);
	};

	template <int N>
	struct projectilestate
	{
		int projs[N];
		int numprojs;

		projectilestate() : numprojs(0) {}

		void reset() { numprojs = 0; }

		void add(int val)
		{
			if(numprojs>=N) numprojs = 0;
			projs[numprojs++] = val;
		}

		bool remove(int val)
		{
			loopi(numprojs) if(projs[i]==val)
			{
				projs[i] = projs[--numprojs];
				return true;
			}
			return false;
		}
	};

	struct gamestate : fpsstate
	{
		vec o;
		int state, editstate;
		int64_t lastdeath, lastspawn;
		int lifesequence;
		int64_t lastshot;
		projectilestate<8> rockets, grenades;
		int frags, flags, deaths, teamkills, shotdamage, damage;
		int lasttimeplayed, timeplayed;
		float effectiveness;

		// vampi
		int64_t lastfragmillis;
		int multifrags;
		int spreefrags;

		gamestate() : state(CS_DEAD), editstate(CS_DEAD) {}

		bool isalive(int64_t gamemillis)
		{
			return state==CS_ALIVE || (state==CS_DEAD && gamemillis - lastdeath <= DEATHMILLIS);
		}

		bool waitexpired(int64_t gamemillis)
		{
			return gamemillis - lastshot >= gunwait;
		}

		void reset()
		{
			if(state!=CS_SPECTATOR) state = editstate = CS_DEAD;
			maxhealth = 100;
			rockets.reset();
			grenades.reset();

			timeplayed = 0;
			effectiveness = 0;
			frags = flags = deaths = teamkills = shotdamage = damage = 0;

			lastfragmillis = 0;
			multifrags = spreefrags = 0;

			respawn();
		}

		void respawn()
		{
			fpsstate::respawn();
			o = vec(-1e10f, -1e10f, -1e10f);
			lastdeath = 0;
			lastspawn = -1;
			lastshot = 0;
		}

		void reassign()
		{
			respawn();
			rockets.reset();
			grenades.reset();
		}

		const char *statename() {
			switch(state) {
				case CS_ALIVE:
					return "alive";
				case CS_DEAD:
					return "dead";
				case CS_SPAWNING:
					return "spawning";
				case CS_LAGGED:
					return "lagged";
				case CS_EDITING:
					return "editing";
				case CS_SPECTATOR:
					return "spectator";
			}

			return "unknown";
		}
	};

	struct savedscore
	{
		uint ip;
		string name;
		int maxhealth, frags, flags, deaths, teamkills, shotdamage, damage;
		int timeplayed;
		float effectiveness;

		void save(gamestate &gs)
		{
			maxhealth = gs.maxhealth;
			frags = gs.frags;
			flags = gs.flags;
			deaths = gs.deaths;
			teamkills = gs.teamkills;
			shotdamage = gs.shotdamage;
			damage = gs.damage;
			timeplayed = gs.timeplayed;
			effectiveness = gs.effectiveness;
		}

		void restore(gamestate &gs)
		{
			if(gs.health==gs.maxhealth) gs.health = maxhealth;
			gs.maxhealth = maxhealth;
			gs.frags = frags;
			gs.flags = flags;
			gs.deaths = deaths;
			gs.teamkills = teamkills;
			gs.shotdamage = shotdamage;
			gs.damage = damage;
			gs.timeplayed = timeplayed;
			gs.effectiveness = effectiveness;
		}
	};

	struct clientinfo
	{
		int clientnum, ownernum, sessionid;
		int64_t connectmillis;
		string name, team, mapvote;
		int playermodel;
		int modevote;
		int privilege;
		bool connected, local, timesync;
		int64_t gameoffset, lastevent;
		gamestate state;
		vector<gameevent *> events;
		vector<uchar> position, messages;
		int posoff, poslen, msgoff, msglen;
		vector<clientinfo *> bots;
		uint authreq;
		string authname;
		int ping, aireinit;
		string clientmap;
		int mapcrc;
		bool warned, gameclip;

		//vampi
		int64_t lasttext; // spam protection
		int spamlines; // number of lines that you can type in the spam interval without getting blocked. useful for people who talk fast :P
		bool spamwarned; // only warn once within the interval
		int64_t lastremip, lastnewmap; // protect against remip and newmap attacks

		// protect against large selections
		int64_t lastbigselspam;
		bool bigselwarned;
		// protect against fast scrolling
		int64_t lastscrollspam;
		ivec editspamsize[2]; // it's edit spam if at least one dimension of this box is too big (this box gets expanded when fast scrolling)
		bool scrollspamwarned;
		// protect against fast Y+scroll
		int64_t lasttexturespam;
		int texturespamtimes;
		bool texturespamwarned;
		
		//protect against fast mapmodel change
		int lastmapmodeltype;
		int64_t lastmapmodelchange;
		int mapmodelspamtimes;
		bool mapmodelspamwarned;

		// edit recording
		ivec playorigin;
		stream *recording, *playing;

		// protect against mass kicking
		int64_t lastkick;

		bool logged_in;
		string permissions;

		clientinfo() { reset(); }
		~clientinfo() { events.deletecontentsp(); }

		void addevent(gameevent *e) {
			if(state.state==CS_SPECTATOR || events.length()>100) delete e;
			else events.add(e);
		}

		void mapchange() {
			mapvote[0] = 0;
			state.reset();
			events.deletecontentsp();
			timesync = false;
			lastevent = 0;
			clientmap[0] = '\0';
			mapcrc = 0;
			warned = false;
			gameclip = false;
		}

		void reassign() {
			state.reassign();
			events.deletecontentsp();
			timesync = false;
			lastevent = 0;
		}

		void reset() {
			name[0] = team[0] = 0;
			playermodel = -1;
			privilege = PRIV_NONE;
			connected = local = false;
			authreq = 0;
			position.setsizenodelete(0);
			messages.setsizenodelete(0);
			ping = 0;
			aireinit = 0;
			mapchange();

			recording = playing = NULL;
			playorigin.x = playorigin.y = playorigin.z = 512; //middle of a size 10 map, I guess

			lastremip = lastnewmap = 0;
			lasttext = spamlines = 0;
			lastbigselspam = 0;
			bigselwarned = false;
			lastscrollspam = 0;
			editspamsize[0].x = editspamsize[0].y = editspamsize[0].z = 0;
			scrollspamwarned = false;
			lasttexturespam = 0;
			texturespamtimes = 0;
			texturespamwarned = 0;
			lastmapmodeltype = 0;
			lastmapmodelchange = 0;
			mapmodelspamwarned = 0;
			mapmodelspamtimes = 0;
			lastkick = 0;
			permissions[0] = 0;
		}

		bool can_script() {
			return privilege >= PRIV_ADMIN || strchr(permissions, 'a') != NULL || strchr(permissions, 's') != NULL;
		}

		int64_t geteventmillis(int64_t servmillis, int64_t clientmillis) {
			if(!timesync || (events.empty() && state.waitexpired(servmillis)))
			{
				timesync = true;
				gameoffset = servmillis - clientmillis;
				return servmillis;
			}
			else return gameoffset + clientmillis;
		}
	};

	struct worldstate
	{
		int uses;
		vector<uchar> positions, messages;
	};

	struct ban {
		int64_t expiry;
		string match;
		string name;
		event tev;
	};


	namespace aiman
	{
		extern void removeai(clientinfo *ci);
		extern void clearai();
		extern void checkai();
		extern void reqadd(clientinfo *ci, int skill);
		extern void reqdel(clientinfo *ci);
		extern void setbotlimit(clientinfo *ci, int limit);
		extern void setbotbalance(clientinfo *ci, bool balance);
		extern void changemap();
		extern void addclient(clientinfo *ci);
		extern void changeteam(clientinfo *ci);

		extern bool addbotname(char *name);
		extern bool delbotname(char *name);
		extern void listbotnames();
		extern void savebotnames(stream *s);
	}

	/********************************
	 * NOTICES (blacklist/whitelist)
	 ********************************/
	struct notice {
		string match;
		string reason;
	};
	
	vector <notice> blacklisted, whitelisted;

	ICOMMAND(blacklist, "ss", (char *match, char *reason), {
		if(!match || !*match) return;
		notice n;
		copystring(n.match, match);
		copystring(n.reason, reason);
		blacklisted.add(n);
		message("\f3%s\f6 has been blacklisted.", match);
		irc.speak("\00314%s has been blacklisted.", match);
	});
	ICOMMAND(unblacklist, "s", (char *match), {
		if(match && *match)
			loopv(blacklisted)
				if(!strcmp(blacklisted[i].match, match)) {
					blacklisted.remove(i);
					message("\f3%s\f6 has been unblacklisted.", match);
					irc.speak("\00314%s has been unblacklisted.", match);
					return;
				}
	});

	ICOMMAND(whitelist, "ss", (char *match, char *reason), {
		if(!match || !*match) return;
		notice n;
		copystring(n.match, match);
		copystring(n.reason, reason);
		whitelisted.add(n);
		message("\f3%s\f6 has been whitelisted.", match);
		irc.speak("\00314%s has been whitelisted.", match);
	});
	ICOMMAND(unwhitelist, "s", (char *match), {
		if(match && *match)
			loopv(whitelisted)
				if(!strcmp(whitelisted[i].match, match)) {
					whitelisted.remove(i);
					message("\f3%s\f6 has been unwhitelisted.", match);
					irc.speak("\00314%s has been unwhitelisted.", match);
					return;
				}
	});

	int show_blacklist(int who) {
		loopv(blacklisted) {
			whisper(who, "\f3%s\f7 - %s", blacklisted[i].match, blacklisted[i].reason);
		}
		return blacklisted.length();
	}

	int show_whitelist(int who) {
		loopv(whitelisted) {
			whisper(who, "\f3%s\f7 - %s", whitelisted[i].match, whitelisted[i].reason);
		}
		return whitelisted.length();
	}

	bool is_blacklisted(int cn) {
		clientinfo *ci = (clientinfo *)getclientinfo(cn);
		loopv(blacklisted)
			if(!fnmatch(blacklisted[i].match, getclientipstr(cn), 0) ||
			   !fnmatch(blacklisted[i].match, getclienthostname(cn), 0) ||
			   !fnmatch(blacklisted[i].match, ci->name, 0)) return true;
		return false;
	}

	const char *blacklist_reason(int cn) {
		loopv(blacklisted) if(!fnmatch(blacklisted[i].match, getclientipstr(cn), 0) || !fnmatch(blacklisted[i].match, getclienthostname(cn), 0)) return blacklisted[i].reason;
		return "";
	}

	bool is_whitelisted(int cn) {
		loopv(whitelisted) if(!fnmatch(whitelisted[i].match, getclientipstr(cn), 0) || !fnmatch(whitelisted[i].match, getclienthostname(cn), 0)) return true;
		return false;
	}

	#define MM_MODE 0xF
	#define MM_AUTOAPPROVE 0x1000
	#define MM_PRIVSERV (MM_MODE | MM_AUTOAPPROVE)
	#define MM_PUBSERV ((1<<MM_OPEN) | (1<<MM_VETO))
	#define MM_COOPSERV (MM_AUTOAPPROVE | MM_PUBSERV | (1<<MM_LOCKED))

	bool notgotitems = true;        // true when map has changed and waiting for clients to send item
	int gamemode = 0;
	int64_t gamemillis = 0, gamelimit = 0;
	bool gamepaused = false;

	string smapname = "";
	int64_t interm = 0, minremain = 0;
	bool mapreload = false;
	int64_t lastsend = 0;
	int mastermode = MM_OPEN, mastermask = MM_PRIVSERV;
	int currentmaster = -1;
	ICOMMAND(getcurrentmaster, "", (), { defformatstring(s)("%d", currentmaster); result(s); } );
	bool masterupdate = false;
	stream *mapdata = NULL;

	vector<uint> allowedips;
	vector<ban> bans;
	vector<clientinfo *> connects, clients, bots;
	vector<worldstate *> worldstates;
	bool reliablemessages = false;

	struct demofile // a demofile likes demos, just like a pedofile likes children
	{
		string info;
		uchar *data;
		int len;
	};

	#define MAXDEMOS 5
	vector<demofile> demos;

	bool demonextmatch = false;
	stream *demotmp = NULL, *demorecord = NULL, *demoplayback = NULL;
	int64_t nextplayback = 0, demomillis = 0;

	struct servmode
	{
		virtual ~servmode() {}

		virtual void entergame(clientinfo *ci) {}
		virtual void leavegame(clientinfo *ci, bool disconnecting = false) {}

		virtual void moved(clientinfo *ci, const vec &oldpos, bool oldclip, const vec &newpos, bool newclip) {}
		virtual bool canspawn(clientinfo *ci, bool connecting = false) { return true; }
		virtual void spawned(clientinfo *ci) {}
		virtual int fragvalue(clientinfo *victim, clientinfo *actor)
		{
			if(victim==actor || isteam(victim->team, actor->team)) return -1;
			return 1;
		}
		virtual void died(clientinfo *victim, clientinfo *actor) {}
		virtual bool canchangeteam(clientinfo *ci, const char *oldteam, const char *newteam) { return true; }
		virtual void changeteam(clientinfo *ci, const char *oldteam, const char *newteam) {}
		virtual void initclient(clientinfo *ci, packetbuf &p, bool connecting) {}
		virtual void update() {}
		virtual void reset(bool empty) {}
		virtual void intermission() {}
		virtual bool hidefrags() { return false; }
		virtual int getteamscore(const char *team) { return 0; }
		virtual void getteamscores(vector<teamscore> &scores) {}
		virtual bool extinfoteam(const char *team, ucharbuf &p) { return false; }
	};

	#define SERVMODE 1
	#include "capture.h"
	#include "ctf.h"

	captureservmode capturemode;
	ctfservmode ctfmode;
	servmode *smode = NULL;

	SVAR(serverdesc, "");
	SVAR(serverpass, "");
	SVAR(adminpass, "");
	VARF(publicserver, 0, 0, 2, {
		switch(publicserver)
		{
			case 0: default: mastermask = MM_PRIVSERV; break;
			case 1: mastermask = MM_PUBSERV; break;
			case 2: mastermask = MM_COOPSERV; break;
		}
	});
	SVAR(servermotd, "");

	//vampi
	SVAR(frogchar, "@");
	SVAR(logfile, "");

	VAR(remipmillis, 1, 2000, INT_MAX);
	VAR(newmapmillis, 1, 2000, INT_MAX);
	VAR(spammillis, 1, 1000, INT_MAX); // interval for spam detection
	VAR(maxspam, 2, 3, INT_MAX); // number of lines that you can type in spammillis interval without getting blocked
	VAR(bantime, 1, 60, INT_MAX); // maximum ban time. after this number of minutes, the banned client can reconnect.

	VAR(multifragmillis, 1, 2000, INT_MAX); // MULTI KILL!!

	VAR(playbackmillis, 10, 100, INT_MAX); // play back a recorded edit action at this rate
	int64_t lastplaybackmillis;

	bool chainsaw = false, gunfinity = false;
	bool firstblood = false;

	VAR(editspamwarn, 0, 1, 2); // spam warnings: 0=disabled, 1=master/admin only, 2=global

	VAR(maxselspam, 1, 128, INT_MAX); // if the size of the selection when editing is bigger than this, edit spam was detected. This is in world units, not cubes.
	VAR(bigselmillis, 1, 1000, INT_MAX); // interval for big selection warnings
	VAR(maxscrollspam, 1, 128, INT_MAX); // length of the built object, on any axis. if bigger than this (world unit), edit spam was detected.
	VAR(editscrollmillis, 1, 1500, INT_MAX); // interval for fast scrolling
	VAR(texturespammillis, 1, 500, INT_MAX); // interval for fast texture scrolling
	VAR(maxtexturespam, 2, 8, INT_MAX); // maximum texture changes in the interval
	VAR(mapmodelspammillis, 1, 500, INT_MAX); // interval for mapmodel change
	VAR(maxmapmodelspam, 2, 3, INT_MAX); // maximum mapmodel changes before warning

	VAR(kickmillis, 1, 30000, INT_MAX); // interval between kicks for protecting against mass kicking

	SVAR(webhook, ""); // web hook. this url is accessed to send information to the central server
	SVAR(loginsurl, ""); // if this URL is set, you can update logins.cfg from this url, using @getlogins
	VAR(httpport, 0, 0, 65535); // the port on which to open the http server. set to 0 to disable
	
	SVAR(spreesuicidemsg, "was looking good until he killed himself");
	SVAR(spreeendmsg, "'s killing spree was ended by ");
	VAR(minspreefrags, 2, 5, INT_MAX); // if the player had at least this many frags, the spree end message is announced

	int vmessage(int cn, const char *fmt, va_list ap) {
		if(cn >= 0 && !allowbroadcast(cn)) return 0;

		char buf[1024]; //bigger than 'string'

		int r = vsnprintf(buf, 1024, fmt, ap);

		sendf(cn, 1, "ris", SV_SERVMSG, buf);
		
		return r;
	}

	void whisper(int cn, const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		vmessage(cn, fmt, ap);
		va_end(ap);
	}

	void message(const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		vmessage(-1, fmt, ap);
		va_end(ap);
	}

	//FIXME: clean these up a bit more
	void privilegemsg(int min_privilege, const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		loopv(clients) if(clients[i]->privilege >= min_privilege) vmessage(clients[i]->clientnum, fmt, ap);
		va_end(ap);
	}

	void mastermessage(const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		loopv(clients) if(clients[i]->privilege) vmessage(clients[i]->clientnum, fmt, ap);
		va_end(ap);
	}

	void adminmessage(const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		loopv(clients) if(clients[i]->privilege >= PRIV_ADMIN) vmessage(clients[i]->clientnum, fmt, ap);
		va_end(ap);
	}

	void log(const char *fmt, ...) {
		stream *f = NULL;
		if(logfile[0]) {
			f = openfile(path(logfile, true), "a"); //FIXME: add openfile(FILE *f); so we can open stdout, and simplify this
		}

		va_list ap;

		va_start(ap, fmt);
		time_t t = time(NULL);
		char *ct = ctime(&t);
		ct[strlen(ct) - 1] = ' '; //replace trailing newline with space
		if(f) {
			f->putstring(ct);
			f->vprintf(fmt, ap);
			f->putstring("\n");
		} else {
			fputs(ct, stdout);
			vfprintf(stdout, fmt, ap);
			fputs("\n", stdout);
		}
		va_end(ap);

		if(f) delete f;
	}

	const char *privname(int type)
	{
		switch(type)
		{
			case PRIV_ADMIN: return "admin";
			case PRIV_MASTER: return "master";
			default: return "unknown";
		}
	}

	// if one of scriptclient or scriptircnet is set, then the script echoes to the client or irc respectively
	clientinfo *scriptclient = NULL;
	IRC::Source *scriptircsource = NULL;

	bool is_admin(IRC::Source *source) {
		return strchr(source->peer->data, 'a');
	}

	bool can_script(IRC::Source *source) {
		return strchr(source->peer->data, 'a') || strchr(source->peer->data, 's');
	}

	const char *modename(int n, const char *unknown)
	{
		if(m_valid(n)) return gamemodes[n - STARTGAMEMODE].name;
		return unknown;
	}

	const char *mastermodename(int n, const char *unknown)
	{
		return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodenames)/sizeof(mastermodenames[0])) ? mastermodenames[n-MM_START] : unknown;
	}


#define CHECK_PERM { \
	if(scriptclient && (scriptclient->privilege < PRIV_ADMIN && !scriptclient->can_script())) return; \
	if(scriptircsource && !is_admin(scriptircsource)) return; \
}

	void echo(const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		defvformatstring(str, fmt, fmt);
		char buf[512];
		if(scriptclient) vmessage(scriptclient->clientnum, fmt, ap);
		else if(scriptircsource) {
			color_sauer2irc(str, buf);
			scriptircsource->reply("%s", buf);
		} else {
			color_sauer2console(str, buf);
			puts(buf);
		}
		va_end(ap);
	}
	ICOMMAND(echo, "C", (char *s), echo("%s", s));
	void say(const char *fmt, ...) {
		defvformatstring(str, fmt, fmt);
		char buf[512];
		message("%s", str);
		color_sauer2irc(str, buf);
		irc.speak(1, "%s", buf);
		color_sauer2console(str, buf);
		puts(buf);
	}
	ICOMMAND(say, "C", (char *s), say("%s", s));
	ICOMMAND(color, "iR", (int *c, char *s), {
		*c = clamp(*c, 0, 7);
		string str;
		if(!s) s=(char *)"";
		formatstring(str)("\f%d%s%s", *c, s, *s?"\f7":"");
		result(str);
	});
	ICOMMAND(colorreset, "", (), result(scriptclient?"\f7":scriptircsource?"\x0f":"\033[0m"););

#define MAXIRC 384
	ICOMMAND(ircwho, "", (), {
		for(unsigned int i = 0; i < irc.servers.size(); i++) {
			IRC::Server *s = irc.servers[i];
			for(unsigned int j = 0; j < s->channels.size(); j++) {
				IRC::Channel *c = s->channels[j];
				Wrapper w(MAXIRC);
				w.sep = "";
				w.append("%s:%s: ", s->host, c->name);
				for(unsigned int k = 0; k < c->peers.size(); k++) {
					w.append("%s", c->peers[k]->peer->nick);
					w.sep = ", ";
				}
				loopvk(w.lines) echo("%s", w.lines[k].s);
			}
		}
	});
	// bot names
	ICOMMAND(addbotname, "ss", (char *name, char *pwd), {
		CHECK_PERM;
		if(aiman::addbotname(name)) say("Botname \"%s\" added.", name);
		else echo("Botname \"%s\" already exists.", name);
	});
	ICOMMAND(delbotname, "ss", (char *name), {
		CHECK_PERM;
		if(aiman::delbotname(name)) say("Botname \"%s\" removed.", name);
		else echo("Could not find botname \"%s\".", name);
	});
	ICOMMAND(listbotnames, "", (), aiman::listbotnames());

	struct login {
		string user;
		string password; // hashed
		string permissions; // each char is a permission
	};
	vector <login> logins;
	ICOMMAND(addlogin, "sss", (char *u, char *p, char *perms), {
		CHECK_PERM;
		loopv(logins) { // check for existing user
			if(!strcmp(logins[i].user, u)) {
				copystring(logins[i].password, p);
				if(perms) copystring(logins[i].permissions, perms);
				return;
			}
		}
		login l;
		copystring(l.user, u);
		copystring(l.password, p);
		copystring(l.permissions, perms);
		logins.add(l);
	});
	ICOMMAND(clearlogins, "", (), { CHECK_PERM; logins.setsize(0); });
	static void gotlogins(evhttp_request *req, void *arg) {
		if(!req) return;
		evbuffer *buf = evhttp_request_get_input_buffer(req);
		if(!buf) return;
		char *ln = NULL;
		stream *s = openfile("logins.cfg", "w");
		if(!s) return;
		while((ln = evbuffer_readln_nul(buf, NULL, EVBUFFER_EOL_ANY))) {
			s->putline(ln);
			free(ln);
		}
		execfile("logins.cfg");
		privilegemsg(PRIV_ADMIN, "Updated \f1logins.cfg");
	}
	ICOMMAND(getlogins, "", (), {
		CHECK_PERM;
		if(*loginsurl) {
			echo("Getting logins from \f2%s", loginsurl);
			froghttp_get(evbase, dnsbase, loginsurl, gotlogins, NULL);
		}
	});
	ICOMMAND(getuptime, "", (), result(timestr(totalmillis)));
	ICOMMAND(getmastermode, "", (), { string s; formatstring(s)("%d", mastermode); result(s); } );
	ICOMMAND(mastermodename, "i", (int *mode), result(mastermodename(*mode)));
	ICOMMAND(getgamemode, "", (), { string s; formatstring(s)("%d", gamemode); result(s); } );
	ICOMMAND(modename, "i", (int *mode), result(modename(*mode)));
	ICOMMAND(mapname, "", (), result(smapname));
	void forcemap(const char *s, int mode);
	ICOMMAND(map, "si", (char *map, int *mode), { forcemap(map, mode?*mode:0); });
	SVAR(version, FROGMOD_VERSION);
#ifdef HAVE_PROC
	ICOMMAND(getrss, "", (), {
		int64_t vmrss;
		proc_get_mem_usage(&vmrss, NULL);
		defformatstring(s)("%lldkB", vmrss);
		result(s);
	});
	ICOMMAND(getvsz, "", (), {
		int64_t vmsize;
		proc_get_mem_usage(NULL, &vmsize);
		defformatstring(s)("%lldkB", vmsize);
		result(s);
	});
#else
	ICOMMAND(getrss, "", (), result(""));
	ICOMMAND(getvsz, "", (), result(""));
#endif
    ICOMMAND(listclients, "", (), {
        vector<char> buf;
        string cn;
        loopv(clients) if(clients[i])
        {
        	if(i > 0) buf.add(' ');
            formatstring(cn)("%d", clients[i]->clientnum);
            buf.put(cn, strlen(cn));
        }
        buf.add('\0');
        result(buf.getbuf());
    });
    ICOMMAND(getclientname, "i", (int *cn), {
		if(!cn) result("");
        clientinfo *ci = (clientinfo *)getclientinfo(*cn);
        result(ci ? ci->name : "");
    });
    ICOMMAND(getclientteam, "i", (int *cn), {
		if(!cn) result("");
        clientinfo *ci = (clientinfo *)getclientinfo(*cn);
        result(ci ? ci->team : "");
    });
    ICOMMAND(getclientip, "i", (int *cn), result(getclientipstr(*cn)));
    ICOMMAND(getclienthostname, "i", (int *cn), result(getclienthostname(*cn)));
#ifdef HAVE_GEOIP
    ICOMMAND(getclientcountry, "i", (int *cn), result(getclientcountry(*cn)));
#else
    ICOMMAND(getclientcountry, "i", (int *cn), result(""));
#endif
    ICOMMAND(getclientuptime, "i", (int *cn), { //FIXME: split into more functions (ie timestr)
    	clientinfo *ci = (clientinfo *)getclientinfo(*cn);
    	result(ci ? timestr(totalmillis - ci->connectmillis) : "");
    });
	ICOMMAND(getclientstate, "i", (int *cn), {
		clientinfo *ci = (clientinfo *)getclientinfo(*cn);
		if(ci) result(ci->state.statename());
		else result("");
    });
    int ispriv(int cn, int minimum) {
    	clientinfo *ci = (clientinfo *)getclientinfo(cn);
    	if(ci && ci->privilege >= minimum) return 1;
    	return 0;
    }
    ICOMMAND(ismaster, "i", (int *cn), intret(ispriv(*cn, PRIV_MASTER)));
    ICOMMAND(isadmin, "i", (int *cn), intret(ispriv(*cn, PRIV_ADMIN)));
	void who() {
		if(scriptircsource) {
			if(clients.length() > 0) {
				Wrapper w(MAXIRC);

				loopv(clients) {
					char buf[MAXIRC+1];
					clientinfo *ci = clients[i];
					if(is_admin(scriptircsource)) {
#ifdef HAVE_GEOIP
						if(ci->privilege)
							snprintf(buf, sizeof(buf), "\00305%s\00314 (%d/%s/%s/%s/%s)", ci->name, ci->clientnum, privname(ci->privilege), ci->state.statename(), getclientipstr(ci->clientnum), getclientcountry(ci->clientnum));
						else
							snprintf(buf, sizeof(buf), "\00306%s\00314 (%d/%s/%s/%s)", ci->name, ci->clientnum, ci->state.statename(), getclientipstr(ci->clientnum), getclientcountry(ci->clientnum));
					} else {
						if(ci->privilege)
							snprintf(buf, sizeof(buf), "\00305%s\00314 (%d/%s/%s/%s)", ci->name, ci->clientnum, privname(ci->privilege), ci->state.statename(), getclientcountry(ci->clientnum));
						else
							snprintf(buf, sizeof(buf), "\00306%s\00314 (%d/%s/%s)", ci->name, ci->clientnum, ci->state.statename(), getclientcountry(ci->clientnum));
#else
						if(ci->privilege)
							snprintf(buf, sizeof(buf), "\00305%s\00314 (%d/%s/%s/%s)", ci->name, ci->clientnum, privname(ci->privilege), ci->state.statename(), getclientipstr(ci->clientnum));
						else
							snprintf(buf, sizeof(buf), "\00306%s\00314 (%d/%s/%s)", ci->name, ci->clientnum, ci->state.statename(), getclientipstr(ci->clientnum));
					} else {
						if(ci->privilege)
							snprintf(buf, sizeof(buf), "\00305%s\00314 (%d/%s/%s)", ci->name, ci->clientnum, privname(ci->privilege), ci->state.statename());
						else
							snprintf(buf, sizeof(buf), "\00306%s\00314 (%d/%s)", ci->name, ci->clientnum, ci->state.statename());
#endif
					}

					w.append((char *)buf);
				}
				loopv(w.lines) echo("%s", w.lines[i].s); // reply to sender actually
			} else echo("\00314No players on the server.");
		} else {
			if(clients.length() == 0) echo("No clients on the server.");
			else loopv(clients) {
				int cn = clients[i]->clientnum;
				if(cn < 0 || cn > MAXCLIENTS) continue; // no bots. FIXME: is this a proper way to check?
				int connectedseconds = (totalmillis - clients[i]->connectmillis);
				if((scriptclient && scriptclient->privilege < PRIV_ADMIN)) { // check for NOT admin
#ifdef HAVE_GEOIP
					echo("%d %s (%s) from \f3%s\f7 connected for %sh", cn, clients[i]->name, clients[i]->state.statename(), getclientcountry(cn), timestr(connectedseconds));
#else
					echo("%d %s (%s) connected for %sh", cn, clients[i]->name, clients[i]->state.statename(), timestr(connectedseconds));
#endif
				} else {
#ifdef HAVE_GEOIP
					echo("%d %s (%s) \f3%s %s\f7 from \f6%s\f7 connected for %sh", cn, clients[i]->name, clients[i]->state.statename(), getclientipstr(cn), getclienthostname(cn), getclientcountry(cn), timestr(connectedseconds));
#else
					echo("%d %s (%s) \f3%s %s\f7 connected for %sh", cn, clients[i]->name, clients[i]->state.statename(), getclientipstr(cn), getclienthostname(cn), timestr(connectedseconds));
#endif
				}
			}
		}
	}
	COMMAND(who, "");

	// write some variables, selectively
	void writecfg() {
		stream *f = openfile(path("config.cfg", true), "w");

		if(f) {
//			f->printf("bantime \"%d\"\n", bantime);
			loopv(bans) if(bans[i].expiry < 0) f->printf("pban \"%s\"\n", bans[i].match);
			loopv(blacklisted)
				f->printf("blacklist \"%s\" \"%s\"\n", blacklisted[i].match, blacklisted[i].reason);
			loopv(whitelisted)
				f->printf("whitelist \"%s\" \"%s\"\n", whitelisted[i].match, whitelisted[i].reason);

			aiman::savebotnames(f);

			cmdwritecfg(f);

			delete f;
		}
  	}
	COMMAND(writecfg, "");

	void writelogins() {
		stream *f = openfile(path("config.cfg", true), "w");

		if(f) {
			loopv(logins) {
				f->printf("addlogin \"%s\" \"%s\" \"%s\"\n", logins[i].user, logins[i].password, logins[i].permissions);
			}
		}
	}

	void *newclientinfo() { return new clientinfo; }
	void deleteclientinfo(void *ci) { delete (clientinfo *)ci; }

	clientinfo *getinfo(int n)
	{
		if(n < MAXCLIENTS) return (clientinfo *)getclientinfo(n);
		n -= MAXCLIENTS;
		return bots.inrange(n) ? bots[n] : NULL;
	}

	vector<server_entity> sents;
	vector<savedscore> scores;

	int msgsizelookup(int msg)
	{
		static int sizetable[NUMSV] = { -1 };
		if(sizetable[0] < 0)
		{
			memset(sizetable, -1, sizeof(sizetable));
			for(const int *p = msgsizes; *p >= 0; p += 2) sizetable[p[0]] = p[1];
		}
		return msg >= 0 && msg < NUMSV ? sizetable[msg] : -1;
	}

	void sendservmsg(const char *s) { sendf(-1, 1, "ris", SV_SERVMSG, s); }

	void resetitems()
	{
		sents.setsize(0);
		//cps.reset();
	}

	bool serveroption(const char *arg)
	{
		if(arg[0]=='-') switch(arg[1])
		{
			case 'n': setsvar("serverdesc", &arg[2]); return true;
			case 'y': setsvar("serverpass", &arg[2]); return true;
			case 'p': setsvar("adminpass", &arg[2]); return true;
			case 'o': setvar("publicserver", atoi(&arg[2])); return true;
			case 'g': setvar("serverbotlimit", atoi(&arg[2])); return true;
		}
		return false;
	}

	void kick(int victim);
	void addban(const char *match, char *name = NULL, int btime = bantime);
	const char *colorname(clientinfo *ci, char *name = NULL, bool color = true);
	void httpcb(evhttp_request *req, void *arg) {
		const char *uri = evhttp_request_get_uri(req);
		evkeyvalq *query = NULL; // uninitialized
		evhttp_parse_query(uri, query);
		const char *q_pass = evhttp_find_header(query, "pass");
		const char *q_kick = evhttp_find_header(query, "kick");
		const char *q_ban = evhttp_find_header(query, "ban");
		evhttp_clear_headers(query);

		evbuffer *buf = evbuffer_new();
		if(q_kick) {
			if(q_pass) {
				if(!strcmp(q_pass, adminpass)) {
					loopv(clients) {
						int cn = clients[i]->clientnum;
						if(clients[i] && !strcmp(getclientipstr(cn), q_kick)) {
							kick(cn);
							message("\f3%s is being kicked by http command", colorname(clients[i]));
							irc.speak("\00305%s is being kicked by http command", colorname(clients[i], NULL, false));
							evbuffer_add_printf(buf, "{ \"success\": \"client kicked\" }\n");
							break;
						}
					}
				} else evbuffer_add_printf(buf, "{ \"error\": \"bad password\" }\n");
			} else {
				evbuffer_add_printf(buf, "{ \"error\": \"password not specified\" }\n");
			}
		} else if(q_ban) {
			if(q_pass) {
				if(!strcmp(q_pass, adminpass)) {
					addban(q_ban, NULL, -1);
					evbuffer_add_printf(buf, "{ \"success\": \"IP banned\" }\n");
				} else evbuffer_add_printf(buf, "{ \"error\": \"bad password\" }\n");
			} else {
				evbuffer_add_printf(buf, "{ \"error\": \"password not specified\" }\n");
			}
		} else {
			evbuffer_add_printf(buf, "{\n\t\"map\": \"%s\",\n\t\"mode\": %d,\n\t\"modename\": \"%s\",\n\t\"maxclients\": %d,\n\t\"clients\": [\n", smapname, gamemode, modename(gamemode), maxclients);
			loopv(clients)
				if(clients[i]) {
					int cn = clients[i]->clientnum;
					evbuffer_add_printf(buf, "%s\t\t{\n\t\t\t\"name\": \"%s\",\n\t\t\t\"cn\": %d,\n\t\t\t\"ip\": \"%s\",\n\t\t\t\"host\": \"%s\"\n\t\t}\n", i > 0 ? "," : "", clients[i]->name, cn, getclientipstr(cn), getclienthostname(cn));
				}
			evbuffer_add_printf(buf, "\t]\n}");
		}

		evkeyvalq *oh = evhttp_request_get_output_headers(req);
		evhttp_add_header(oh, "Content-type", "application/json");

		evhttp_send_reply(req, 200, "OK", buf);
		evbuffer_free(buf);
	}

	static void http404cb(struct evhttp_request *req, void *arg) {
		evhttp_send_error(req, 404, "Not found");
	}

	struct evhttp *http;
	void http_init(int port) {
		http = evhttp_new(evbase);
		evhttp_bind_socket(http, NULL, httpport);
		evhttp_set_cb(http, "/", httpcb, NULL);
		evhttp_set_gencb(http, http404cb, NULL);
		printf("HTTP server up.\n");
	}

	void ircmsgcb(IRC::Source *source, char *msg);
	void ircactioncb(IRC::Source *source, char *msg) {
		string buf, buf2;
		color_irc2sauer(msg, buf2);
		buf[escapestring(buf, buf2, buf2 + strlen(buf2))] = 0;
		message("\f4%s \f1* %s\f7 %s", source->channel->alias, source->peer->nick, msg);
		const char *al = getalias("ircactioncb");
		if(al && *al) {
			defformatstring(str)("ircactioncb \"%s\" \"%s\" \"%s\"", buf, source->peer->nick, source->server->host, source->channel->name);
			scriptircsource = source;
			execute(str);
			scriptircsource = NULL;
		}
	}
	void ircnoticecb(IRC::Server *s, char *prefix, char *trailing) {
		if(prefix) echo("\f2[%s]\f1 -%s- %s\f7", s->host, prefix, trailing);
		else echo("\f2[%s]\f1 %s\f7", s->host, trailing);
	}
	void ircpingcb(IRC::Server *s, char *prefix, char *trailing) {
		echo("\f2[%s PING/PONG]\f1 %s\f7", s->host, trailing?trailing:"");
	}
	void ircjoincb(IRC::Source *s) {
		message("\f4%s \f1%s\f7 \f4has joined", s->channel->alias, s->peer->nick);
		echo("\f2[%s %s] \f1%s joined", s->server->host, s->channel->name, s->peer->nick);
	}
	void ircpartcb(IRC::Source *s, char *reason) {
		if(reason) {
			message("\f4 %s \f1%s\f7 \f4has parted (%s)", s->channel->alias, s->peer->nick, reason);
			echo("\f2[%s %s] \f1%s parted (%s)", s->server->host, s->channel->name, s->peer->nick, reason);
		} else {
			message("\f4%s \f1%s\f7 \f4has parted", s->channel->alias, s->peer->nick);
			echo("\f2[%s %s] \f1%s parted", s->server->host, s->channel->name, s->peer->nick);
		}
	}

	void serverinit()
	{
		smapname[0] = '\0';
		resetitems();

		if(httpport > 0) {
			printf("Initializing http server on port %d\n", httpport);
			http_init(httpport);
		}

		//FIXME: too many?
		persistidents=false;
		execfile("stdlib.cfg", false);
		persistidents=true;
		execfile("logins.cfg", false);
		execfile("config.cfg", false);
		irc.channel_message_cb = irc.private_message_cb = ircmsgcb;
		irc.channel_action_message_cb = irc.private_action_message_cb = ircactioncb;
		irc.notice_cb = irc.motd_cb = ircnoticecb;
		irc.ping_cb = ircpingcb;
		irc.join_cb = ircjoincb;
		irc.part_cb = ircpartcb;
	}

	int numclients(int exclude = -1, bool nospec = true, bool noai = true)
	{
		int n = 0;
		loopv(clients) if(i!=exclude && (!nospec || clients[i]->state.state!=CS_SPECTATOR) && (!noai || clients[i]->state.aitype == AI_NONE)) n++;
		return n;
	}

	bool duplicatename(clientinfo *ci, char *name)
	{
		if(!name) name = ci->name;
		loopv(clients) if(clients[i]!=ci && !strcmp(name, clients[i]->name)) return true;
		return false;
	}

	const char *colorname(clientinfo *ci, char *name, bool color)
	{
		if(!name) name = ci->name;
		if(name[0] && !duplicatename(ci, name) && ci->state.aitype == AI_NONE) return name;
		static string cname[3];
		static int cidx = 0;
		cidx = (cidx+1)%3;
		if(color)
			formatstring(cname[cidx])(ci->state.aitype == AI_NONE ? "%s \fs\f5(%d)\fr" : "%s \fs\f5[%d]\fr", name, ci->clientnum);
		else
			formatstring(cname[cidx])(ci->state.aitype == AI_NONE ? "%s (%d)" : "%s [%d]", name, ci->clientnum);
		return cname[cidx];
	}

	bool canspawnitem(int type) { return !m_noitems && (type>=I_SHELLS && type<=I_QUAD && (!m_noammo || type<I_SHELLS || type>I_CARTRIDGES)); }

	int spawntime(int type)
	{
		if(m_classicsp) return INT_MAX;
		int np = numclients(-1, true, false);
		np = np<3 ? 4 : (np>4 ? 2 : 3);         // spawn times are dependent on number of players
		int sec = 0;
		switch(type)
		{
			case I_SHELLS:
			case I_BULLETS:
			case I_ROCKETS:
			case I_ROUNDS:
			case I_GRENADES:
			case I_CARTRIDGES: sec = np*4; break;
			case I_HEALTH: sec = np*5; break;
			case I_GREENARMOUR:
			case I_YELLOWARMOUR: sec = 20; break;
			case I_BOOST:
			case I_QUAD: sec = 40+rnd(40); break;
		}
		return sec*1000;
	}

	bool pickup(int i, int sender)         // server side item pickup, acknowledge first client that gets it
	{
		if(minremain<=0 || !sents.inrange(i) || !sents[i].spawned) return false;
		clientinfo *ci = getinfo(sender);
		if(!ci || (!ci->local && !ci->state.canpickup(sents[i].type))) return false;
		sents[i].spawned = false;
		sents[i].spawntime = spawntime(sents[i].type);
		sendf(-1, 1, "ri3", SV_ITEMACC, i, sender);
		ci->state.pickup(sents[i].type);
		return true;
	}

	clientinfo *choosebestclient(float &bestrank)
	{
		clientinfo *best = NULL;
		bestrank = -1;
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(ci->state.timeplayed<0) continue;
			float rank = ci->state.state!=CS_SPECTATOR ? ci->state.effectiveness/max(ci->state.timeplayed, 1) : -1;
			if(!best || rank > bestrank) { best = ci; bestrank = rank; }
		}
		return best;
	}

	void autoteam()
	{
		static const char *teamnames[2] = {"good", "evil"};
		vector<clientinfo *> team[2];
		float teamrank[2] = {0, 0};
		for(int round = 0, remaining = clients.length(); remaining>=0; round++)
		{
			int first = round&1, second = (round+1)&1, selected = 0;
			while(teamrank[first] <= teamrank[second])
			{
				float rank;
				clientinfo *ci = choosebestclient(rank);
				if(!ci) break;
				if(smode && smode->hidefrags()) rank = 1;
				else if(selected && rank<=0) break;
				ci->state.timeplayed = -1;
				team[first].add(ci);
				if(rank>0) teamrank[first] += rank;
				selected++;
				if(rank<=0) break;
			}
			if(!selected) break;
			remaining -= selected;
		}
		loopi(sizeof(team)/sizeof(team[0]))
		{
			loopvj(team[i])
			{
				clientinfo *ci = team[i][j];
				if(!strcmp(ci->team, teamnames[i])) continue;
				copystring(ci->team, teamnames[i], MAXTEAMLEN+1);
				sendf(-1, 1, "riis", SV_SETTEAM, ci->clientnum, teamnames[i]);
			}
		}
	}

	struct teamrank
	{
		const char *name;
		float rank;
		int clients;

		teamrank(const char *name) : name(name), rank(0), clients(0) {}
	};

	const char *chooseworstteam(const char *suggest = NULL, clientinfo *exclude = NULL)
	{
		teamrank teamranks[2] = { teamrank("good"), teamrank("evil") };
		const int numteams = sizeof(teamranks)/sizeof(teamranks[0]);
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(ci==exclude || ci->state.aitype!=AI_NONE || ci->state.state==CS_SPECTATOR || !ci->team[0]) continue;
			ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
			ci->state.lasttimeplayed = lastmillis;

			loopj(numteams) if(!strcmp(ci->team, teamranks[j].name))
			{
				teamrank &ts = teamranks[j];
				ts.rank += ci->state.effectiveness/max(ci->state.timeplayed, 1);
				ts.clients++;
				break;
			}
		}
		teamrank *worst = &teamranks[numteams-1];
		loopi(numteams-1)
		{
			teamrank &ts = teamranks[i];
			if(smode && smode->hidefrags())
			{
				if(ts.clients < worst->clients || (ts.clients == worst->clients && ts.rank < worst->rank)) worst = &ts;
			}
			else if(ts.rank < worst->rank || (ts.rank == worst->rank && ts.clients < worst->clients)) worst = &ts;
		}
		return worst->name;
	}

	void writedemo(int chan, void *data, int len)
	{
		if(!demorecord) return;
		int stamp[3] = { (int)gamemillis, chan, len }; //FIXME: ugh
		lilswap(stamp, 3);
		demorecord->write(stamp, sizeof(stamp));
		demorecord->write(data, len);
	}

	void recordpacket(int chan, void *data, int len)
	{
		writedemo(chan, data, len);
	}

	void enddemorecord()
	{
		if(!demorecord) return;

		DELETEP(demorecord);

		if(!demotmp) return;

		int len = demotmp->size();
		if(demos.length()>=MAXDEMOS)
		{
			delete[] demos[0].data;
			demos.remove(0);
		}
		demofile &d = demos.add();
		time_t t = time(NULL);
		char *timestr = ctime(&t), *trim = timestr + strlen(timestr);
		while(trim>timestr && isspace(*--trim)) *trim = '\0';
		formatstring(d.info)("%s: %s, %s, %.2f%s", timestr, modename(gamemode), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
		message("Demo \"%s\" recorded", d.info);
		d.data = new uchar[len];
		d.len = len;
		demotmp->seek(0, SEEK_SET);
		demotmp->read(d.data, len);
		DELETEP(demotmp);
	}

	int welcomepacket(packetbuf &p, clientinfo *ci);
	void sendwelcome(clientinfo *ci);

	void setupdemorecord()
	{
		if(!m_mp(gamemode) || m_edit) return;

		demotmp = opentempfile("demorecord", "w+b");
		if(!demotmp) return;

		stream *f = opengzfile(NULL, "wb", demotmp);
		if(!f) { DELETEP(demotmp); return; }

		sendservmsg("recording demo");

		demorecord = f;

		demoheader hdr;
		memcpy(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic));
		hdr.version = DEMO_VERSION;
		hdr.protocol = PROTOCOL_VERSION;
		lilswap(&hdr.version, 2);
		demorecord->write(&hdr, sizeof(demoheader));

		packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		welcomepacket(p, NULL);
		writedemo(1, p.buf, p.len);
	}

	void listdemos(int cn)
	{
		packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		putint(p, SV_SENDDEMOLIST);
		putint(p, demos.length());
		loopv(demos) sendstring(demos[i].info, p);
		sendpacket(cn, 1, p.finalize());
	}

	void cleardemos(int n)
	{
		if(!n)
		{
			loopv(demos) delete[] demos[i].data;
			demos.setsize(0);
			sendservmsg("cleared all demos");
		}
		else if(demos.inrange(n-1))
		{
			delete[] demos[n-1].data;
			demos.remove(n-1);
			message("Cleared demo %d", n);
		}
	}

	void senddemo(int cn, int num)
	{
		if(!num) num = demos.length();
		if(!demos.inrange(num-1)) return;
		demofile &d = demos[num-1];
		sendf(cn, 2, "rim", SV_SENDDEMO, d.len, d.data);
	}

	void enddemoplayback()
	{
		if(!demoplayback) return;
		DELETEP(demoplayback);

		loopv(clients) sendf(clients[i]->clientnum, 1, "ri3", SV_DEMOPLAYBACK, 0, clients[i]->clientnum);

		sendservmsg("demo playback finished");

		loopv(clients) sendwelcome(clients[i]);
	}

	void setupdemoplayback()
	{
		if(demoplayback) return;
		demoheader hdr;
		string msg;
		msg[0] = '\0';
		defformatstring(file)("%s.dmo", smapname);
		demoplayback = opengzfile(file, "rb");
		if(!demoplayback) formatstring(msg)("Could not read demo \"%s\"", file);
		else if(demoplayback->read(&hdr, sizeof(demoheader))!=sizeof(demoheader) || memcmp(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic)))
			formatstring(msg)("\"%s\" is not a demo file", file);
		else
		{
			lilswap(&hdr.version, 2);
			if(hdr.version!=DEMO_VERSION) formatstring(msg)("Demo \"%s\" requires an %s version of Cube 2: Sauerbraten", file, hdr.version<DEMO_VERSION ? "older" : "newer");
			else if(hdr.protocol!=PROTOCOL_VERSION) formatstring(msg)("Demo \"%s\" requires an %s version of Cube 2: Sauerbraten", file, hdr.protocol<PROTOCOL_VERSION ? "older" : "newer");
		}
		if(msg[0])
		{
			DELETEP(demoplayback);
			sendservmsg(msg);
			return;
		}

		message("Playing demo \"%s\"", file);

		demomillis = 0;
		sendf(-1, 1, "ri3", SV_DEMOPLAYBACK, 1, -1);

		if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
		{
			enddemoplayback();
			return;
		}
		lilswap(&nextplayback, 1);
	}

	void readdemo()
	{
		if(!demoplayback || gamepaused) return;
		demomillis += curtime;
		while(demomillis>=nextplayback)
		{
			int chan, len;
			if(demoplayback->read(&chan, sizeof(chan))!=sizeof(chan) ||
			demoplayback->read(&len, sizeof(len))!=sizeof(len))
			{
				enddemoplayback();
				return;
			}
			lilswap(&chan, 1);
			lilswap(&len, 1);
			ENetPacket *packet = enet_packet_create(NULL, len, 0);
			if(!packet || demoplayback->read(packet->data, len)!=len)
			{
				if(packet) enet_packet_destroy(packet);
				enddemoplayback();
				return;
			}
			sendpacket(-1, chan, packet);
			if(!packet->referenceCount) enet_packet_destroy(packet);
			if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
			{
				enddemoplayback();
				return;
			}
			lilswap(&nextplayback, 1);
		}
	}

	void stopdemo()
	{
		if(m_demo) enddemoplayback();
		else enddemorecord();
	}

	void pausegame(bool val)
	{
		if(gamepaused==val) return;
		gamepaused = val;
		sendf(-1, 1, "rii", SV_PAUSEGAME, gamepaused ? 1 : 0);
	}

	void hashpassword(int cn, int sessionid, const char *pwd, char *result, int maxlen)
	{
		char buf[2*sizeof(string)];
		formatstring(buf)("%d %d ", cn, sessionid);
		copystring(&buf[strlen(buf)], pwd);
		if(!hashstring(buf, result, maxlen)) *result = '\0';
	}

	bool checkpassword(clientinfo *ci, const char *wanted, const char *given)
	{
		string hash;
		hashpassword(ci->clientnum, ci->sessionid, wanted, hash, sizeof(hash));
		return !strcmp(hash, given);
	}

	void revokemaster(clientinfo *ci)
	{
		ci->privilege = PRIV_NONE;
		if(ci->state.state==CS_SPECTATOR && !ci->local) aiman::removeai(ci);
	}

	void setmaster(clientinfo *ci, bool val, const char *pass = "", const char *authname = NULL)
	{
		if(authname && !val) return;
		const char *name = "";
		if(val)
		{
			bool haspass = adminpass[0] && checkpassword(ci, adminpass, pass);

			if(is_blacklisted(ci->clientnum) && !haspass) { whisper(ci->clientnum, "Cannot take master: blacklisted."); return; }

			if(ci->privilege) if(!adminpass[0] || haspass==(ci->privilege==PRIV_ADMIN)) return;

			loopv(clients) if(ci!=clients[i] && clients[i]->privilege)
			{
				if(haspass) clients[i]->privilege = PRIV_NONE;
				else if((authname || ci->local) && clients[i]->privilege<=PRIV_MASTER) continue;
				else return;
			}
			if(haspass) ci->privilege = PRIV_ADMIN;
			else if(!authname && !(mastermask&MM_AUTOAPPROVE) && !ci->privilege && !ci->local)
			{
				sendf(ci->clientnum, 1, "ris", SV_SERVMSG, "This server requires you to use the \"/auth\" command to gain master.");
				return;
			}
			else
			{
				if(authname)
				{
					loopv(clients) if(ci!=clients[i] && clients[i]->privilege<=PRIV_MASTER) revokemaster(clients[i]);
				}
				ci->privilege = PRIV_MASTER;
			}
			name = privname(ci->privilege);
		}
		else
		{
			if(!ci->privilege) return;
			name = privname(ci->privilege);
			revokemaster(ci);
		}
		if(!val) mastermode = MM_OPEN;
		allowedips.setsize(0);
		if(val && authname) {
			message("%s claimed %s as '\fs\f5%s\fr'. Mastermode is \f0%s\f7 (\f6%d\f7).", colorname(ci), name, authname, mastermodename(mastermode), mastermode);
			irc.speak(1, "\00306%s\00314 claimed %s as '\00303%s\00314'. Mastermode is %s (%d)", colorname(ci, NULL, false), name, authname, mastermodename(mastermode), mastermode);
		} else {
			message("%s %s %s. Mastermode is \f0%s\f7 (\f6%d\f7)", colorname(ci), val ? "claimed" : "relinquished", name, mastermodename(mastermode), mastermode);
			irc.speak(1, "\00306%s\00314 %s %s. Mastermode is %s (%d)", colorname(ci, NULL, false), val ? "claimed" : "relinquished", name, mastermodename(mastermode), mastermode);
		}

		currentmaster = val ? ci->clientnum : -1;
		masterupdate = true;
		if(gamepaused)
		{
			int admins = 0;
			loopv(clients) if(clients[i]->privilege >= PRIV_ADMIN || clients[i]->local) admins++;
			if(!admins) pausegame(false);
		}
	}
	ICOMMAND(takemaster, "", (), {
		if(currentmaster > -1) {
			clientinfo *ci = (clientinfo *)getclientinfo(currentmaster);
			if(ci) setmaster(ci, 0);
		}
	});

	savedscore &findscore(clientinfo *ci, bool insert)
	{
		uint ip = getclientip(ci->clientnum);
		if(!ip && !ci->local) return *(savedscore *)0;
		if(!insert)
		{
			loopv(clients)
			{
				clientinfo *oi = clients[i];
				if(oi->clientnum != ci->clientnum && getclientip(oi->clientnum) == ip && !strcmp(oi->name, ci->name))
				{
					oi->state.timeplayed += lastmillis - oi->state.lasttimeplayed;
					oi->state.lasttimeplayed = lastmillis;
					static savedscore curscore;
					curscore.save(oi->state);
					return curscore;
				}
			}
		}
		loopv(scores)
		{
			savedscore &sc = scores[i];
			if(sc.ip == ip && !strcmp(sc.name, ci->name)) return sc;
		}
		if(!insert) return *(savedscore *)0;
		savedscore &sc = scores.add();
		sc.ip = ip;
		copystring(sc.name, ci->name);
		return sc;
	}

	void savescore(clientinfo *ci)
	{
		savedscore &sc = findscore(ci, true);
		if(&sc) sc.save(ci->state);
	}

	int checktype(int type, clientinfo *ci)
	{
		if(ci && ci->local) return type;
		// only allow edit messages in coop-edit mode
		if(type>=SV_EDITENT && type<=SV_EDITVAR && !m_edit) return -1;
		// server only messages
		static int servtypes[] = { SV_SERVINFO, SV_INITCLIENT, SV_WELCOME, SV_MAPRELOAD, SV_SERVMSG, SV_DAMAGE, SV_HITPUSH, SV_SHOTFX, SV_DIED, SV_SPAWNSTATE, SV_FORCEDEATH, SV_ITEMACC, SV_ITEMSPAWN, SV_TIMEUP, SV_CDIS, SV_CURRENTMASTER, SV_PONG, SV_RESUME, SV_BASESCORE, SV_BASEINFO, SV_BASEREGEN, SV_ANNOUNCE, SV_SENDDEMOLIST, SV_SENDDEMO, SV_DEMOPLAYBACK, SV_SENDMAP, SV_DROPFLAG, SV_SCOREFLAG, SV_RETURNFLAG, SV_RESETFLAG, SV_INVISFLAG, SV_CLIENT, SV_AUTHCHAL, SV_INITAI };
		if(ci) loopi(sizeof(servtypes)/sizeof(int)) if(type == servtypes[i]) return -1;
		return type;
	}

	void cleanworldstate(ENetPacket *packet)
	{
		loopv(worldstates)
		{
			worldstate *ws = worldstates[i];
			if(ws->positions.inbuf(packet->data) || ws->messages.inbuf(packet->data)) ws->uses--;
			else continue;
			if(!ws->uses)
			{
				delete ws;
				worldstates.remove(i);
			}
			break;
		}
	}

	void addclientstate(worldstate &ws, clientinfo &ci)
	{
		if(ci.position.empty()) ci.posoff = -1;
		else
		{
			ci.posoff = ws.positions.length();
			loopvj(ci.position) ws.positions.add(ci.position[j]);
			ci.poslen = ws.positions.length() - ci.posoff;
			ci.position.setsizenodelete(0);
		}
		if(ci.messages.empty()) ci.msgoff = -1;
		else
		{
			ci.msgoff = ws.messages.length();
			ucharbuf p = ws.messages.reserve(16);
			putint(p, SV_CLIENT);
			putint(p, ci.clientnum);
			putuint(p, ci.messages.length());
			ws.messages.addbuf(p);
			loopvj(ci.messages) ws.messages.add(ci.messages[j]);
			ci.msglen = ws.messages.length() - ci.msgoff;
			ci.messages.setsizenodelete(0);
		}
	}

	void addeditcommands(worldstate &ws) {
		if(totalmillis - lastplaybackmillis < (int64_t)playbackmillis) return; // limit playback speed to avoid lag
		lastplaybackmillis = totalmillis;

		int goodcn = -1;

		vector <uchar> q;
		ucharbuf qb = q.reserve(64);

		loopv(clients) {
			int cn = clients[i]->clientnum;

			if(goodcn < 0) goodcn = cn; // find a good cn

			if(clients[i]->playing) {
				char buf[1024] = "";

				clients[i]->playing->getline(buf, sizeof(buf));

				char *t = strtok(buf, "\t\n ");
				if(t && *t) {
					int type = strtol(t, NULL, 0);
					putint(qb, type);
					int nrest = msgsizelookup(type) - 4;
					loopj(3) {
						char *s = strtok(NULL, "\t\n ");
						if(s && *s) {
							int w = clients[i]->playorigin.v[j];
							int v = strtol(s, NULL, 0);
							putint(qb, w + v);
						}
					}
					loopj(nrest) {
						char *s = strtok(NULL, "\t\n ");
//						printf("j %d s %s\n", j, s);
						if(s && *s) putint(qb, strtol(s, NULL, 0));
//						else printf("s %p type %d j %d\n", s, type, j);
					}
				}

				if(clients[i]->playing->end()) {
					DELETEP(clients[i]->playing);
					whisper(cn, "Finished playing.");
				}
			}
		}

		q.addbuf(qb);

		if(q.length() > 0) {
			ucharbuf p = ws.messages.reserve(64);
			putint(p, SV_CLIENT);
			putint(p, goodcn);
			putuint(p, q.length());
			ws.messages.addbuf(p);
			loopvj(q) ws.messages.add(q[j]);

			q.setsizenodelete(0);
		}
	}

	bool buildworldstate()
	{
		worldstate &ws = *new worldstate;
		loopv(clients)
		{
			clientinfo &ci = *clients[i];
			if(ci.state.aitype != AI_NONE) continue;
			addclientstate(ws, ci);
			loopv(ci.bots)
			{
				clientinfo &bi = *ci.bots[i];
				addclientstate(ws, bi);
				if(bi.posoff >= 0)
				{
					if(ci.posoff < 0) { ci.posoff = bi.posoff; ci.poslen = bi.poslen; }
					else ci.poslen += bi.poslen;
				}
				if(bi.msgoff >= 0)
				{
					if(ci.msgoff < 0) { ci.msgoff = bi.msgoff; ci.msglen = bi.msglen; }
					else ci.msglen += bi.msglen;
				}
			}
		}

		//vampi
		addeditcommands(ws);

		int psize = ws.positions.length(), msize = ws.messages.length();
		if(psize) recordpacket(0, ws.positions.getbuf(), psize);
		if(msize) recordpacket(1, ws.messages.getbuf(), msize);
		loopi(psize) { uchar c = ws.positions[i]; ws.positions.add(c); }
		loopi(msize) { uchar c = ws.messages[i]; ws.messages.add(c); }
		ws.uses = 0;
		if(psize || msize) loopv(clients)
		{
			clientinfo &ci = *clients[i];
			if(ci.state.aitype != AI_NONE) continue;
			ENetPacket *packet;
			if(psize && (ci.posoff<0 || psize-ci.poslen>0))
			{
				packet = enet_packet_create(&ws.positions[ci.posoff<0 ? 0 : ci.posoff+ci.poslen],
											ci.posoff<0 ? psize : psize-ci.poslen,
											ENET_PACKET_FLAG_NO_ALLOCATE);
				sendpacket(ci.clientnum, 0, packet);
				if(!packet->referenceCount) enet_packet_destroy(packet);
				else { ++ws.uses; packet->freeCallback = cleanworldstate; }
			}

			if(msize && (ci.msgoff<0 || msize-ci.msglen>0))
			{
				packet = enet_packet_create(&ws.messages[ci.msgoff<0 ? 0 : ci.msgoff+ci.msglen],
											ci.msgoff<0 ? msize : msize-ci.msglen,
											(reliablemessages ? ENET_PACKET_FLAG_RELIABLE : 0) | ENET_PACKET_FLAG_NO_ALLOCATE);
				sendpacket(ci.clientnum, 1, packet);
				if(!packet->referenceCount) enet_packet_destroy(packet);
				else { ++ws.uses; packet->freeCallback = cleanworldstate; }
			}
		}
		reliablemessages = false;
		if(!ws.uses)
		{
			delete &ws;
			return false;
		}
		else
		{
			worldstates.add(&ws);
			return true;
		}
	}

	bool sendpackets()
	{
		if(clients.empty() || (!hasnonlocalclients() && !demorecord)) return false;
		int64_t curtime = get_ticks() - lastsend;
		if(curtime<33) return false;
		bool flush = buildworldstate();
		lastsend += curtime - (curtime%33);
		return flush;
	}

	template<class T>
	void sendstate(gamestate &gs, T &p)
	{
		putint(p, gs.lifesequence);
		putint(p, gs.health);
		putint(p, gs.maxhealth);
		putint(p, gs.armour);
		putint(p, gs.armourtype);
		putint(p, gs.gunselect);
		loopi(GUN_PISTOL-GUN_SG+1) putint(p, gs.ammo[GUN_SG+i]);
	}

	void spawnstate(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		gs.spawnstate(gamemode);
		gs.lifesequence = (gs.lifesequence + 1)&0x7F;
	}

	void sendspawn(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		spawnstate(ci);
		sendf(ci->ownernum, 1, "rii7v", SV_SPAWNSTATE, ci->clientnum, gs.lifesequence,
			gs.health, gs.maxhealth,
			gs.armour, gs.armourtype,
			gs.gunselect, GUN_PISTOL-GUN_SG+1, &gs.ammo[GUN_SG]);
		gs.lastspawn = gamemillis;
	}

	void sendwelcome(clientinfo *ci)
	{
		packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		int chan = welcomepacket(p, ci);
		sendpacket(ci->clientnum, chan, p.finalize());
	}

	void putinitclient(clientinfo *ci, packetbuf &p)
	{
		if(ci->state.aitype != AI_NONE)
		{
			putint(p, SV_INITAI);
			putint(p, ci->clientnum);
			putint(p, ci->ownernum);
			putint(p, ci->state.aitype);
			putint(p, ci->state.skill);
			putint(p, ci->playermodel);
			sendstring(ci->name, p);
			sendstring(ci->team, p);
		}
		else
		{
			putint(p, SV_INITCLIENT);
			putint(p, ci->clientnum);
			sendstring(ci->name, p);
			sendstring(ci->team, p);
			putint(p, ci->playermodel);
		}
	}

	void welcomeinitclient(packetbuf &p, int exclude = -1)
	{
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(!ci->connected || ci->clientnum == exclude) continue;

			putinitclient(ci, p);
		}
	}

	int welcomepacket(packetbuf &p, clientinfo *ci)
	{
		int hasmap = (m_edit && (clients.length()>1 || (ci && ci->local))) || (smapname[0] && (minremain>0 || (ci && ci->state.state==CS_SPECTATOR) || numclients(ci && ci->local ? ci->clientnum : -1)));
		putint(p, SV_WELCOME);
		putint(p, hasmap);
		if(hasmap)
		{
			putint(p, SV_MAPCHANGE);
			sendstring(smapname, p);
			putint(p, gamemode);
			putint(p, notgotitems ? 1 : 0);
			if(!ci || (m_timed && smapname[0]))
			{
				putint(p, SV_TIMEUP);
				putint(p, minremain);
			}
			if(!notgotitems)
			{
				putint(p, SV_ITEMLIST);
				loopv(sents) if(sents[i].spawned)
				{
					putint(p, i);
					putint(p, sents[i].type);
				}
				putint(p, -1);
			}
		}
		if(gamepaused)
		{
			putint(p, SV_PAUSEGAME);
			putint(p, 1);
		}
		if(ci)
		{
			putint(p, SV_SETTEAM);
			putint(p, ci->clientnum);
			sendstring(ci->team, p);
		}
		if(ci && (m_demo || m_mp(gamemode)) && ci->state.state!=CS_SPECTATOR)
		{
			if(smode && !smode->canspawn(ci, true))
			{
				ci->state.state = CS_DEAD;
				putint(p, SV_FORCEDEATH);
				putint(p, ci->clientnum);
				sendf(-1, 1, "ri2x", SV_FORCEDEATH, ci->clientnum, ci->clientnum);
			}
			else
			{
				gamestate &gs = ci->state;
				spawnstate(ci);
				putint(p, SV_SPAWNSTATE);
				putint(p, ci->clientnum);
				sendstate(gs, p);
				gs.lastspawn = gamemillis;
			}
		}
		if(ci && ci->state.state==CS_SPECTATOR)
		{
			putint(p, SV_SPECTATOR);
			putint(p, ci->clientnum);
			putint(p, 1);
			sendf(-1, 1, "ri3x", SV_SPECTATOR, ci->clientnum, 1, ci->clientnum);
		}
		if(!ci || clients.length()>1)
		{
			putint(p, SV_RESUME);
			loopv(clients)
			{
				clientinfo *oi = clients[i];
				if(ci && oi->clientnum==ci->clientnum) continue;
				putint(p, oi->clientnum);
				putint(p, oi->state.state);
				putint(p, oi->state.frags);
				putint(p, oi->state.quadmillis);
				sendstate(oi->state, p);
			}
			putint(p, -1);
			welcomeinitclient(p, ci ? ci->clientnum : -1);
		}
		if(smode) smode->initclient(ci, p, true);
		return 1;
	}

	bool restorescore(clientinfo *ci)
	{
		//if(ci->local) return false;
		savedscore &sc = findscore(ci, false);
		if(&sc)
		{
			sc.restore(ci->state);
			return true;
		}
		return false;
	}

	void sendresume(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		sendf(-1, 1, "ri2i9vi", SV_RESUME, ci->clientnum,
			gs.state, gs.frags, gs.quadmillis,
			gs.lifesequence,
			gs.health, gs.maxhealth,
			gs.armour, gs.armourtype,
			gs.gunselect, GUN_PISTOL-GUN_SG+1, &gs.ammo[GUN_SG], -1);
	}

	void sendinitclient(clientinfo *ci)
	{
		packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		putinitclient(ci, p);
		sendpacket(-1, 1, p.finalize(), ci->clientnum);
	}

	void changemap(const char *s, int mode)
	{
		stopdemo();
		pausegame(false);
		if(smode) smode->reset(false);
		aiman::clearai();

		mapreload = false;
		gamemode = mode;
		gamemillis = 0;
		minremain = m_overtime ? 15 : 10;
		gamelimit = minremain*60000;
		interm = 0;
		copystring(smapname, s);
		resetitems();
		notgotitems = true;
		scores.setsize(0);
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
		}

		if(!m_mp(gamemode)) kicknonlocalclients(DISC_PRIVATE);

		if(m_teammode) autoteam();

		if(m_capture) smode = &capturemode;
		else if(m_ctf) smode = &ctfmode;
		else smode = NULL;
		if(smode) smode->reset(false);

		if(m_timed && smapname[0]) sendf(-1, 1, "ri2", SV_TIMEUP, minremain);
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			ci->mapchange();
			ci->state.lasttimeplayed = lastmillis;
			if(m_mp(gamemode) && ci->state.state!=CS_SPECTATOR) sendspawn(ci);
		}

		aiman::changemap();

		if(m_demo)
		{
			if(clients.length()) setupdemoplayback();
		}
		else if(demonextmatch)
		{
			demonextmatch = false;
			setupdemorecord();
		}

		firstblood = false;

		irc.speak(2, "\00312Map: \00306%s\00314 (\00307%s\00314)", smapname, modename(mode));
	}

	struct votecount
	{
		char *map;
		int mode, count;
		votecount() {}
		votecount(char *s, int n) : map(s), mode(n), count(0) {}
	};

	void checkvotes(bool force = false)
	{
		vector<votecount> votes;
		int maxvotes = 0;
		loopv(clients)
		{
			clientinfo *oi = clients[i];
			if(oi->state.state==CS_SPECTATOR && !oi->privilege && !oi->local) continue;
			if(oi->state.aitype!=AI_NONE) continue;
			maxvotes++;
			if(!oi->mapvote[0]) continue;
			votecount *vc = NULL;
			loopvj(votes) if(!strcmp(oi->mapvote, votes[j].map) && oi->modevote==votes[j].mode)
			{
				vc = &votes[j];
				break;
			}
			if(!vc) vc = &votes.add(votecount(oi->mapvote, oi->modevote));
			vc->count++;
		}
		votecount *best = NULL;
		loopv(votes) if(!best || votes[i].count > best->count || (votes[i].count == best->count && rnd(2))) best = &votes[i];
		if(force || (best && best->count > maxvotes/2))
		{
			if(demorecord) enddemorecord();
			if(best && (best->count > (force ? 1 : maxvotes/2)))
			{
				sendservmsg(force ? "vote passed by default" : "vote passed by majority");
				sendf(-1, 1, "risii", SV_MAPCHANGE, best->map, best->mode, 1);
				changemap(best->map, best->mode);
			}
			else
			{
				mapreload = true;
				if(clients.length()) sendf(-1, 1, "ri", SV_MAPRELOAD);
			}
		}
	}

	void forcemap(const char *map, int mode)
	{
		stopdemo();
		if(hasnonlocalclients() && !mapreload)
		{
			message("Local player forced %s on map %s", modename(mode), map);
		}
		sendf(-1, 1, "risii", SV_MAPCHANGE, map, mode, 1);
		changemap(map, mode);
	}

	void vote(char *map, int reqmode, int sender)
	{
		clientinfo *ci = getinfo(sender);
		if(!ci || (ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) || (!ci->local && !m_mp(reqmode))) return;
		copystring(ci->mapvote, map);
		ci->modevote = reqmode;
		if(!ci->mapvote[0]) return;
		if(ci->local || mapreload || (ci->privilege && mastermode>=MM_VETO))
		{
			if(demorecord) enddemorecord();
			if((!ci->local || hasnonlocalclients()) && !mapreload)
			{
				message("%s forced %s on map %s", ci->privilege && mastermode>=MM_VETO ? privname(ci->privilege) : "local player", modename(ci->modevote), ci->mapvote);
			}
			sendf(-1, 1, "risii", SV_MAPCHANGE, ci->mapvote, ci->modevote, 1);
			changemap(ci->mapvote, ci->modevote);
		}
		else
		{
			message("%s suggests %s on map %s (select map to vote)", colorname(ci), modename(reqmode), map);
			checkvotes();
		}
	}

	void checkintermission()
	{
		if(minremain>0)
		{
			minremain = gamemillis>=gamelimit ? 0 : (gamelimit - gamemillis + 60000 - 1)/60000;
			sendf(-1, 1, "ri2", SV_TIMEUP, (int)minremain);
			if(!minremain && smode) smode->intermission();
		}
		if(!interm && minremain<=0) {
			interm = gamemillis+10000;
			if(clients.length() > 0) {
				irc.speak(2, "\00312Intermission.");
				if(webhook[0]) {
					char *url = (char *)malloc(strlen(webhook) + strlen("?action=intermission") + 1);
					if(url) {
						sprintf(url, "%s?action=intermission", webhook);
						loopv(clients) {
							clientinfo *ci = clients[i];
							if(ci) {
								char *epl = evhttp_encode_uri(ci->name);
								defformatstring(pl)("&players[]=%s,%d,%d", epl, ci->state.frags, ci->state.deaths);
								free(epl);
								url = (char *)realloc(url, strlen(url) + strlen(pl) + 1);
								if(!url) break;
								strcat(url, pl);
							}
						}
						if(url) {
							printf("getting %s\n", url);
							froghttp_get(evbase, dnsbase, url, NULL, NULL);
							free(url);
						}
					}
				}
			}
		}
	}

	void startintermission() {
		gamelimit = min(gamelimit, gamemillis);
		checkintermission();
	}

	// killing spree messages
	struct spreemsg {
		int frags;
		string msg1, msg2;
	};
	vector <spreemsg> spreemessages;
	ICOMMAND(addspreemsg, "iss", (int *frags, char *msg1, char *msg2), { spreemsg m; m.frags = *frags; copystring(m.msg1, msg1); copystring(m.msg2, msg2); spreemessages.add(m); });
	struct multikillmsg {
		int frags;
		string msg;
	};
	vector <multikillmsg> multikillmessages;
	SVAR(defmultikillmsg, "MULTI KILL"); // this is the default message for multikills that don't have a string. it is followed by the number of frags in braces
	VAR(minmultikill, 2, 2, INT_MAX); // minimum number of kills for a multi kill
	ICOMMAND(addmultikillmsg, "is", (int *frags, char *msg), { multikillmsg m; m.frags = *frags; copystring(m.msg, msg); multikillmessages.add(m); });

	void dodamage(clientinfo *target, clientinfo *actor, int damage, int gun, const vec &hitpush = vec(0, 0, 0))
	{
		gamestate &ts = target->state;
		ts.dodamage(damage);
		actor->state.damage += damage;
		sendf(-1, 1, "ri6", SV_DAMAGE, target->clientnum, actor->clientnum, damage, ts.armour, ts.health);
		if(target!=actor && !hitpush.iszero())
		{
			ivec v = vec(hitpush).rescale(DNF);
			sendf(ts.health<=0 ? -1 : target->ownernum, 1, "ri7", SV_HITPUSH, target->clientnum, gun, damage, v.x, v.y, v.z);
		}
		if(ts.health<=0)
		{
			target->state.deaths++;
			if(actor!=target && isteam(actor->team, target->team)) actor->state.teamkills++;
			int fragvalue = smode ? smode->fragvalue(target, actor) : (target==actor || isteam(target->team, actor->team) ? -1 : 1);
			actor->state.frags += fragvalue;
			if(fragvalue>0)
			{
				int friends = 0, enemies = 0; // note: friends also includes the fragger
				if(m_teammode) loopv(clients) if(strcmp(clients[i]->team, actor->team)) enemies++; else friends++;
				else { friends = 1; enemies = clients.length()-1; }
				actor->state.effectiveness += fragvalue*friends/float(max(enemies, 1));

				if(totalmillis - actor->state.lastfragmillis < (int64_t)multifragmillis) {
					actor->state.multifrags++;
				} else {
					actor->state.multifrags = 1;
				}
				actor->state.lastfragmillis = totalmillis;
			}
			sendf(-1, 1, "ri4", SV_DIED, target->clientnum, actor->clientnum, actor->state.frags);
			if(!firstblood && actor != target) { firstblood = true; message("\f2%s drew \f6FIRST BLOOD!!!", colorname(actor)); }
			if(actor != target) actor->state.spreefrags++;
			if(target->state.spreefrags >= minspreefrags) {
				if(actor == target)
					message("\f2%s %s", colorname(target), spreesuicidemsg);
				else
					message("\f2%s%s %s", colorname(target), spreeendmsg, colorname(actor));
			}
			target->state.spreefrags = 0;
			target->state.multifrags = 0;
			target->state.lastfragmillis = 0;
			loopv(spreemessages) {
				if(actor->state.spreefrags == spreemessages[i].frags) message("\f2%s %s \f6%s", colorname(actor), spreemessages[i].msg1, spreemessages[i].msg2);
			}
			target->position.setsizenodelete(0);
			if(smode) smode->died(target, actor);
			ts.state = CS_DEAD;
			ts.lastdeath = gamemillis;
			// don't issue respawn yet until DEATHMILLIS has elapsed
			// ts.respawn();
		}
	}

	void suicide(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		if(gs.state!=CS_ALIVE) return;
		ci->state.frags += smode ? smode->fragvalue(ci, ci) : -1;
		ci->state.deaths++;
		sendf(-1, 1, "ri4", SV_DIED, ci->clientnum, ci->clientnum, gs.frags);
		if(gs.spreefrags >= 5) message("\f2%s was looking good until he killed himself", colorname(ci));
		gs.spreefrags = 0;
		gs.multifrags = 0;
		gs.lastfragmillis = 0;
		ci->position.setsizenodelete(0);
		if(smode) smode->died(ci, NULL);
		gs.state = CS_DEAD;
		gs.respawn();
	}

	void suicideevent::process(clientinfo *ci)
	{
		suicide(ci);
	}

	void explodeevent::process(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		switch(gun)
		{
			case GUN_RL:
				if(!gs.rockets.remove(id)) return;
				break;

			case GUN_GL:
				if(!gs.grenades.remove(id)) return;
				break;

			default:
				return;
		}
		loopv(hits)
		{
			hitinfo &h = hits[i];
			clientinfo *target = getinfo(h.target);
			if(!target || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence || h.dist<0 || h.dist>RL_DAMRAD) continue;

			bool dup = false;
			loopj(i) if(hits[j].target==h.target) { dup = true; break; }
			if(dup) continue;

			int damage = guns[gun].damage;
			if(gs.quadmillis) damage *= 4;
			damage = int(damage*(1-h.dist/RL_DISTSCALE/RL_DAMRAD));
			if(gun==GUN_RL && target==ci) damage /= RL_SELFDAMDIV;
			dodamage(target, ci, damage, gun, h.dir);
		}
	}

	void shotevent::process(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		int64_t wait = millis - gs.lastshot;
		if(!gs.isalive(gamemillis) ||
		wait < gs.gunwait ||
		gun<GUN_FIST || gun>GUN_PISTOL ||
		gs.ammo[gun]<=0)
			return;
		if(gun!=GUN_FIST) gs.ammo[gun]--;
		gs.lastshot = millis;
		gs.gunwait = guns[gun].attackdelay;
		sendf(-1, 1, "ri9x", SV_SHOTFX, ci->clientnum, gun,
				int(from.x*DMF), int(from.y*DMF), int(from.z*DMF),
				int(to.x*DMF), int(to.y*DMF), int(to.z*DMF),
				ci->ownernum);
		gs.shotdamage += guns[gun].damage*(gs.quadmillis ? 4 : 1)*(gun==GUN_SG ? SGRAYS : 1);
		switch(gun)
		{
			case GUN_RL: gs.rockets.add(id); break;
			case GUN_GL: gs.grenades.add(id); break;
			default:
			{
				int totalrays = 0, maxrays = gun==GUN_SG ? SGRAYS : 1;
				loopv(hits)
				{
					hitinfo &h = hits[i];
					clientinfo *target = getinfo(h.target);
					if(!target || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence || h.rays<1) continue;

					totalrays += h.rays;
					if(totalrays>maxrays) continue;
					int damage = h.rays*guns[gun].damage;
					if(gs.quadmillis) damage *= 4;
					dodamage(target, ci, damage, gun, h.dir);
				}
				break;
			}
		}
	}

	void pickupevent::process(clientinfo *ci)
	{
		gamestate &gs = ci->state;
		if(m_mp(gamemode) && !gs.isalive(gamemillis)) return;
		pickup(ent, ci->clientnum);
	}

	bool gameevent::flush(clientinfo *ci, int64_t fmillis)
	{
		process(ci);
		return true;
	}

	bool timedevent::flush(clientinfo *ci, int64_t fmillis)
	{
		if(millis > fmillis) return false;
		else if(millis >= ci->lastevent)
		{
			ci->lastevent = millis;
			process(ci);
		}
		return true;
	}

	void clearevent(clientinfo *ci)
	{
		delete ci->events.remove(0);
	}

	void flushevents(clientinfo *ci, int millis)
	{
		while(ci->events.length())
		{
			gameevent *ev = ci->events[0];
			if(ev->flush(ci, millis)) clearevent(ci);
			else break;
		}
	}

	void processevents()
	{
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(curtime>0 && ci->state.quadmillis) ci->state.quadmillis = max(ci->state.quadmillis-curtime, int64_t(0));
			flushevents(ci, gamemillis);
		}
	}

	void cleartimedevents(clientinfo *ci)
	{
		int keep = 0;
		loopv(ci->events)
		{
			if(ci->events[i]->keepable())
			{
				if(keep < i)
				{
					for(int j = keep; j < i; j++) delete ci->events[j];
					ci->events.remove(keep, i - keep);
					i = keep;
				}
				keep = i+1;
				continue;
			}
		}
		while(ci->events.length() > keep) delete ci->events.pop();
		ci->timesync = false;
	}

	void serverupdate()
	{
		if(!gamepaused) gamemillis += curtime;

		if(m_demo) readdemo();
		else if(!gamepaused && minremain>0)
		{
			processevents();
			if(curtime)
			{
				loopv(sents) if(sents[i].spawntime) // spawn entities when timer reached
				{
					int oldtime = sents[i].spawntime;
					sents[i].spawntime -= curtime;
					if(sents[i].spawntime<=0)
					{
						sents[i].spawntime = 0;
						sents[i].spawned = true;
						sendf(-1, 1, "ri2", SV_ITEMSPAWN, i);
					}
					else if(sents[i].spawntime<=10000 && oldtime>10000 && (sents[i].type==I_QUAD || sents[i].type==I_BOOST))
					{
						sendf(-1, 1, "ri2", SV_ANNOUNCE, sents[i].type);
					}
				}
			}
			aiman::checkai();
			if(smode) smode->update();
		}

		loopv(connects) if(totalmillis-connects[i]->connectmillis>15000) disconnect_client(connects[i]->clientnum, DISC_TIMEOUT);

		if(masterupdate)
		{
			clientinfo *m = currentmaster>=0 ? getinfo(currentmaster) : NULL;
			sendf(-1, 1, "ri3", SV_CURRENTMASTER, currentmaster, m ? m->privilege : 0);
			masterupdate = false;
		}

		if(!gamepaused && m_timed && smapname[0] && gamemillis-curtime>0 && gamemillis/60000!=(gamemillis-curtime)/60000) checkintermission();
		if(interm && gamemillis>interm)
		{
			if(demorecord) enddemorecord();
			interm = 0;
			checkvotes(true);
		}

		// multi kill
		loopv(clients) {
			clientinfo *ci = clients[i];
			if(totalmillis - ci->state.lastfragmillis >= (int64_t)multifragmillis) {
				if(ci->state.multifrags >= minmultikill) {
					char *msg = NULL;
					loopv(multikillmessages) {
						if(multikillmessages[i].frags == ci->state.multifrags) {
							msg = multikillmessages[i].msg;
							break;
						}
					}
					if(msg) message("\f2%s scored a \f6%s", colorname(ci), msg);
					else message("\f2%s scored a \f6%s (%d)", colorname(ci), defmultikillmsg, ci->state.multifrags);
				}
				ci->state.multifrags = 0;
			}
		}

	}

	struct crcinfo
	{
		int crc, matches;

		crcinfo(int crc, int matches) : crc(crc), matches(matches) {}

		static int compare(const crcinfo *x, const crcinfo *y)
		{
			if(x->matches > y->matches) return -1;
			if(x->matches < y->matches) return 1;
			return 0;
		}
	};

	void checkmaps(int req = -1)
	{
		if(m_edit || !smapname[0]) return;
		vector<crcinfo> crcs;
		int total = 0, unsent = 0, invalid = 0;
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(ci->state.state==CS_SPECTATOR || ci->state.aitype != AI_NONE) continue;
			total++;
			if(!ci->clientmap[0])
			{
				if(ci->mapcrc < 0) invalid++;
				else if(!ci->mapcrc) unsent++;
			}
			else
			{
				crcinfo *match = NULL;
				loopvj(crcs) if(crcs[j].crc == ci->mapcrc) { match = &crcs[j]; break; }
				if(!match) crcs.add(crcinfo(ci->mapcrc, 1));
				else match->matches++;
			}
		}
		if(total - unsent < min(total, 4)) return;
		crcs.sort(crcinfo::compare);
		loopv(clients)
		{
			clientinfo *ci = clients[i];
			if(ci->state.state==CS_SPECTATOR || ci->state.aitype != AI_NONE || ci->clientmap[0] || ci->mapcrc >= 0 || (req < 0 && ci->warned)) continue;
			whisper(req, "%s has modified map \"%s\"", colorname(ci), smapname);
			if(req < 0) ci->warned = true;
		}
		if(crcs.empty() || crcs.length() < 2) return;
		loopv(crcs)
		{
			crcinfo &info = crcs[i];
			if(i || info.matches <= crcs[i+1].matches) loopvj(clients)
			{
				clientinfo *ci = clients[j];
				if(ci->state.state==CS_SPECTATOR || ci->state.aitype != AI_NONE || !ci->clientmap[0] || ci->mapcrc != info.crc || (req < 0 && ci->warned)) continue;
				whisper(req, "%s has modified map \"%s\"", colorname(ci), smapname);
				if(req < 0) ci->warned = true;
			}
		}
	}

	void sendservinfo(clientinfo *ci)
	{
		sendf(ci->clientnum, 1, "ri5", SV_SERVINFO, ci->clientnum, PROTOCOL_VERSION, ci->sessionid, serverpass[0] ? 1 : 0);
	}

	void clearbans();
	void noclients()
	{
		clearbans();
		aiman::clearai();
		gunfinity = chainsaw = 0;
	}

	void localconnect(int n)
	{
		clientinfo *ci = getinfo(n);
		ci->clientnum = ci->ownernum = n;
		ci->connectmillis = totalmillis;
		ci->sessionid = (rnd(0x1000000)*((totalmillis%10000)+1))&0xFFFFFF;
		ci->local = true;

		connects.add(ci);
		sendservinfo(ci);
	}

	void localdisconnect(int n)
	{
		if(m_demo) enddemoplayback();
		clientdisconnect(n);
	}

	int clientconnect(int n, uint ip)
	{
		clientinfo *ci = getinfo(n);
		ci->clientnum = ci->ownernum = n;
		ci->connectmillis = totalmillis;
		ci->sessionid = (rnd(0x1000000)*((totalmillis%10000)+1))&0xFFFFFF;

		connects.add(ci);
		if(!m_mp(gamemode)) return DISC_PRIVATE;
		sendservinfo(ci);
		return DISC_NONE;
	}

	void clientdisconnect(int n, int reason) {
		clientinfo *ci = getinfo(n);
		if(ci->connected) {
			if(ci->privilege) setmaster(ci, false);
			if(smode) smode->leavegame(ci, true);
			ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
			savescore(ci);
			sendf(-1, 1, "ri2", SV_CDIS, n);
			if(ci->name[0]) {
				irc.speak(1, "\00312Disconnect: \00306%s", ci->name);
				echo("\f1Disconnect: \f0%s", ci->name);
				if(webhook[0]) {
					char *ereason = evhttp_encode_uri(disc_reasons[reason]);
					defformatstring(url)("%s?action=disconnect&ip=%s&reason=%s", webhook, getclientipstr(ci->clientnum), ereason);
					free(ereason);
					froghttp_get(evbase, dnsbase, url, NULL, NULL);
				}
			}

			clients.removeobj(ci);
			aiman::removeai(ci);
			if(!numclients(-1, false, true)) noclients(); // bans clear when server empties
		}
		else connects.removeobj(ci);
	}

	int reserveclients() { return 3; }

	int allowconnect(clientinfo *ci, const char *pwd)
	{
		if(ci->local) return DISC_NONE;
		if(!m_mp(gamemode)) return DISC_PRIVATE;
		if(serverpass[0])
		{
			if(!checkpassword(ci, serverpass, pwd)) return DISC_PRIVATE;
			return DISC_NONE;
		}
		if(adminpass[0] && checkpassword(ci, adminpass, pwd)) return DISC_NONE;
		if(numclients(-1, false, true)>=maxclients) return DISC_MAXCLIENTS;
		uint ip = getclientip(ci->clientnum);
		//wildcard matching:
		loopv(bans)
			if(!fnmatch(bans[i].match, getclientipstr(ci->clientnum), 0) ||
			   !fnmatch(bans[i].match, ci->name, 0)) return DISC_IPBAN;
		
		int priv = PRIV_NONE;
		loopv(clients) if(clients[i]->privilege > priv) priv = clients[i]->privilege;
		if(mastermode>=MM_PRIVATE && allowedips.find(ip)<0) return DISC_PRIVATE;
		return DISC_NONE;
	}

	bool allowbroadcast(int n)
	{
		clientinfo *ci = getinfo(n);
		return ci && ci->connected;
	}

	clientinfo *findauth(uint id)
	{
		loopv(clients) if(clients[i]->authreq == id) return clients[i];
		return NULL;
	}

	void authfailed(uint id)
	{
		clientinfo *ci = findauth(id);
		if(!ci) return;
		ci->authreq = 0;
	}

	void authsucceeded(uint id)
	{
		clientinfo *ci = findauth(id);
		if(!ci) return;
		ci->authreq = 0;
		setmaster(ci, true, "", ci->authname);
	}

	void authchallenged(uint id, const char *val)
	{
		clientinfo *ci = findauth(id);
		if(!ci) return;
		sendf(ci->clientnum, 1, "risis", SV_AUTHCHAL, "", id, val);
	}

	uint nextauthreq = 0;

	void tryauth(clientinfo *ci, const char *user)
	{
		if(!nextauthreq) nextauthreq = 1;
		ci->authreq = nextauthreq++;
		filtertext(ci->authname, user, false, 100);
		if(!requestmasterf("reqauth %u %s\n", ci->authreq, ci->authname))
		{
			ci->authreq = 0;
			sendf(ci->clientnum, 1, "ris", SV_SERVMSG, "not connected to authentication server");
		}
	}

	void answerchallenge(clientinfo *ci, uint id, char *val)
	{
		if(ci->authreq != id) return;
		for(char *s = val; *s; s++)
		{
			if(!isxdigit(*s)) { *s = '\0'; break; }
		}
		if(!requestmasterf("confauth %u %s\n", id, val))
		{
			ci->authreq = 0;
			sendf(ci->clientnum, 1, "ris", SV_SERVMSG, "not connected to authentication server");
		}
	}

	void processmasterinput(const char *cmd, int cmdlen, const char *args)
	{
		uint id;
		string val;
		if(sscanf(cmd, "failauth %u", &id) == 1)
			authfailed(id);
		else if(sscanf(cmd, "succauth %u", &id) == 1)
			authsucceeded(id);
		else if(sscanf(cmd, "chalauth %u %s", &id, val) == 2)
			authchallenged(id, val);
	}

	void receivefile(int sender, uchar *data, int len)
	{
		if(!m_edit || len > 1024*1024) return;
		clientinfo *ci = getinfo(sender);
		if(ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) return;
		if(mapdata) DELETEP(mapdata);
		if(!len) return;
		mapdata = opentempfile("mapdata", "w+b");
		if(!mapdata) { sendf(sender, 1, "ris", SV_SERVMSG, "failed to open temporary file for map"); return; }
		mapdata->write(data, len);
		message("[\f0%s\ff uploaded map to server, type \f2/getmap\ff to receive it]", colorname(ci));
	}


	int getcn(char *str) { // return a cn, or -1 if player is not found
		str = trim(str);
		if(str[0] == 0) return -1;

		loopv(clients) { if(!strcmp(str, clients[i]->name)) return clients[i]->clientnum; }

		char *c = str;
		while(*c) if(!isdigit(*c++)) return -1; // client name can start with a digit, so then it's not a number

		loopv(clients) { if(clients[i]->clientnum == atoi(str)) return clients[i]->clientnum; } // only return a valid cn, if numeric

		return -1;
	}

	int tokenize(char *str, const char *fmt, ...) {
		char *p = str;
		int n = 0;
		va_list ap;
		va_start(ap, fmt);

		while(*p && *fmt) {
			while(*p && isspace(*p)) p++; // left trim

			if(*p) {
				n++;

				char *st = p;
				if(*fmt != 'r') {
					if(*p == '"') {
						st++; p++;
						while(*p && *p != '"') p++;
					} else {
						while(*p && !isspace(*p)) p++;
					}
			
					if(*p) *p++ = 0;
				} else while(*++p); // point it to the trailing \0

				switch(*fmt++) {
					case 's':
					case 'r':
						strncpy(va_arg(ap, char *), st, MAXSTRLEN);
						break;
					case 'i':
					{
						int *i = va_arg(ap, int *);
						*i = strtol(st, NULL, 0);
							break;
					}
					case 'f':
					{
						float *i = va_arg(ap, float *);
						*i = strtod(st, NULL);
							break;
					}
					case 'p':
					{
						uint *i = va_arg(ap, uint *);
						*i = ipint(st);
						break;
					}
					case 'c': // a player name or cn. returns a proper cn
					{
						int *i = va_arg(ap, int *);
						*i = getcn(st);
						break;
					}
				}
			}
		}

		va_end(ap);
		return n;
	}

	void bantimer_cb(int fd, short type, void *arg) {
		loopv(bans) {
			if(bans[i].expiry > 0 && get_ticks() >= bans[i].expiry) {
				if(evtimer_initialized(&bans[i].tev)) evtimer_del(&bans[i].tev);
				message("Ban \f3%s (%s)\f7 expired.\n", bans[i].match, bans[i].name);
				bans.remove(i);
				i--;
			}
		}
	}

	void addban(const char *match, char *name, int btime) {
		ban &b = bans.add();
		if(btime > 0) b.expiry = get_ticks() + btime * 60000;
		else b.expiry = -1; // never expire
		copystring(b.match, match);
		if(name) copystring(b.name, name);
		else b.name[0] = 0;
		loopv(allowedips) {
			if(!fnmatch(match, ipstr(allowedips[i]), 0)) { allowedips.remove(i); i--; }
		}

		if(btime < 0) writecfg();
		else {
			evtimer_assign(&b.tev, evbase, &bantimer_cb, &b);
			struct timeval tv;
			tv.tv_sec = btime * 60;
			tv.tv_usec = 0;
			evtimer_add(&b.tev, &tv);
		}
	}
	ICOMMAND(pban, "s", (char *match), {
		CHECK_PERM;
		addban(match, NULL, -1);
	});
	ICOMMAND(ban, "s", (char *match), {
		CHECK_PERM;
		addban(match);
	});
	
	bool delban(char *match) {
		loopv(bans) {
			if(!strcmp(match, bans[i].match)) {
				if(evtimer_initialized(&bans[i].tev)) evtimer_del(&bans[i].tev);
				bans.remove(i);
				return true;
			}
		}
		return false;
	}
	ICOMMAND(unban, "s", (char *match), { CHECK_PERM; delban(match); });

	void addban(int victim) {
		clientinfo *ci = (clientinfo *)getclientinfo(victim);
		if(ci) addban(getclientipstr(victim), ci->name);
	}

	void kick(int victim) {
		if(getclientinfo(victim)) // no bots
			disconnect_client(victim, DISC_KICK);
	}
	ICOMMAND(kick, "s", (char *who), {
		CHECK_PERM;
		kick(getcn(who));
	});

	void clearbans() {
		loopv(bans) {
			if(bans[i].expiry >= 0) {
				if(evtimer_initialized(&bans[i].tev)) evtimer_del(&bans[i].tev);
				bans.remove(i);
				i--;
			}
		}
		message("All bans cleared.");
		irc.speak(1, "\00314All bans cleared.");
	}
	COMMAND(clearbans, "");

	void spectator(int val, int cn) {
		clientinfo *spinfo = (clientinfo *)getclientinfo(cn); // no bots
		if(!spinfo) return;
		if(!spinfo || (spinfo->state.state==CS_SPECTATOR ? val : !val)) return;

		if(spinfo->state.state!=CS_SPECTATOR && val) {
			if(spinfo->state.state==CS_ALIVE) suicide(spinfo);
			if(smode) smode->leavegame(spinfo);
			spinfo->state.state = CS_SPECTATOR;
			spinfo->state.timeplayed += lastmillis - spinfo->state.lasttimeplayed;
			if(!spinfo->local && !spinfo->privilege) aiman::removeai(spinfo);
			message("Player \f2%s\f7 is now a spectator.", spinfo->name);
			irc.speak("\00314Player \00306%s\00314 is now a spectator.", spinfo->name);
		} else if(spinfo->state.state==CS_SPECTATOR && !val) {
			spinfo->state.state = CS_DEAD;
			spinfo->state.respawn();
			spinfo->state.lasttimeplayed = lastmillis;
			aiman::addclient(spinfo);
			if(spinfo->clientmap[0] || spinfo->mapcrc) checkmaps();
			message("Player \f2%s\f7 is no longer a spectator.", spinfo->name);
			irc.speak("\00314Player \00306%s\00314 is no longer a spectator.", spinfo->name);
		}
		sendf(-1, 1, "ri3", SV_SPECTATOR, cn, val);
	}
	ICOMMAND(spectator, "ii", (int *val, int *cn), { spectator(*val, *cn); });

	void setmastermode(int mm) {
		if(mm == mastermode) return;
		if(mm>=MM_OPEN && mm<=MM_PRIVATE) {
			mastermode = mm;
			allowedips.setsize(0);
			if(mm>=MM_PRIVATE)
			{
				loopv(clients) allowedips.add(getclientip(clients[i]->clientnum));
			}
			message("Mastermode is now \f0%s\ff (\f6%d\f7).", mastermodename(mastermode), mastermode);
			irc.speak(1, "\00314Mastermode is now %s (%d).", mastermodename(mastermode), mastermode);
		}
	}
	ICOMMAND(mastermode, "i", (int *mm), { if(mm) setmastermode(*mm); });

	void gothostname(void *info) {
		clientinfo *ci = (clientinfo *)info;
		loopv(bans) if(!fnmatch(bans[i].match, getclienthostname(ci->clientnum), 0)) { disconnect_client(ci->clientnum, DISC_IPBAN); return; }
	}

	ICOMMAND(ircconnect, "ssis", (const char *s, const char *n, int *p, const char *a), {
		if(s && *s && n && *n) irc.connect(s, n, (p&&*p)?*p:6667, (a&&*a)?a:NULL);
	});
	ICOMMAND(ircjoin, "ssis", (const char *s, const char *c, const int *v, const char *a), {
		if(s && *s && c && *c) irc.join(s, c, (v&&*v)?*v:0, (a&&*a)?a:NULL);
	});
	ICOMMAND(ircpart, "ss", (const char *s, const char *c), {
		if(s && *s && c && *c) irc.part(s, c);
	});
	ICOMMAND(ircecho, "C", (const char *msg), {
		string buf;
		color_sauer2irc((char *)msg, buf);
		if(scriptircsource) scriptircsource->speak(buf);
	});

	ICOMMAND(forceintermission, "", (), { startintermission(); });
	ICOMMAND(recorddemo, "i", (int *i), {
		demonextmatch = i && *i;
		echo("Demo recording is %s for next match.", demonextmatch ? "enabled" : "disabled");
	});
	ICOMMAND(stopdemo, "", (), stopdemo());
	ICOMMAND(cleardemos, "i", (int *i), { if(i) cleardemos(*i); });
	ICOMMAND(listdemos, "", (), {
		if(scriptclient) listdemos(scriptclient->clientnum);
		else {
			Wrapper w;
			w.sep = "";
			w.append("Listing demos: ");
			w.sep = ", ";
			loopv(demos) w.append(demos[i].info);
			loopvk(w.lines) echo("%s", w.lines[k].s);
		}
	});
	ICOMMAND(senddemo, "ii", (int *cn, int *i), {
		if(cn && i) senddemo(*cn, *i);
	});

#define MAXIRC 384 // safe bet

	SVAR(ircignore, ""); // ignore nicks that match this filter. set it to * to silence the irc
	void ircmsgcb(IRC::Source *source, char *msg) {
		bool gotfrog = false;
		for(char *c = frogchar; *c; c++) if(msg[0] == *c) { gotfrog = true; break; }
		if(!source->channel || gotfrog) { // frog command
			if(gotfrog) msg++; // skip first char if it's a frogchar
			char *c = msg;
			string command;
			for(int i = 0; *c && !isspace(*c) && i < (int)sizeof(command) - 1; i++, c++) { command[i] = tolower(*c); command[i+1] = 0; }
			c = trim(c);
			scriptircsource = source;
			if(!strcmp(command, "who"))
				execute("who"); // allow this
			else if(!strcmp(command, "info"))
				execute("info");
			else if(!strcmp(command, "login")) {
				string user, pwd;
				int r = tokenize(c, "ss", user, pwd);
				if(r >= 2) {
					string hash;
					getsha1(pwd, hash);
					bool good = false;
					loopvj(logins) {
						if(!strcasecmp(logins[j].user, user) && !strcasecmp(logins[j].password, hash)) {
							copystring(scriptircsource->peer->data, logins[j].permissions);
							message("%s has logged in from IRC", scriptircsource->peer->nick);
							irc.speak(1, "\00306%s\00314 has logged in", scriptircsource->peer->nick);
							good = true; break;
						}
					}
					if(!good) source->reply("\00314Invalid login.");
				} else if(r == 1) {
					if(!strcmp(user, adminpass)) {
						scriptircsource->peer->data[0] = 'a';
						scriptircsource->peer->data[1] = 0;
						message("%s has logged in from IRC", scriptircsource->peer->nick);
						irc.speak(1, "\00306%s\00314 has logged in", scriptircsource->peer->nick);
					}
				} else scriptircsource->reply("\00314Invalid usage.");
			} else if(!strcmp(command, "help")) {
				source->reply("\00314Available commands: \00307who info login");
			} else if(can_script(scriptircsource)) execute(msg);
		} else if(source->channel) { // only relay channel messages, not private messages
			if(fnmatch(ircignore, source->peer->nick, 0)) {
				string buf, buf2;
				color_irc2sauer(msg, buf2);
				buf[escapestring(buf, buf2, buf2 + strlen(buf2))] = 0;
				message("\f4%s \f2<%s> \f7%s", source->channel->alias, source->peer->nick, buf);
				echo("\f2[%s %s]\f1 <%s>\f0 %s\f7", source->server->host, source->channel->name, source->peer->nick, buf);
				const char *al = getalias("ircmsgcb");
				defformatstring(str)("ircmsgcb \"%s\" \"%s\" \"%s\"", buf, source->peer->nick, source->channel->name);
				scriptircsource = source;
				if(al && *al) execute(str);
			}
		}
		scriptircsource = NULL;
	}

	bool frog_command(char *text, int sender, clientinfo *ci) {
		char *c = text;

		echo("\f1<%s> \f0%s\f7", ci->name, text);

		while(*c && isspace(*c)) c++; // avoid leakage of commands into game

		bool gotfrog = false;
		for(char *f = frogchar; *f; f++) if(text[0] == *f) { gotfrog = true; break; }
		if(!gotfrog) return false;

		c++; // skip frogchar

		text = c;

		char command[40] = "";
		for(int i = 0; *c && !isspace(*c) && i < 40; i++, c++) { command[i] = tolower(*c); command[i+1] = 0; }

		c = trim(c);

		while(*c && isspace(*c)) c++; // left trim
		int l = strlen(c);
		while(l > 0 && isspace(c[l])) c[l--] = 0; // right trim

		scriptclient = ci;
		if(!strcmp(command, "help")) {
			if(*c) {
				if(!strcmp(c, "help"))
					whisper(sender, "\f6%s\f7: Prints this message. Use it without an argument to list available commands.", c);
				else if(!strcmp(c, "uptime"))
					whisper(sender, "\f6%s\f7: Show the time you've been connected to this server, and the time the server has been running.", c);
				else if(!strcmp(c, "me"))
					whisper(sender, "\f6%s \f2<text>\f7: Send a special message (equivalent of /me from IRC).\n", c);
				else if(!strcmp(c, "whisper"))
					whisper(sender, "\f6%s \f2<player or clientnum> <message>\f7: Send a private message to a player.\n", c);
				else if(!strcmp(c, "bans"))
					whisper(sender, "\f6%s\f7: Show currently banned IPs and names.", c);
				else if(!strcmp(c, "who"))
					whisper(sender, "\f6%s \f2[admin password]\f7: Show player names"
#ifdef HAVE_GEOIP
					" and countries,"
#endif
					" and if the password is provided, IPs too.", c);
				else if(ci->privilege == PRIV_ADMIN) {
					if(!strcmp(c, "blacklist")) {
						whisper(sender, "\f6%s\f7: Show blacklist. When a blacklisted player joins the server, a warning is issued by the server.", c);
						whisper(sender, "\f6%s\f2 <IP> [reason]\f7: Add an IP to the blacklist, optionally specifying a reason.", c);
					} else if(!strcmp(c, "unblacklist")) {
						whisper(sender, "\f6%s\f7: Remove an IP from the blacklist.", frogchar[0], c);
					} else if(!strcmp(c, "whitelist")) {
						whisper(sender, "\f6%s\f7: Show whitelist. Whitelisted players cannot be kicked by master.", c);
						whisper(sender, "\f6%s\f2 <IP>\f7: Add an IP to the whitelist.", c);
					} else if(!strcmp(c, "unwhitelist")) {
						whisper(sender, "\f6%s\f7: Remove an IP from the whitelist.", c);
					} else if(!strcmp(c, "ban")) {
						whisper(sender, "\f6%s\f2 <IP>\f7: Add an IP to the bans list.", c);
					} else if(!strcmp(c, "kick")) {
						whisper(sender, "\f6%s\f2 <player or cn>\f7: Kick a player without banning.", c);
					} else if(!strcmp(c, "addinfo")) {
						whisper(sender, "\f6%s\f2 <name> <text>\f7: Add an item to the information list.", c);
					} else if(!strcmp(c, "delinfo")) {
						whisper(sender, "\f6%s\f2 <name>\f7: Delete an item from the information list.", c);
					}
				}
			} else {
				whisper(sender, "Type \f6%s\f2 <command>\f7 to see help for a command.", command);
				whisper(sender, "Available commands: \f6help info uptime me whisper bans who listbotnames");
				if(ci->privilege == PRIV_ADMIN) whisper(sender, "Available admin commands: \f6blacklist unblacklist whitelist unwhitelist ban kick ircsay ircme addbotname delbtoname addinfo delinfo");
			}
		} else if(!strcmp(command, "info")) {
			execute("info");
		} else if(!strcmp(command, "me")) {
			if(*c) {
				message("* \f1%s\f7 %s", ci->name, c);
				irc.speak(1 , "\00306*%s \00303%s", ci->name, c);
			}
		} else if(!strcmp(command, "ircsay")) {
			if(ci->privilege == PRIV_ADMIN && *c) {
				irc.speak(1 , "%s", c); // use %s instead of c directly to avoid attacks: if someone says "ircsay %s", it could make the server crash
			}
		} else if(!strcmp(command, "ircme")) {
			if(ci->privilege == PRIV_ADMIN && *c) {
				irc.speak(1 , "\001ACTION %s\001", c); // use %s instead of c directly to avoid attacks: if someone says "ircsay %s", it could make the server crash
			}
		} else if(!strcmp(command, "whisper")) {
			int cn;
			string str;
			int r = tokenize(c, "cr", &cn, str);

			if(r < 2) whisper(sender, "Usage: whisper <cn> <message>.");
			else {
				if(cn > -1 && cn < MAXCLIENTS) {
					whisper(cn, "* \f1%s\f7 whispers to you: %s", ci->name, str);
					clientinfo *to = (clientinfo *)getclientinfo(cn);
					if(to) whisper(sender, "* Whispered to \f2%s\f7.", to->name);
				} else whisper(sender, "Incorrect player specified.");
			}
		} else if(!strcmp(command, "bans")) {
			if(server::bans.length() > 0) {
				sendf(sender, 1, "ris", SV_SERVMSG, "Bans:");
				loopv(bans) {
					if(bans[i].expiry < 0)
						whisper(sender, " \f3*\f7 %s %s, permanent", bans[i].match, bans[i].name);
					else whisper(sender, " \f3*\f7 %s %s, expires in %s", bans[i].match, bans[i].name, timestr(bans[i].expiry - get_ticks()));
				}
			} else sendf(sender, 1, "ris", SV_SERVMSG, "No banned IPs.");
		} else if(!strcmp(command, "uptime")) {
				int connectedseconds = (totalmillis - ci->connectmillis);
				whisper(sender, "This server has been running for \f2%s\f7h.\nYou have been connected to this server for \f2%s\f7h.", timestr(totalmillis), timestr(connectedseconds));
		} else if(!strcmp(command, "blacklist")) {
			if(ci->privilege == PRIV_ADMIN) {
				notice n;
				n.reason[0] = 0;
				int r = tokenize(c, "sr", n.match, n.reason);
				if(r >= 1) {
					blacklisted.add(n);

					writecfg();

					whisper(sender, "Appended \f2%s\f7 to blacklist.", n.match);
				} else {
					if(show_blacklist(sender) < 1)
						whisper(sender, "Blacklist empty.");
				}
			}
		} else if(!strcmp(command, "unblacklist")) {
			if(*c && ci->privilege == PRIV_ADMIN) {
				loopv(blacklisted) if(!strcmp(blacklisted[i].match, c)) { blacklisted.remove(i); i--; }
				writecfg();
				whisper(sender, "Removed %s from blacklist.", c);
			} else whisper(sender, "IP not specified.");
		} else if(!strcmp(command, "whitelist")) {
			if(ci->privilege == PRIV_ADMIN) {
				notice n;
				n.reason[0] = 0;
				int r = tokenize(c, "sr", n.match, n.reason);
				if(r >= 1) {
					whitelisted.add(n);

					writecfg();

					whisper(sender, "Appended \f2%s\f7 to whitelist.", n.match);
				} else {
					if(show_whitelist(sender) < 1)
						whisper(sender, "Whitelist empty.");
				}
			}
		} else if(!strcmp(command, "unwhitelist")) {
			if(*c && ci->privilege == PRIV_ADMIN) {
				loopv(whitelisted) if(!strcmp(whitelisted[i].match, c)) { whitelisted.remove(i); i--; }
				writecfg();
				whisper(sender, "Removed %s from whitelist.", c);
			} else whisper(sender, "IP not specified.");
		} else if(!strcmp(command, "damage")) {
			if(ci->privilege >= PRIV_ADMIN && *c) {
				int victim, damage;
				int r = tokenize(c, "ci", &victim, &damage);
				
				if(r >= 2) {
					clientinfo *vi = (clientinfo *)getclientinfo(victim);
					if(vi) dodamage(vi, ci, damage, GUN_PISTOL);
				} else whisper(sender, "Invalid usage.");
			}
#ifdef FROGMOD_VERSION
		} else if(!strcmp(command, "version")) {
			whisper(sender, "Running FrogMod " FROGMOD_VERSION ".");
#endif
		} else if(!strcmp(command, "givemaster")) {
			if(ci->privilege >= PRIV_MASTER && *c) {
				int cn;
				int r = tokenize(c, "c", &cn);
				if(r >= 1) {
					clientinfo *ni = (clientinfo *)getclientinfo(cn);
					if(ni && ci != ni) {
						ci->privilege = 0;
						ni->privilege = PRIV_MASTER;
						currentmaster = cn;
						masterupdate = true;
						message("\f0%s\f2 gives master to \f0%s\f2. mastermode remains \f0%s\f2 (\f6%d\f2) ", ci->name, ni->name, mastermodename(mastermode), mastermode);
						irc.speak(1, "\00306%s\00314 gives master to \00306%s", ci->name, ni->name);
					}
				}
			}
		} else if(!strcmp(command, "giveadmin")) {
			if(ci->privilege >= PRIV_ADMIN && *c) {
				int cn;
				int r = tokenize(c, "c", &cn);
				if(r >= 1) {
					clientinfo *ni = (clientinfo *)getclientinfo(cn);
					if(ni && ci != ni) {
						ci->privilege = 0;
						ni->privilege = PRIV_ADMIN;
						currentmaster = cn;
						masterupdate = true;
						message("\f0%s\f2 gives admin to \f0%s\f2. mastermode remains \f0%s\f2 (\f6%d\f2) ", ci->name, ni->name, mastermodename(mastermode), mastermode);
						irc.speak(1, "\00306%s\00314 gives admin to \00306%s", ci->name, ni->name);
					}
				}
			}
		} else if(!strcmp(command, "record") || !strcmp(command, "rec")) { //record coopedit commands
			if(*c && m_edit) { //FIXME: check for existing and append .rec extension automatically
				char buf[1024];
				sprintf(buf, "%s.rec", c);
				DELETEP(ci->recording);
				ci->recording = openfile(buf, "w");
				if(!ci->recording) {
					whisper(sender, "Could not start recording %s", c);
				} else {
					whisper(sender, "Recording %s...", c);
				}
			} else whisper(sender, "Filename not specified.");
		} else if(!strcmp(command, "recstop") || !strcmp(command, "stoprec")) {
			if(ci->recording) {
				DELETEP(ci->recording);
				whisper(sender, "Stopped recording.");
			}
			if(ci->playing) {
				DELETEP(ci->playing)
				whisper(sender, "Stopped playing.");
			}
		} else if(!strcmp(command, "recplay")) {
			if(*c && m_edit) {
				DELETEP(ci->recording);
				char buf[1024];
				sprintf(buf, "%s.rec", c);
				ci->playing = openfile(buf, "r"); //FIXME: better avoid path attacks?
				if(ci->playing) whisper(sender, "Playing back %s...", c);
				else whisper(sender, "Could not find recording \"%s\"", c);
			} else whisper(sender, "Filename not specified.");
		} else if(!strcmp(command, "reclist") || !strcmp(command, "listrec")) {
			glob_t pglob;
			int r = glob("*?.rec", 0, NULL, &pglob); // at least one char
			
			if(r == 0) {
				if(pglob.gl_pathc > 0) {
					Wrapper w;
					w.append("\f6Available recordings:\f7");
					loopi(pglob.gl_pathc) {
						char *buf = pglob.gl_pathv[i];
						int l = strlen(buf);
						buf[l - 4] = 0; // discard .rec extension
						w.append("\f2%s\f7", buf);
						w.sep = ", ";
					}
					loopv(w.lines) whisper(sender, "%s", w.lines[i].s);
				} else whisper(sender, "\f6No recordings.");
				globfree(&pglob);
			} else if(r == GLOB_NOMATCH) whisper(sender, "\f6No recordings.");
			else whisper(sender, "\f6Could not list recordings.");
		} else if(!strcmp(command, "recdel")) {
			if(ci->privilege >= PRIV_ADMIN && *c) { //FIXME: check if file exists
				char buf[1024];
				sprintf(buf, "%s.rec", c);
				unlink(buf);
				whisper(sender, "Recording \"%s\" deleted.", c);
			}
		} else if(!strcmp(command, "sendto")) {
			if(ci->privilege >= PRIV_MASTER) {
				int cn;
				int r = tokenize(c, "c", &cn);
				if(r >= 1) {
					clientinfo *to = (clientinfo *)getclientinfo(cn);
					if(mapdata && to)
					{
						message("\f1Master sending map to \f2%s\f1...", to->name);
						sendfile(cn, 2, mapdata, "ri", SV_SENDMAP);
					}
					else sendf(sender, 1, "ris", SV_SERVMSG, "No map to send");
				}
			}
		} else if(!strcmp(command, "login")) {
			string user, pwd;
			int r = tokenize(c, "ss", user, pwd);
			if(r >= 2) {
				string hash;
				getsha1(pwd, hash);
				loopv(logins) {
					if(!strcasecmp(logins[i].user, user) && !strcasecmp(logins[i].password, hash)) {
						ci->logged_in = true;
						copystring(ci->permissions, logins[i].permissions);
						message("%s has now logged in.", ci->name);
						irc.speak(1, "\00306%s\00314 has now logged in.", ci->name);
					}
				}
			} else if(r == 1) {
				if(!strcmp(user, adminpass)) {
						ci->logged_in = true;
						copystring(ci->permissions, "a");
						message("%s has now logged in.", ci->name);
						irc.speak(1, "\00306%s\00314 has now logged in.", ci->name);
				}
			}
		} else if(!strcmp(command, "who")) {
			execute(command);
		} else if(ci->can_script()) execute(text);

		scriptclient = NULL;

		return true;
	}

	VAR(autosend, 0, 0, 1);
	void parsepacket(int sender, int chan, packetbuf &p)     // has to parse exactly each byte of the packet
	{
		if(sender<0) return;
		char text[MAXTRANS];
		int type;
		clientinfo *ci = sender>=0 ? getinfo(sender) : NULL, *cq = ci, *cm = ci;
		if(ci && !ci->connected)
		{
			if(chan==0) return;
			else if(chan!=1 || getint(p)!=SV_CONNECT) { disconnect_client(sender, DISC_TAGT); return; }
			else
			{ // SV_CONNECT here
				// get name
				getstring(text, p);
				filtertext(text, text, false, MAXNAMELEN);
				if(!text[0]) copystring(text, "unnamed");
				copystring(ci->name, text, MAXNAMELEN+1);

				getstring(text, p); // get password hash
				int disc = allowconnect(ci, text);
				if(disc) {
					disconnect_client(sender, disc);
					return;
				}

				if(is_blacklisted(sender)) message("\f3Blacklist warning\f7: \f2%s\f7: %s", colorname(ci), blacklist_reason(sender));

				ci->playermodel = getint(p);

				if(m_demo) enddemoplayback();

				connects.removeobj(ci);
				clients.add(ci);

				ci->connected = true;
				if(mastermode>=MM_LOCKED) ci->state.state = CS_SPECTATOR;
				if(currentmaster>=0) masterupdate = true;
				ci->state.lasttimeplayed = lastmillis;

				const char *worst = m_teammode ? chooseworstteam(text, ci) : NULL;
				copystring(ci->team, worst ? worst : "good", MAXTEAMLEN+1);
				sendwelcome(ci);
				if(restorescore(ci)) sendresume(ci);
				sendinitclient(ci);

				aiman::addclient(ci);

				if(m_demo) setupdemoplayback();

				if(servermotd[0]) whisper(sender, servermotd);

#ifdef HAVE_GEOIP
				const char *country = getclientcountrynul(sender);
				if(country) {
					message("%s is connected from \f6%s\f7", ci->name, country);
					irc.speak(1, "\00312Connected: \00306%s\00314 from \00307%s", ci->name, country);
					echo("\f1Connected: \f0%s \f3(%s %s) \f0from \f3%s", ci->name, getclientipstr(ci->clientnum), getclienthostname(ci->clientnum), country);
				} else {
#endif
					irc.speak(1, "\00312Connected: \00306%s\00314", ci->name);
					echo("\f1Connected: \f0%s \f3(%s %s)", ci->name, getclientipstr(ci->clientnum), getclienthostname(ci->clientnum));
#ifdef HAVE_GEOIP
				}
#endif

				for(int i = 0; i < clients.length(); i++) {
					if(clients[i]->privilege == PRIV_ADMIN) {
						whisper(clients[i]->clientnum, "Incoming client \f3%s\f7 %s", getclientipstr(sender), getclienthostname(sender));
					}
				}

				if(webhook[0]) {
					char *ename = evhttp_encode_uri(ci->name);
					defformatstring(url)("%s?action=connect&name=%s&ip=%s", webhook, ename, getclientipstr(sender));
					free(ename);
					printf("accessing \"%s\"\n", url);
					froghttp_get(evbase, dnsbase, url, NULL, NULL);
				}
				
				if(mapdata && autosend) sendfile(sender, 2, mapdata, "ri", SV_SENDMAP);
			}
		}
		else if(chan==2)
		{
			receivefile(sender, p.buf, p.maxlen);
			return;
		}

		if(p.packet->flags&ENET_PACKET_FLAG_RELIABLE) reliablemessages = true;
		int curmsg;
#define QUEUE_AI clientinfo *cm = cq;
#define QUEUE_MSG { if(cm && (!cm->local || demorecord || hasnonlocalclients())) while(curmsg<p.length()) cm->messages.add(p.buf[curmsg++]); }
#define QUEUE_BUF(size, body) { \
	if(cm && (!cm->local || demorecord || hasnonlocalclients())) \
	{ \
		curmsg = p.length(); \
		ucharbuf buf = cm->messages.reserve(size); \
		{ body; } \
		cm->messages.addbuf(buf); \
	} \
}
#define QUEUE_INT(n) QUEUE_BUF(5, putint(buf, n))
#define QUEUE_UINT(n) QUEUE_BUF(4, putuint(buf, n))
#define QUEUE_STR(text) QUEUE_BUF(2*strlen(text)+1, sendstring(text, buf))
		while((curmsg = p.length()) < p.maxlen) switch(type = checktype(getint(p), ci))
		{
			case SV_POS:
			{
				int pcn = getint(p);
				clientinfo *cp = getinfo(pcn);
				if(cp && pcn != sender && cp->ownernum != sender) cp = NULL;
				vec pos;
				loopi(3) pos[i] = getuint(p)/DMF;
				getuint(p);
				loopi(5) getint(p);
				int physstate = getuint(p);
				if(physstate&0x20) loopi(2) getint(p);
				if(physstate&0x10) getint(p);
				getuint(p);
				if(cp)
				{
					if((!ci->local || demorecord || hasnonlocalclients()) && (cp->state.state==CS_ALIVE || cp->state.state==CS_EDITING))
					{
						cp->position.setsizenodelete(0);
						while(curmsg<p.length()) cp->position.add(p.buf[curmsg++]);
					}
					if(smode && cp->state.state==CS_ALIVE) smode->moved(cp, cp->state.o, cp->gameclip, pos, (physstate&0x80)!=0);
					cp->state.o = pos;
					cp->gameclip = (physstate&0x80)!=0;
				}
				break;
			}

			case SV_FROMAI:
			{
				int qcn = getint(p);
				if(qcn < 0) cq = ci;
				else
				{
					cq = getinfo(qcn);
					if(cq && qcn != sender && cq->ownernum != sender) cq = NULL;
				}
				break;
			}

			case SV_EDITMODE:
			{
				int val = getint(p);
				if(!ci->local && !m_edit) break;
				if(val ? ci->state.state!=CS_ALIVE && ci->state.state!=CS_DEAD : ci->state.state!=CS_EDITING) break;
				if(smode)
				{
					if(val) smode->leavegame(ci);
					else smode->entergame(ci);
				}
				if(val)
				{
					ci->state.editstate = ci->state.state;
					ci->state.state = CS_EDITING;
					ci->events.setsizenodelete(0);
					ci->state.rockets.reset();
					ci->state.grenades.reset();
				}
				else ci->state.state = ci->state.editstate;
				QUEUE_MSG;
				break;
			}

			case SV_MAPCRC:
			{
				getstring(text, p);
				int crc = getint(p);
				if(!ci) break;
				if(strcmp(text, smapname))
				{
					if(ci->clientmap[0])
					{
						ci->clientmap[0] = '\0';
						ci->mapcrc = 0;
					}
					else if(ci->mapcrc > 0) ci->mapcrc = 0;
					break;
				}
				copystring(ci->clientmap, text);
				ci->mapcrc = text[0] ? crc : 1;
				checkmaps();
				break;
			}

			case SV_CHECKMAPS:
				checkmaps(sender);
				break;

			case SV_TRYSPAWN:
				if(!ci || !cq || cq->state.state!=CS_DEAD || cq->state.lastspawn>=0 || (smode && !smode->canspawn(cq))) break;
				if(!ci->clientmap[0] && !ci->mapcrc)
				{
					ci->mapcrc = -1;
					checkmaps();
				}
				if(cq->state.lastdeath)
				{
					flushevents(cq, cq->state.lastdeath + DEATHMILLIS);
					cq->state.respawn();
				}
				cleartimedevents(cq);
				sendspawn(cq);
				break;

			case SV_GUNSELECT:
			{
				int gunselect = getint(p);
				if(!cq || cq->state.state!=CS_ALIVE) break;
				cq->state.gunselect = gunselect;
				QUEUE_AI;
				QUEUE_MSG;
				break;
			}

			case SV_SPAWN:
			{
				int ls = getint(p), gunselect = getint(p);
				if(!cq || (cq->state.state!=CS_ALIVE && cq->state.state!=CS_DEAD) || ls!=cq->state.lifesequence || cq->state.lastspawn<0) break;
				cq->state.lastspawn = -1;
				cq->state.state = CS_ALIVE;
				cq->state.gunselect = gunselect;
				if(smode) smode->spawned(cq);
				QUEUE_AI;
				QUEUE_BUF(100,
				{
					putint(buf, SV_SPAWN);
					sendstate(cq->state, buf);
				});
				break;
			}

			case SV_SUICIDE:
			{
				if(cq) cq->addevent(new suicideevent);
				break;
			}

			case SV_SHOOT:
			{
				shotevent *shot = new shotevent;
				shot->id = getint(p);
				shot->millis = cq ? cq->geteventmillis(gamemillis, shot->id) : 0;
				shot->gun = getint(p);
				loopk(3) shot->from[k] = getint(p)/DMF;
				loopk(3) shot->to[k] = getint(p)/DMF;
				int hits = getint(p);
				loopk(hits)
				{
					if(p.overread()) break;
					hitinfo &hit = shot->hits.add();
					hit.target = getint(p);
					hit.lifesequence = getint(p);
					hit.rays = getint(p);
					loopk(3) hit.dir[k] = getint(p)/DNF;
				}
				if(cq) cq->addevent(shot);
				else delete shot;
				break;
			}

			case SV_EXPLODE:
			{
				explodeevent *exp = new explodeevent;
				int cmillis = getint(p);
				exp->millis = cq ? cq->geteventmillis(gamemillis, cmillis) : 0;
				exp->gun = getint(p);
				exp->id = getint(p);
				int hits = getint(p);
				loopk(hits)
				{
					if(p.overread()) break;
					hitinfo &hit = exp->hits.add();
					hit.target = getint(p);
					hit.lifesequence = getint(p);
					hit.dist = getint(p)/DMF;
					loopk(3) hit.dir[k] = getint(p)/DNF;
				}
				if(cq) cq->addevent(exp);
				else delete exp;
				break;
			}

			case SV_ITEMPICKUP:
			{
				int n = getint(p);
				if(!cq) break;
				pickupevent *pickup = new pickupevent;
				pickup->ent = n;
				cq->addevent(pickup);
				break;
			}

			case SV_EDITF:              // coop editing messages
			case SV_EDITT:
			case SV_EDITM:
			case SV_FLIP:
			case SV_COPY:
			case SV_PASTE:
			case SV_ROTATE:
			case SV_REPLACE:
			case SV_DELCUBE:
			{
				int size = msgsizelookup(type);
				if(size>0) {
					selinfo sel;
					sel.o.x = getint(p); sel.o.y = getint(p); sel.o.z = getint(p);
					sel.s.x = getint(p); sel.s.y = getint(p); sel.s.z = getint(p);
					sel.grid = getint(p); sel.orient = getint(p);
					sel.cx = getint(p); sel.cxs = getint(p); sel.cy = getint(p), sel.cys = getint(p);
					sel.corner = getint(p);

					printf("Edit: %s\n", ci->name);

					if(sel.s.x * sel.grid >= maxselspam || sel.s.y * sel.grid >= maxselspam || sel.s.z * sel.grid >= maxselspam) {
						if(!ci->bigselwarned && totalmillis - ci->lastbigselspam > (int64_t) bigselmillis) {
							if(editspamwarn > 0) privilegemsg(editspamwarn == 1 ? PRIV_MASTER : 0, "\f3WARNING: Edit spam detected: %s (large selection size)!!!", colorname(ci));
							ci->bigselwarned = true;
						} else {
							ci->bigselwarned = false;
						}
						ci->lastbigselspam = totalmillis;
					}

					if(type == SV_EDITF && !(sel.cx & 1 || sel.cxs & 1 || sel.cy & 1 || sel.cys & 1)) { // odd values in c{xy}[s] means we're editing corners. that's not mapwrecking
						if(totalmillis - ci->lastscrollspam <= (int64_t)editscrollmillis) {
							ci->editspamsize[0].x = min(sel.o.x, ci->editspamsize[0].x);
							ci->editspamsize[0].y = min(sel.o.y, ci->editspamsize[0].y);
							ci->editspamsize[0].z = min(sel.o.z, ci->editspamsize[0].z);
							ci->editspamsize[1].x = max(sel.o.x + sel.s.x, ci->editspamsize[1].x);
							ci->editspamsize[1].y = max(sel.o.y + sel.s.y, ci->editspamsize[1].y);
							ci->editspamsize[1].z = max(sel.o.z + sel.s.z, ci->editspamsize[1].z);

							if(!ci->scrollspamwarned &&
								(ci->editspamsize[1].x - ci->editspamsize[0].x >= maxscrollspam ||
								ci->editspamsize[1].y - ci->editspamsize[0].y >= maxscrollspam ||
								ci->editspamsize[1].z - ci->editspamsize[0].z >= maxscrollspam)) {
								if(editspamwarn > 0) privilegemsg(editspamwarn == 1 ? PRIV_MASTER : 0, "\f3WARNING: Edit spam detected: %s (fast scrolling)!!!", colorname(ci));
								ci->scrollspamwarned = true;
							}
						} else {
							ci->editspamsize[0].x = ci->editspamsize[0].y = ci->editspamsize[0].z = INT_MAX;
							ci->editspamsize[1].x = ci->editspamsize[1].y = ci->editspamsize[1].z = 0;
							ci->scrollspamwarned = false;
						}
						ci->lastscrollspam = totalmillis;
					}

					if(type == SV_EDITT) {
						if(totalmillis - ci->lasttexturespam <= (int64_t)texturespammillis) {
							ci->texturespamtimes++;
							if(ci->texturespamtimes >= maxtexturespam) {
								if(!ci->texturespamwarned) {
									if(editspamwarn > 0) privilegemsg(editspamwarn == 1 ? PRIV_MASTER : 0, "\f3WARNING: Edit spam detected: %s (fast texture change. Please use F2 instead of Y+scroll for picking a texture)", colorname(ci));
									ci->texturespamwarned = true;
								}
							}
						} else {
							ci->texturespamtimes = 0;
							ci->texturespamwarned = false;
						}
						ci->lasttexturespam = totalmillis;
					}

					if(ci->recording) { //FIXME: remove the buf/sprintf and print to the file directly
						char buf[256];
						sprintf(buf, "%d  %d %d %d  %d %d %d  %d %d  %d %d %d %d  %d",
							type,
							sel.o.x - ci->playorigin.x, sel.o.y - ci->playorigin.y, sel.o.z - ci->playorigin.z,
							sel.s.x, sel.s.y, sel.s.z,
							sel.grid, sel.orient,
							sel.cx, sel.cxs, sel.cy, sel.cys,
							sel.corner);
						if(size > 14) loopi(size - 14) {
							char ibuf[10];
							sprintf(ibuf, " %d", getint(p));
							strcat(buf, ibuf);
						}
						strcat(buf, "\n");
						ci->recording->putstring(buf);
					} else {
						if(size > 14) loopi(size - 14) getint(p); //get the rest of the message, we don't care about the info
						if(!ci->playing) ci->playorigin = sel.o; // only set the last edit pos if not recording, and not playing
					}

					if(ci && cq && (ci != cq || ci->state.state!=CS_SPECTATOR)) { QUEUE_AI; QUEUE_MSG; }
				}

				break;
			}

			case SV_REMIP:
			{
				if(ci && cq && (ci != cq || ci->state.state!=CS_SPECTATOR)) {
					if(totalmillis - ci->lastremip < (int64_t)remipmillis) {
						sendf(sender, 1, "ris", SV_SERVMSG, "\f3Remipping too soon! \f2Blocked\f7.");
					} else {
						QUEUE_AI;
						QUEUE_MSG;
					}
					ci->lastremip = totalmillis;
				}
				break;
			}

			case SV_TEXT:
			{
				getstring(text, p);
				filtertext(text,text);

				if(totalmillis - ci->lasttext < (int64_t)spammillis) {
					ci->spamlines++;

					if(ci->spamlines >= maxspam) {
						if(!ci->spamwarned) sendf(sender, 1, "ris", SV_SERVMSG, "\f3Sending messages too fast! \f2Blocked\f7.");
						ci->spamwarned = true;
						break;
					}
				} else {
					ci->spamwarned = false;
					ci->spamlines = 0;
				}
				ci->lasttext = totalmillis;

				if(!frog_command(text, sender, ci)) {
					QUEUE_AI;
					QUEUE_INT(SV_TEXT);
					QUEUE_STR(text);
					irc.speak(1 , "\00306<%s> \00303%s", ci->name, text);
				}

				break;
			}

			case SV_SAYTEAM:
			{
				getstring(text, p);
				if(!ci || !cq || (ci->state.state==CS_SPECTATOR && !ci->local && !ci->privilege) || !m_teammode || !cq->team[0]) break;
				loopv(clients)
				{
					clientinfo *t = clients[i];
					if(t==cq || t->state.state==CS_SPECTATOR || t->state.aitype != AI_NONE || strcmp(cq->team, t->team)) continue;
					sendf(t->clientnum, 1, "riis", SV_SAYTEAM, cq->clientnum, text);
				}
				break;
			}

			case SV_SWITCHNAME:
			{
				QUEUE_MSG;
				getstring(text, p);
				irc.speak(1, "\00306%s\00314 is now known as \00306%s", ci->name, text);
				filtertext(ci->name, text, false, MAXNAMELEN);
				if(!ci->name[0]) copystring(ci->name, "unnamed");
				QUEUE_STR(ci->name);
				break;
			}

			case SV_SWITCHMODEL:
			{
				ci->playermodel = getint(p);
				QUEUE_MSG;
				break;
			}

			case SV_SWITCHTEAM:
			{
				getstring(text, p);
				filtertext(text, text, false, MAXTEAMLEN);
				if(strcmp(ci->team, text))
				{
					if(m_teammode && smode && !smode->canchangeteam(ci, ci->team, text))
						sendf(sender, 1, "riis", SV_SETTEAM, sender, ci->team);
					else
					{
						if(smode && ci->state.state==CS_ALIVE) smode->changeteam(ci, ci->team, text);
						copystring(ci->team, text);
						aiman::changeteam(ci);
						sendf(-1, 1, "riis", SV_SETTEAM, sender, ci->team);
					}
				}
				break;
			}

			case SV_MAPVOTE:
			case SV_MAPCHANGE:
			{
				getstring(text, p);
				filtertext(text, text, false);
				int reqmode = getint(p);
				if(type!=SV_MAPVOTE && !mapreload) break;
				vote(text, reqmode, sender);
				break;
			}

			case SV_ITEMLIST:
			{
				if((ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) || !notgotitems || strcmp(ci->clientmap, smapname)) { while(getint(p)>=0 && !p.overread()) getint(p); break; }
				int n;
				while((n = getint(p))>=0 && n<MAXENTS && !p.overread())
				{
					server_entity se = { NOTUSED, 0, false };
					while(sents.length()<=n) sents.add(se);
					sents[n].type = getint(p);
					if(canspawnitem(sents[n].type))
					{
						if(m_mp(gamemode) && (sents[n].type==I_QUAD || sents[n].type==I_BOOST)) sents[n].spawntime = spawntime(sents[n].type);
						else sents[n].spawned = true;
					}
				}
				notgotitems = false;
				break;
			}

			case SV_EDITENT:
			{
				int i = getint(p);
				loopk(3) getint(p);
				int type = getint(p);
				
				getint(p); // rotation
				int attr2 = getint(p); // model (for mapmodels)
				loopk(3) getint(p);
				
				if(!ci || ci->state.state==CS_SPECTATOR) break;
				QUEUE_MSG;
				bool canspawn = canspawnitem(type);
				if(i<MAXENTS && (sents.inrange(i) || canspawnitem(type)))
				{
					server_entity se = { NOTUSED, 0, false };
					while(sents.length()<=i) sents.add(se);
					sents[i].type = type;
					if(canspawn ? !sents[i].spawned : sents[i].spawned)
					{
						sents[i].spawntime = canspawn ? 1 : 0;
						sents[i].spawned = false;
					}
				}
				
				//detect fast mapmodel change
				if(type == ET_MAPMODEL) {
					if(attr2 != ci->lastmapmodeltype) {
						if(totalmillis - ci->lastmapmodelchange < (int64_t)mapmodelspammillis) {
							ci->mapmodelspamtimes++;
							if(ci->mapmodelspamtimes >= maxmapmodelspam) {
								if(!ci->mapmodelspamwarned)
									if(editspamwarn > 0) privilegemsg(editspamwarn == 1 ? PRIV_MASTER : 0, "\f3WARNING: Edit spam detected: %s (fast mapmodel change)!!!", colorname(ci));
								ci->mapmodelspamwarned = true;
							}
						} else {
							ci->mapmodelspamtimes = 0;
							ci->mapmodelspamwarned = false;
						}
						ci->lastmapmodelchange = totalmillis;
					}
					ci->lastmapmodeltype = attr2;
				}
				break;
			}

			case SV_EDITVAR:
			{
				int type = getint(p);
				getstring(text, p);
				switch(type)
				{
					case ID_VAR: getint(p); break;
					case ID_FVAR: getfloat(p); break;
					case ID_SVAR: getstring(text, p);
				}
				if(ci && ci->state.state!=CS_SPECTATOR) QUEUE_MSG;
				break;
			}

			case SV_PING:
				sendf(sender, 1, "i2", SV_PONG, getint(p));
				break;

			case SV_CLIENTPING:
			{
				int ping = getint(p);
				if(ci)
				{
					ci->ping = ping;
					loopv(ci->bots) ci->bots[i]->ping = ping;
				}
				QUEUE_MSG;
				break;
			}

			case SV_MASTERMODE:
			{
				int mm = getint(p);
				if(ci->privilege == PRIV_MASTER && clients.length() - bots.length() == 1 && mm == MM_PRIVATE)
					whisper(sender, "\f3WARNING\f7: Cannot set mastermode 3 (private) with only one player.\nPlease work offline if you want privacy (type /disconnect).");
				else if(ci->privilege || ci->local) {
					if((ci->privilege>=PRIV_ADMIN || ci->local) || (mastermask&(1<<mm))) setmastermode(mm);
					else whisper(sender, "Mastermode %d is disabled on this server.", mm);
				}
				break;
			}

			case SV_CLEARBANS:
			{
				if(ci->privilege || ci->local) clearbans();
				break;
			}

			case SV_KICK:
			{
				int victim = getint(p);
				if((ci->privilege || ci->local) && getclientinfo(victim)) // no bots
				{
					if(ci->privilege < PRIV_ADMIN && totalmillis - ci->lastkick < (int64_t)kickmillis) whisper(sender, "Mass kick detected. Kick denied.");
					else if(ci->clientnum == victim) { //seems to never be reached (probably client checks for this too)
						sendf(sender, 1, "ris", SV_SERVMSG, "Cannot kick/ban yourself!");
					} else {
						//search the whitelist for this ip
						bool w = is_whitelisted(victim);

						if(!w || ci->privilege == PRIV_ADMIN) {
							addban(victim);
							kick(victim);
							if(w) {
								sendf(sender, 1, "ris", SV_SERVMSG, "\f3Warning\f7: kicking whitelisted player");
							}
						} else {
							sendf(sender, 1, "ris", SV_SERVMSG, "Cannot kick/ban whitelisted player.");
							sendf(victim, 1, "ris", SV_SERVMSG, "\f3Failed kick/ban attempt from master (whitelist).\f7");
						}
					}
					ci->lastkick = totalmillis;
				}
				break;
			}

			case SV_SPECTATOR:
			{
				int specn = getint(p), val = getint(p);
				if(!ci->privilege && !ci->local && (specn !=sender || (ci->state.state==CS_SPECTATOR && mastermode>=MM_LOCKED))) break;
				spectator(val, specn);
				break;
			}

			case SV_SETTEAM:
			{
				int who = getint(p);
				getstring(text, p);
				filtertext(text, text, false, MAXTEAMLEN);
				if(!ci->privilege && !ci->local) break;
				clientinfo *wi = getinfo(who);
				if(!wi || !strcmp(wi->team, text)) break;
				if(!smode || smode->canchangeteam(wi, wi->team, text))
				{
					if(smode && wi->state.state==CS_ALIVE)
						smode->changeteam(wi, wi->team, text);
					copystring(wi->team, text, MAXTEAMLEN+1);
				}
				aiman::changeteam(wi);
				sendf(-1, 1, "riis", SV_SETTEAM, who, wi->team);
				break;
			}

			case SV_FORCEINTERMISSION:
				if(ci->local && !hasnonlocalclients()) startintermission();
				break;

			case SV_RECORDDEMO:
			{
				int val = getint(p);
				if(ci->privilege<PRIV_ADMIN && !ci->local) break;
				demonextmatch = val!=0;
				message("Demo recording is %s for next match", demonextmatch ? "enabled" : "disabled");
				break;
			}

			case SV_STOPDEMO:
			{
				if(ci->privilege<PRIV_ADMIN && !ci->local) break;
				stopdemo();
				break;
			}

			case SV_CLEARDEMOS:
			{
				int demo = getint(p);
				if(ci->privilege<PRIV_ADMIN && !ci->local) break;
				cleardemos(demo);
				break;
			}

			case SV_LISTDEMOS:
				if(!ci->privilege && !ci->local && ci->state.state==CS_SPECTATOR) break;
				listdemos(sender);
				break;

			case SV_GETDEMO:
			{
				int n = getint(p);
				if(!ci->privilege  && !ci->local && ci->state.state==CS_SPECTATOR) break;
				senddemo(sender, n);
				break;
			}

			case SV_GETMAP:
				if(mapdata)
				{
					message("\f1Sending map to \f2%s\f1...", ci->name);
					sendfile(sender, 2, mapdata, "ri", SV_SENDMAP);
				}
				else sendf(sender, 1, "ris", SV_SERVMSG, "No map to send");
				break;

			case SV_NEWMAP:
			{
				int size = getint(p);
				if(totalmillis - ci->lastnewmap < (int64_t)newmapmillis) {
					sendf(sender, 1, "ris", SV_SERVMSG, "\f3Newmapping too soon! \f2Blocked.");
				} else {
					if(!ci->privilege && !ci->local && ci->state.state==CS_SPECTATOR) break;
					if(size>=0)
					{
						smapname[0] = '\0';
						resetitems();
						notgotitems = false;
						if(smode) smode->reset(true);
						QUEUE_MSG;
						irc.speak(2, "\00306%s\00312 started a new map of size \00303%d", colorname(ci, NULL, false), min(16, max(10, size)));
						ci->lastnewmap = totalmillis;
					}
				}
				break;
			}

			case SV_SETMASTER:
			{
				int val = getint(p);
				getstring(text, p);
				setmaster(ci, val!=0, text);
				// don't broadcast the master password
				break;
			}

			case SV_ADDBOT:
				aiman::reqadd(ci, getint(p));
				break;

			case SV_DELBOT:
				aiman::reqdel(ci);
				break;

			case SV_BOTLIMIT:
			{
				int limit = getint(p);
				if(ci) aiman::setbotlimit(ci, limit);
				break;
			}

			case SV_BOTBALANCE:
			{
				int balance = getint(p);
				if(ci) aiman::setbotbalance(ci, balance!=0);
				break;
			}

			case SV_AUTHTRY:
			{
				string desc, name;
				getstring(desc, p, sizeof(desc)); // unused for now
				getstring(name, p, sizeof(name));
				if(!desc[0]) tryauth(ci, name);
				break;
			}

			case SV_AUTHANS:
			{
				string desc, ans;
				getstring(desc, p, sizeof(desc)); // unused for now
				uint id = (uint)getint(p);
				getstring(ans, p, sizeof(ans));
				if(!desc[0]) answerchallenge(ci, id, ans);
				break;
			}

			case SV_PAUSEGAME:
			{
				int val = getint(p);
				if(ci->privilege<PRIV_ADMIN && !ci->local) break;
				printf("pausing...\n");
				pausegame(val > 0);
				break;
			}

			#define PARSEMESSAGES 1
			#include "capture.h"
			#include "ctf.h"
			#undef PARSEMESSAGES

			default:
			{
				int size = server::msgsizelookup(type);
				if(size==-1) { disconnect_client(sender, DISC_TAGT); return; }
				if(size>0) loopi(size-1) getint(p);
				if(ci && cq && (ci != cq || ci->state.state!=CS_SPECTATOR)) { QUEUE_AI; QUEUE_MSG; }
				break;
			}
		}
	}

	int laninfoport() { return SAUERBRATEN_LANINFO_PORT; }
	int serverinfoport(int servport) { return servport < 0 ? SAUERBRATEN_SERVINFO_PORT : servport+1; }
	int serverport(int infoport) { return infoport < 0 ? SAUERBRATEN_SERVER_PORT : infoport-1; }
	const char *defaultmaster() { return "sauerbraten.org"; }
	int masterport() { return SAUERBRATEN_MASTER_PORT; }

	#include "extinfo.h"

	void serverinforeply(ucharbuf &req, ucharbuf &p)
	{
		if(!getint(req))
		{
			extserverinforeply(req, p);
			return;
		}

		putint(p, numclients(-1, false, true));
		putint(p, 5);                   // number of attrs following
		putint(p, PROTOCOL_VERSION);    // a // generic attributes, passed back below
		putint(p, gamemode);            // b
		putint(p, minremain);           // c
		putint(p, maxclients);
		putint(p, serverpass[0] ? MM_PASSWORD : (!m_mp(gamemode) ? MM_PRIVATE : (mastermode || mastermask&MM_AUTOAPPROVE ? mastermode : MM_AUTH)));
		sendstring(smapname, p);
		sendstring(serverdesc, p);
		sendserverinforeply(p);
	}

	bool servercompatible(char *name, char *sdec, char *map, int ping, const vector<int> &attr, int np)
	{
		return attr.length() && attr[0]==PROTOCOL_VERSION;
	}

	#include "aiman.h"
}

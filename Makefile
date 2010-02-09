DEBUG=true

FROGMOD_VERSION=$(shell git log --abbrev-commit --pretty=format:%h -1)

programs=frogserv
eventdir=libevent2
enetdir=enet

frogserv_SRCS=color.cpp command.cpp crypto.cpp gameserver.cpp geom.cpp masterserver.cpp server.cpp stream.cpp tools.cpp evirc.cpp sha1.cpp json.cpp
frogserv_EXTRA_DEPS=$(enetdir)/libenet.a $(eventdir)/.libs/libevent.a
frogserv_CXXFLAGS=-Wall -fomit-frame-pointer -fsigned-char -Ienet/include -I$(eventdir)/include -I$(eventdir) -DFROGMOD_VERSION=\"$(FROGMOD_VERSION)\"
frogserv_LDFLAGS=$(enetdir)/libenet.a $(eventdir)/.libs/libevent.a
frogserv_LIBS=z resolv
extra=config.h config.mk

ifeq ($(DEBUG),true)
frogserv_CXXFLAGS+=-g
frogserv_LDFLAGS+=-g
else
frogserv_CXXFLAGS+=-O3
endif

-include config.mk

include common.mk

# extra stuff goes below "include common.mk" (to avoid being taken as the default/first target)

config.h:
config.mk:
	@if ! ./config.sh; then exit 1; fi

enet/libenet.a: enet/Makefile
	@echo "$(COMPILING)Building enet$(RESETC)"
	@cd enet && $(MAKE)

enet/Makefile:
	@echo "$(COMPILING)Configuring enet$(RESETC)"
	@cd enet && ./configure

$(eventdir)/.libs/libevent.a: $(eventdir)/Makefile
	@echo "$(COMPILING)Building libevent$(RESETC)"
	@cd $(eventdir) && $(MAKE)

$(eventdir)/Makefile: $(eventdir)/configure
	@echo "$(COMPILING)Configuring libevent$(RESETC)"
	@cd $(eventdir) && ./configure

$(eventdir)/configure:
	@cd $(eventdir) && ./autogen.sh

.PHONY: eclean
eclean: clean
	@if [ -f enet/Makefile ]; then cd enet && $(MAKE) distclean; fi
	@if [ -f $(eventdir)/Makefile ]; then cd $(eventdir) && $(MAKE) distclean; fi

.PHONY: distclean
distclean: eclean
	@rm -f config.mk config.h

CFLAGS = -g -O2 -Wall -Wextra
LDFLAGS =
LDLIBS = -lX11 -lXext -lXrender

prefix = /usr/local
bindir = $(prefix)/bin

CC = cc
RM = rm -f
INSTALL = install

all: xwinwrap

install: xwinwrap
	$(INSTALL) -d -m 755 '$(DESTDIR)$(bindir)'
	$(INSTALL) xwinwrap '$(DESTDIR)$(bindir)'

clean:
	$(RM) xwinwrap

.PHONY: all install clean

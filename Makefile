#all:
#	cc -g  -o viamillipede -lpthread  plumbing.c rx.c tx.c viamillipede.c
#clean: 
#	rm -f viamillipede viamillipede.core
#
#test: clean all
#	sh  test.sh

#.include <src.opts.mk>
# XXX I'm not really suppoesd to DESTDIR but ... ???
#DESTDIR= /usr/local/
BINDIR= /usr/local/bin
MANDIR= /usr/local/man/man
MK_DEBUG_FILES= no
SHAREDIR= ""
#CFLAGS += -Dvmpd_strict

PROG= viamillipede
SRCS= plumbing.c tx.c rx.c viamillipede.c
# This LDADD knob can't possibly be intended to use this way
LDADD= -lpthread
.include <bsd.prog.mk>

# madasuk version
VERSION = "0.2"

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/man

INCS = -I. -I/usr/include
LIBS = `pkg-config --libs gtk+-2.0` -lX11 -lXext

CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = -g -std=c99 -pedantic -Wall -Os -D_REENTRANT ${INCS} ${CPPFLAGS} `pkg-config --cflags gtk+-2.0`
LDFLAGS = ${LIBS}

# compiler and linker
CC = cc

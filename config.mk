# dwm version
VERSION = `{ test -d .git && which git >/dev/null 2>&1; } && git describe --always | sed 's|-|.|g; s|v||' || echo 6.1+`
YEAR = `{ test -d .git && which git >/dev/null 2>&1; } && { git log -n 1 --date=iso --pretty=format:%ad | cut -c 1-4; } || echo 2012+`

# Customize below to fit your system

# paths
PREFIX = /usr/local

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# includes and libs
INCS = -I${X11INC}
LIBS = -L${X11LIB} -lX11 -lXinerama -lXfixes

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=2 -DVERSION=\"${VERSION}\" -DYEAR=\"${YEAR}\"
CFLAGS   = -std=c99 -pedantic -Wall -Wno-deprecated-declarations -O2 ${INCS} ${CPPFLAGS}
LDFLAGS  = -s ${LIBS}

# compiler and linker
CC = cc

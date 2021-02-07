APPNAME  = pulsecron

CC       = clang

PKGS     = libpulse lua5.1 libxdg-basedir libsystemd

INCS    := $(shell pkg-config --cflags $(PKGS)) -I./
CFLAGS  := -std=gnu18 -ggdb -W -Wall -Wextra -pedantic -O2 $(INCS) $(CFLAGS)

LIBS    := $(shell pkg-config --libs $(PKGS))
LDFLAGS := $(LIBS) $(LDFLAGS) -Wl,--export-dynamic -lpthread

SRCS  = $(wildcard *.c)
HEADS = $(wildcard *.h)
OBJS  = $(foreach obj,$(SRCS:.c=.o),$(obj))

pulsecron: $(OBJS)
	@echo $(CC) -o $@ $(OBJS)
	@$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): $(HEADS)

.c.o:
	@echo $(CC) -c $< -o $@
	@$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

clean:
	rm -f pulsecron $(OBJS)

all: pulsecron

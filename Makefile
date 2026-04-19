prefix      ?= /usr/local
exec_prefix ?= $(prefix)
sbindir     ?= $(exec_prefix)/sbin
DESTDIR     ?=

CC          ?= cc
CFLAGS      ?= -O2 -Wall
LDFLAGS     ?=

TARGET      = minit
INSTALL     = install
INSTALL_PROGRAM = $(INSTALL) -m 755

all: $(TARGET)

$(TARGET): minit.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ minit.c

install: $(TARGET)
	$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(sbindir)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(sbindir)/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
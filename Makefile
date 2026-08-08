CC = gcc
CFLAGS = -Wall -Wextra -O2 -g `pkg-config --cflags gtk+-3.0 ayatana-appindicator3-0.1`
LDFLAGS = `pkg-config --libs gtk+-3.0 ayatana-appindicator3-0.1`

SRCDIR = src
OBJDIR = obj
BINDIR = .

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TARGET = $(BINDIR)/traymd

PREFIX ?= /usr/local
DESTDIR ?=

bindir ?= $(PREFIX)/bin
datadir ?= $(PREFIX)/share
applicationsdir ?= $(datadir)/applications

.PHONY: all clean install uninstall test

all: $(TARGET)

$(OBJDIR)/test-tasks: tests/test_tasks.c $(SRCDIR)/editor.c $(SRCDIR)/markdown.c $(SRCDIR)/code_highlight.c $(SRCDIR)/config.c $(SRCDIR)/window.c $(wildcard $(SRCDIR)/*.h) | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) $(filter %.c,$^) -o $@ $(LDFLAGS) -lm

$(OBJDIR)/test-config: tests/test_config.c $(SRCDIR)/config.c $(SRCDIR)/config.h | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) $(filter %.c,$^) -o $@ `pkg-config --libs glib-2.0`

test: $(OBJDIR)/test-config $(OBJDIR)/test-tasks
	$(OBJDIR)/test-config
	$(OBJDIR)/test-tasks

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(bindir)/traymd
	install -Dm644 assets/traymd.desktop $(DESTDIR)$(applicationsdir)/traymd.desktop

uninstall:
	rm -f $(DESTDIR)$(bindir)/traymd
	rm -f $(DESTDIR)$(applicationsdir)/traymd.desktop

# Header dependencies
$(OBJDIR)/main.o: $(SRCDIR)/app.h $(SRCDIR)/tray.h $(SRCDIR)/window.h
$(OBJDIR)/app.o: $(SRCDIR)/app.h $(SRCDIR)/config.h $(SRCDIR)/notes.h $(SRCDIR)/window.h $(SRCDIR)/editor.h
$(OBJDIR)/window.o: $(SRCDIR)/window.h $(SRCDIR)/app.h $(SRCDIR)/editor.h $(SRCDIR)/config.h
$(OBJDIR)/editor.o: $(SRCDIR)/editor.h $(SRCDIR)/markdown.h $(SRCDIR)/app.h $(SRCDIR)/config.h $(SRCDIR)/undo.h
$(OBJDIR)/markdown.o: $(SRCDIR)/markdown.h $(SRCDIR)/code_highlight.h $(SRCDIR)/config.h
$(OBJDIR)/code_highlight.o: $(SRCDIR)/code_highlight.h
$(OBJDIR)/notes.o: $(SRCDIR)/notes.h
$(OBJDIR)/undo.o: $(SRCDIR)/undo.h
$(OBJDIR)/tray.o: $(SRCDIR)/tray.h $(SRCDIR)/app.h $(SRCDIR)/window.h $(SRCDIR)/config.h
$(OBJDIR)/config.o: $(SRCDIR)/config.h


GTK_PC = gtk+-3.0
GTK_CFLAGS = $(shell pkg-config $(GTK_PC) --cflags)
GTK_LIBS = $(shell pkg-config $(GTK_PC) --libs)

CFLAGS = -g
CFLAGS += $(GTK_CFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS)-c $< -o $@

gapk: gapk.o
	$(CC) -o $@ $< $(GTK_LIBS)

clean:
	rm gapk *.o

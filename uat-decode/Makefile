CFLAGS = -O2

SOURCES = decode.c

scope: *$(SOURCES)
	gcc $(CFLAGS) -o decode $(SOURCES) -lfec -lm

clean:
	rm decode
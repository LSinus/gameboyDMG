CFLAGS= -Wall -g -I/opt/homebrew/include/ -D_THREAD_SAFE

LIBS = -L/opt/homebrew/lib -lSDL2

all: 
	$(CC) $(CFLAGS) gameboy.c -o gameboy $(LIBS)
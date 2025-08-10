CFLAGS= -Wall -I/opt/homebrew/include/ -D_THREAD_SAFE
CFLAGS_DEBUG= -Wall -g -I/opt/homebrew/include/ -D_THREAD_SAFE -DDEBUG_TEST_LOG

LIBS = -L/opt/homebrew/lib -lSDL2

all: 
	$(CC) $(CFLAGS_DEBUG) gameboy.c -o gameboy $(LIBS)

release: 
	$(CC) $(CFLAGS) gameboy.c -o gameboy $(LIBS) -O3

test: 
	$(CC) $(CFLAGS_DEBUG) gameboy.c -o gameboy $(LIBS)
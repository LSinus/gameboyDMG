GLFLAG=-framework OpenGL

CFLAGS= -Wall -I/opt/homebrew/include/ -D_THREAD_SAFE 
CFLAGS_DEBUG= -Wall -g -I/opt/homebrew/include/ -D_THREAD_SAFE -DDEBUG_TEST_LOG 

LIBS = -L/opt/homebrew/lib -lSDL2 -lSDL2_ttf $(GLFLAG)

CFILES = src/gui/microui.c \
         src/gui/renderer.c \
         src/gui/SDL_FontCache.c \
         src/hardware/cpu.c \
         src/hardware/memory.c \
         src/hardware/ppu.c \
         src/hardware/timer.c \
         src/hardware/joypad.c \
         src/gameboy.c
all: 
	$(CC) $(CFLAGS_DEBUG) $(CFILES) -o gameboy $(LIBS)

release: 
	$(CC) $(CFLAGS) $(CFILES) -o gameboy $(LIBS) -O3

test: 
	$(CC) $(CFLAGS_DEBUG) $(CFILES) -o gameboy $(LIBS)
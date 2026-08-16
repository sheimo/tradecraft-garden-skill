CC_X64 = x86_64-w64-mingw32-gcc
CC_X86 = i686-w64-mingw32-gcc

CFLAGS = -O1 -fno-jump-tables -shared -Wall -Wno-pointer-arith

all: bin/loader.x64.o bin/loader.x86.o

bin/loader.x64.o: src/loader.c
	mkdir -p bin
	$(CC_X64) $(CFLAGS) -DWIN_X64 -c src/loader.c -o $@

bin/loader.x86.o: src/loader.c
	mkdir -p bin
	$(CC_X86) $(CFLAGS) -DWIN_X86 -c src/loader.c -o $@

pic:
	$(MAKE) all
	cpl build loader.spec x64 bin/loader.x64.pic
	cpl build loader.spec x86 bin/loader.x86.pic

clean:
	rm -rf bin/

#!/usr/bin/env make -f

#CC = x86_64-w64-mingw32-gcc
export CC
export LIBS_LINK_FLAGS

core = src/beryl.o src/lexer.o src/libs/core_lib.o
opt_libs = src/libs/io_lib.o src/io.o src/libs/unix_lib.o src/libs/debug_lib.o

export mexternal_libs = libs/math

CFLAGS += -std=c99 -Wall -Wextra -Wpedantic

release: CFLAGS += -O2
release: beryl

debug: CFLAGS += -g -fsanitize=address,undefined,leak -DDEBUG
debug: beryl

valgrind: CFLAGS += -g -DDEBUG
valgrind: beryl

test: debug
	./test.sh
run: debug
	./beryl

export dyn_libs = libs/math

.PHONY: $(dyn_libs)
$(dyn_libs):
	$(MAKE) -C $@
	cp -t env/libs $@/*.beryldl $@/*.beryl

dynamic-libraries: $(dyn_libs)

beryl: src/main.o $(core) $(opt_libs)
	$(CC) src/main.o $(core) $(opt_libs) -rdynamic -oberyl $(CFLAGS)

windows: src/main.o $(core) $(opt_libs) 
	$(CC) -shared $(core) $(opt_libs) -oberyl.dll $(CFLAGS)
	$(CC) src/main.o beryl.dll $(CFLAGS) -oberyl.exe
	$(MAKE) -f libs/MakeWinDLLs
	#./berylscript.exe ./make_docs.beryl

lib: $(core) $(opt_libs)
	ar rcs libBeryl.ar $(core) $(opt_libs) 

minimal-lib: $(core)
	ar rcs libBeryl.ar $(core)

install: release dynamic-libraries
	./make_docs.awk src/libs/*.c
	./install_local.sh

install-global: release dynamic-libraries
	./make_docs.awk src/libs/*.c
	./install_global.sh

install-headers:
	cp src/beryl.h /usr/local/include/beryl.h

clean:
	rm -f $(core) $(opt_libs) src/main.o
	rm -f ./beryl
	rm -f ./beryl.exe
	rm -f ./beryl.dll
	rm -f ./*.js
	rm -f ./*.html
	rm -f ./*.wasm
	rm env/docs/*.bdoc
	rm env/libs/*.beryl env/libs/*.beryldl

$(core) $(opt_libs) src/main.o:

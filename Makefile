#!/usr/bin/env make -f

#CC = x86_64-w64-mingw32-gcc
export CC
export LIBS_LINK_FLAGS

core = src/beryl.o src/lexer.o src/libs/core_lib.o
opt_libs = src/libs/io_lib.o src/io.o src/libs/unix_lib.o src/libs/debug_lib.o

export mexternal_libs = libs/math

BERYL_MAJOR_VERSION = 0
BERYL_SUBMAJOR_VERSION = 0
BERYL_MINOR_VERSION = 9
export BERYL_MAJOR_VERSION
export BERYL_SUBMAJOR_VERSION
export BERYL_MINOR_VERSION

CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -D BERYL_MAJOR_VERSION=$(BERYL_MAJOR_VERSION) -D BERYL_SUBMAJOR_VERSION=$(BERYL_SUBMAJOR_VERSION) -D BERYL_MINOR_VERSION=$(BERYL_MINOR_VERSION)

release: CFLAGS += -O2
release: beryl dynamic-libraries 
release:
	./make_docs.awk src/libs/*.c	

deb: release
	make -C packaging/deb

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
	ar rcs libberyl.a $(core) $(opt_libs) 

minimal-lib: $(core)
	ar rcs libberyl.a $(core)

install: release
	./install_local.sh

install-global: release
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

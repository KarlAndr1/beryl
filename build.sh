#!/usr/bin/env sh

if [ "$1" = debug ] || [ "$1" = d ]; then
	cc -DDEBUG src/*.c src/libs/*.c -fsanitize=address,undefined,leak -g -rdynamic -std=c99 -beryl
elif [ "$1" = run ] || [ "$1" = r ]; then
	cc -DDEBUG src/*.c src/libs/*.c -fsanitize=address,undefined,leak -g -rdynamic -oa.out -std=c99 -beryl
	exec ./a.out
elif [ "$1" = library ] || [ "$1" = lib ] || [ "$1" = l ]; then
	cc src/berylscript.c src/lexer.c src/libs/*.c -O2 -c -rdynamic
	ar rcs libBeryl.ar ./*.o # rcs means: 'r' insert into archive, 'c' create archive if it does not exist, 's' add/update the archive index
elif [ "$1" = clean ] || [ "$1" = c ]; then
	rm ./*.o
	rm libBeryl.ar
else
	cc src/*.c src/libs/*.c -O2 -oberyl -rdynamic
	./build_libs.sh
	./make_docs.awk src/libs/*.c
fi

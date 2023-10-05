#!/usr/bin/env sh

if [ "$1" = valgrind ] ; then
	for file in test_scripts/*; do
		echo "Test: $file"
		valgrind -q --leak-check=full --show-leak-kinds=all ./beryl $file
	done
else
	for file in test_scripts/*; do
		echo "Test: $file"
		./beryl $file
	done
fi

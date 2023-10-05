#!/usr/bin/env sh

for dir in libs/*; do
	if [ -d $dir ]; then
		make -C $dir
		cp -t env/libs $dir/*.beryldl $dir/*.beryl
	fi
done

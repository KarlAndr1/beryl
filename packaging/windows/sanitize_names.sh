#!/usr/bin/env sh

for file in env/docs/*.bdoc; do
	new_name="$(echo $file | sed 's/[\+\-\*\?]//g')"
	if [ "$new_name" != "$file" ]; then
		echo "Renamed '$file' to '$new_name'"
		mv "$file" "$new_name"
	fi
done

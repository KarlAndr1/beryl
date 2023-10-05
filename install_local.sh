#!/usr/bin/env sh

error() {
	echo "$0: Error: $1"
	exit 0
}

if [ ! -f ./beryl ]; then
	error 'Project has not been built.' 
fi

if [ -d ~/bin ]; then
	cp ./beryl ~/bin/beryl
elif [ -d ~/.local/bin ]; then
	cp ./beryl ~/.local/bin/beryl
else
	echo 'Neither ~/bin or ~/.local/bin are available. Do you wish to install to /usr/local/bin (requires root)? (y/n)'
	read choice
	if [ $choice == y || $choice == Y || $choice == yes || $choice == Yes ]; then
		sudo cp ./beryl /usr/local/bin/beryl
	else
		echo 'Aborting installation'
		exit 0
	fi
fi

if [ ! -d ~/.berylscript ]; then # Make the ~/.berylscript dir if it doesn't exist, and set the env variables that point to it
	mkdir ~/.berylscript
	mkdir ~/.berylscript/libs
	mkdir ~/.berylscript/docs
	
	printf '\n# Added by berylscript installer \n' >> ~/.profile
	printf 'export BERYL_SCRIPT_HOME="$HOME/.berylscript"\n' >> ~/.profile
	printf 'export BERYL_SCRIPT_INIT="$HOME/.berylscript/init.beryl"\n' >> ~/.profile
fi

cp ./env/init.beryl ~/.berylscript/init.beryl

cp -r -t ~/.berylscript/libs ./env/libs/* # Copy all the compiled/exported libraries in the env/libs/ directory to the new .berylscript/libs directory

cp -t ~/.berylscript/docs ./env/docs/* # Copy all the documentation to env/docs

echo "$0: Installation done."

#!/usr/bin/env sh

# NOTE: Needs to run as root/superuser!

error() {
	echo "$0: Error: $1"
	exit -1
}


if [ ! -f beryl ]; then
	error "Project has not been built."
fi

if [ -f /usr/local/bin/beryl ]; then
	echo "BerylScript is already installed. Do you wish to uninstall the old verion (Duplicate libraries may be overwritten)? Type 'YES' if so."
	read choice
	if [ $choice != YES ]; then
		echo "$0: Aborting installation"
		exit 0
	fi
fi

if [ ! -d /usr/local/berylscript ]; then
	mkdir /usr/local/berylscript /usr/local/berylscript/libs /usr/local/berylscript/docs
fi

cp ./beryl /usr/local/bin/beryl
cp ./env/init.beryl /usr/local/berylscript/init.beryl

cp -r -t /usr/local/berylscript/libs ./env/libs/*
cp -t /usr/local/berylscript/docs ./env/docs/*

printf '# Sets up environment variables used by berylscript\n' > /etc/profile.d/berylscript.sh
printf 'export BERYL_SCRIPT_HOME=/usr/local/berylscript\n' >> /etc/profile.d/berylscript.sh
printf 'export BERYL_SCRIPT_INIT=/usr/local/berylscript/init.beryl\n' >> /etc/profile.d/berylscript.sh

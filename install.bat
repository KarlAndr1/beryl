
mkdir "C:\Program Files\beryl"
copy ./beryl.dll "C:\Program Files\beryl.dll"
copy ./beryl.exe "C:\Program Files\beryl.exe"
copy ./env/init.beryl "C:\Program Files\beryl\init.beryl"

mkdir "C:\Program Files\beryl\libs"
xcopy /s ./env/libs "C:\Program Files\beryl\libs"


mkdir "C:\Program Files\beryl\docs
xcopy /s ./env/docs "C:\Program Files\beryl\docs"

IF %WINEHOMEDIR% == "" (
	echo "Setting environment variable(s) for all users..."
	setx /M BERYL_SCRIPT_HOME "C:\Program Files\beryl"
	setx /M BERYL_SCRIPT_INIT "C:\Program Files\beryl\init.beryl"
)

echo "Done."

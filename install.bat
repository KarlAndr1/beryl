
mkdir "C:\Program Files\BerylScript"
copy ./beryl.dll "C:\Program Files\berylscript.dll"
copy ./beryl.exe "C:\Program Files\berylscript.exe"
copy ./env/init.beryl "C:\Program Files\BerylScript\init.beryl"

mkdir "C:\Program Files\BerylScript\libs"
xcopy /s ./env/libs "C:\Program Files\BerylScript\libs"

rem This needs to be done *after* installing berylscript since berylscript is used to generate the docs in this case
beryl make_docs.beryl src/libs/core_lib.c src/libs/io_lib.c src/libs/unix_lib.c

rem Some files can't be copied since they contain characters like ?
mkdir "C:\Program Files\BerylScript\docs
xcopy /s ./env/docs "C:\Program Files\BerylScript\docs"

IF %WINEHOMEDIR% == "" (
	echo "Setting environment variable(s) for all users..."
	setx /M BERYL_SCRIPT_HOME "C:\Program Files\BerylScript"
	setx /M BERYL_SCRIPT_INIT "C:\Program Files\BerylScript\init.beryl"
)

echo "Done."

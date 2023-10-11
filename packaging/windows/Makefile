
zip:
	zip -j beryl-lang-windows.zip ../../beryl.dll ../../beryl.dll ../../libberyl.a ../../src/beryl.h ../../install.bat
	
	rm -rf env
	cp -r ../../env env
	./sanitize_names.sh
	
	zip -gr beryl-lang-windows.zip env

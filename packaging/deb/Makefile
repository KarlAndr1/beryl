DEB_RELEASE = 1
export DEB_RELEASE

PKG_NAME = beryl-lang_$(BERYL_MAJOR_VERSION).$(BERYL_SUBMAJOR_VERSION).$(BERYL_MINOR_VERSION)-$(DEB_RELEASE)

deb:
	mkdir -p $(PKG_NAME)/DEBIAN $(PKG_NAME)/etc/profile.d $(PKG_NAME)/usr/beryl $(PKG_NAME)/usr/bin $(PKG_NAME)/usr/include $(PKG_NAME)/usr/lib $(PKG_NAME)/usr/beryl
	cp ../../beryl $(PKG_NAME)/usr/bin/beryl
	cp ../../libberyl.a $(PKG_NAME)/usr/lib/libberyl.a
	cp ../../src/beryl.h $(PKG_NAME)/usr/include/beryl.h
	cp -rt $(PKG_NAME)/usr/beryl ../../env/*
	cp beryl.sh $(PKG_NAME)/etc/profile.d/beryl.sh
	
	#cp control $(PKG_NAME)/DEBIAN/control
	envsubst < ./control > $(PKG_NAME)/DEBIAN/control
	
	dpkg-deb --build $(PKG_NAME)

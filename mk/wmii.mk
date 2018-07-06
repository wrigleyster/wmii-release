VERSION = 3.9b1
CONFVERSION = 
COPYRIGHT = ©2009 Kris Maglione
CFLAGS += '-DVERSION=\"$(VERSION)\"' '-DCOPYRIGHT=\"$(COPYRIGHT)\"' \
	  '-DCONFVERSION=\"$(CONFVERSION)\"' '-DCONFPREFIX=\"$(ETC)\"'
FILTER = sed "s|@CONFPREFIX@|$(ETC)|g; \
	      s|@CONFVERSION@|$(CONFVERSION)|g; \
	      s|@DOCDIR@|$(DOC)|g; \
	      s|@LIBDIR@|$(LIBDIR)|g; \
	      s|@BINSH@|$(BINSH)|g; \
	      s|@TERMINAL@|$(TERMINAL)|g;"


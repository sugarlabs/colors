pythondir = $(shell python -c "from distutils import sysconfig; print sysconfig.get_python_lib()")

all clean:
	$(MAKE) -C colorsc $@

install: all
	./setup.py fix_manifest
	sed -i /^colorsc/d MANIFEST
	./setup.py install --prefix=$(DESTDIR)/usr
	for i in __init__.py _colorsc.so colorsc.py; do \
		install -m 644 -D colorsc/$$i $(DESTDIR)/$(pythondir)/colorsc/$$i; \
	done

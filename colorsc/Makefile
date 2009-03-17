CXXFLAGS = $(shell pkg-config --cflags gdk-x11-2.0) \
           $(shell pkg-config --cflags gstreamer-0.10) \
		   $(shell pkg-config --cflags pygtk-2.0) \
		   $(shell python-config --cflags) \
           -fPIC -O2
LDFLAGS  = $(shell pkg-config --libs gdk-x11-2.0) \
           $(shell pkg-config --libs gstreamer-0.10) \
		   $(shell pkg-config --libs pygtk-2.0) \
           $(shell python-config --libs)

ARCH = $(shell arch | grep 64 >/dev/null && echo linux64 || echo linux32)
PYTHON_VERSION = $(shell python -c 'import sys; print "%d%d" % sys.version_info[0:2]')
LIB_DIR = $(ARCH)_$(PYTHON_VERSION)

all : _colorsclib.so
	rm -rf $(LIB_DIR)
	mkdir $(LIB_DIR)
	strip -s _colorsclib.so
	mv _colorsclib.so colorsclib.py $(LIB_DIR)/
	touch $(LIB_DIR)/__init__.py

clean:
	rm -rf _colorsclib.so colorsclib.cpp colorsclib.py colorsclib.pyc *.o

%.cpp: %.i
	swig -c++ -python -o $*.cpp $<

_canvas.o: colorsc.h canvas.h 

_colorsclib.so: colorsclib.o canvas.o
	$(CXX) -shared $(LDFLAGS) -o $@ $^

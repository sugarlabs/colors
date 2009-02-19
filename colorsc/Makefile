CXXFLAGS = $(shell pkg-config --cflags gdk-x11-2.0) \
           $(shell pkg-config --cflags gstreamer-0.10) \
		   $(shell pkg-config --cflags pygtk-2.0) \
		   $(shell python-config --cflags) \
           -fPIC -O2
LDFLAGS  = $(shell pkg-config --libs gdk-x11-2.0) \
           $(shell pkg-config --libs gstreamer-0.10) \
		   $(shell pkg-config --libs pygtk-2.0) \
           $(shell python-config --libs)

all: _colorsc.so

clean:
	rm -rf _colorsc.so colorsc.cpp colorsc.py colorsc.pyc *.o

%.cpp: %.i
	swig -c++ -python -o $*.cpp $<

canvas.o: colorsc.h canvas.h 

_colorsc.so: colorsc.o canvas.o
	$(CXX) -shared $(LDFLAGS) -o $@ $^

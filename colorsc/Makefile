all: _colorsc.so

clean:
	rm -rf _colorsc.so *_wrap.cxx colorsc.py colorsc.pyc build

%_wrap.cxx: %.i
	swig -c++ -python $<

_colorsc.so: colorsc_wrap.cxx colorsc.h canvas.h canvas.cpp
	python setup.py build_ext --inplace


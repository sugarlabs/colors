swig -c++ -python colorsc.i
python setup.py build_ext --inplace
mv _colorsc.so ../
mv colorsc.py ../


#!/usr/bin/env python

"""
setup.py file for colorsc
"""

from distutils.core import setup, Extension

colorsc_module = Extension('_colorsc', sources=['canvas.cpp', 'palette.cpp', 'colorsc_wrap.cxx'] )

setup (name = 'colorsc', version = '0.1',
       author      = "Jens Andersson",
       description = """Colors C extension library.""",
       ext_modules = [colorsc_module],  py_modules = ["colorsc"])


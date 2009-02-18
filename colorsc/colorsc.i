/*
    Copyright 2008 by Jens Andersson and Wade Brainerd.  
    This file is part of Colors! XO.

    Colors is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Colors is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Colors.  If not, see <http://www.gnu.org/licenses/>.
*/
%module colorsc 

%{
#include <pygobject.h>

#include "canvas.h"
#include "palette.h"
%}

// Testing facility.
%typemap(in) void* {
        $1 = $input;
}

// Pass a gst.Buffer as a GstBuffer*.
%typemap(in) GstBuffer* {
        // todo- Error checking would be nice.
        PyGObject* pygo = (PyGObject*)$input;
        GstBuffer* buf = (GstBuffer*)pygo->obj;
        $1 = buf;
}

// Pass a gtk.gdk.Image as a GdkImage*.
%typemap(in) GdkImage* {
        // todo- Error checking would be nice.
        PyGObject* pygo = (PyGObject*)$input;
        GdkImage* img = (GdkImage*)pygo->obj;
        $1 = img;
}

// Return SurfaceA8R8G8B8 as Python string.
%typemap(out) SurfaceA8R8G8B8 {
        $result = PyString_FromStringAndSize((const char*)$1.pixels, $1.stride*$1.height);
}

// Return ByteBuffer as Python string.
%typemap(out) ByteBuffer {
        $result = PyString_FromStringAndSize((const char*)$1.data, $1.size);
}

%include "colorsc.h"
%include "canvas.h"
%include "palette.h"


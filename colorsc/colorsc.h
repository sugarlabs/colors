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
#ifndef _COLORSC_H_
#define _COLORSC_H_

#include <Python.h>

#include <vector>
#include <algorithm>
using namespace std;

#include <cmath>
#include <float.h>

typedef guint16 depth16_t;
typedef guint32 depth24_t;

static const float PI = 3.14159f;

inline float sgn(float a) { if (a>0) return 1; if (a<0) return -1; return 0; }

inline float sqr(float a) { return a*a; }

inline float clamp(float a, float mn, float mx) { return max(mn, min(mx, a)); }

inline float to_rad(float degrees) { return degrees * PI/180.0f; }
inline float to_deg(float rads) { return rads * 180.0f/PI; }

// Maps an incoming value in a given f0-t0 range to a new value in the f1-t1 range, optionally clamping to f1-t1.
inline float map_range(float a, float f0, float t0, float f1, float t1, bool clmp=true)
{
    float r = (a-f0)/(t0-f0);
    if (clmp) r = clamp(r, 0.0f, 1.0f);
    return f1+r*(t1-f1);
}

// Scales a value by an 8bit scale factor from 0-255.  The MSB of the scale factor is added to it, which means that
// scaling by 255 is an identity.
inline int fixed_scale(int value, int scale)
{
    scale += scale >> 7;
    return (value * scale) >> 8;
}

inline unsigned short endian_swap(unsigned short v)
{
    return (v>>8) | (v<<8);
}

struct Pos
{
    float x, y;
    Pos() : x(0), y(0) {}
    Pos(float x, float y) : x(x), y(y) {}
    Pos operator+(const Pos& b) const { return Pos(x+b.x, y+b.y); }
    Pos operator-(const Pos& b) const { return Pos(x-b.x, y-b.y); }
    Pos operator*(const Pos& b) const { return Pos(x*b.x, y*b.y); }
    Pos operator/(const Pos& b) const { return Pos(x/b.x, y/b.y); }
    Pos operator*(float b) const { return Pos(x*b, y*b); }
    Pos operator/(float b) const { return Pos(x/b, y/b); }
    static Pos create_from_min(const Pos& a, const Pos& b) { return Pos(min(a.x,b.x), min(a.y,b.y)); }
    static Pos create_from_max(const Pos& a, const Pos& b) { return Pos(max(a.x,b.x), max(a.y,b.y)); }
    static Pos create_from_angle(float a, float r) { return Pos(cosf(a*PI/180.0f)*r, sinf(a*PI/180.0f)*r); }
    static Pos create_from_rotation(const Pos& a, const Pos& center, float t)
    {
        return Pos(
            (a.x-center.x)*cosf(t) - (a.y-center.y)*sinf(t) + center.x,
            (a.y-center.y)*cosf(t) + (a.x-center.x)*sinf(t) + center.y);
    }
};

struct Color
{
    unsigned char r, g, b, a;
    Color() : r(0), g(0), b(0), a(0) {}
    Color(int r, int g, int b, int a) : r(r), g(g), b(b), a(a) {}
    unsigned int get_a8r8g8b8() { return (a<<24) | (r<<16) | (g<<8) | (b<<0); }
    static Color create_from_a8r8g8b8(unsigned int v)
    {
        Color c;
        c.a = (v>>24) & 0xff;
        c.r = (v>>16) & 0xff;
        c.g = (v>> 8) & 0xff;
        c.b = (v>> 0) & 0xff;
        return c;
    }
    unsigned int get_a8b8g8r8() { return (a<<24) | (b<<16) | (g<<8) | (r<<0); }
    static Color create_from_a8b8g8r8(unsigned int v)
    {
        Color c;
        c.a = (v>>24) & 0xff;
        c.b = (v>>16) & 0xff;
        c.g = (v>> 8) & 0xff;
        c.r = (v>> 0) & 0xff;
        return c;
    }
    unsigned int get_r5g6b5() { return ((r>>3)<<11) | ((g>>2)<<5) | (b>>3); }

    void to_pixel(depth16_t *pixel)
    {
        *pixel = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3);
    }

    void to_pixel(depth24_t *pixel)
    {
        *pixel = (r<<16) | (g<<8) | b;
    }

    static Color create_from_r5g6b5(unsigned short v)
    {
        Color c;
        c.r = ((v>>11)&0x1f)<<3;
        c.g = ((v>> 5)&0x1f)<<3;
        c.b = ((v>> 0)&0x1f)<<3;
        c.a = 255;
        return c;
    }
    unsigned int get_b5g6r5() { return ((b>>3)<<11) | ((g>>2)<<5) | (r>>3); }
    static Color create_from_float(float r, float g, float b, float a)
    {
        Color c;
        c.r = int(r*255.0f);
        c.g = int(g*255.0f);
        c.b = int(b*255.0f);
        c.a = int(a*255.0f);
        return c;
    }
    static Color create_from_blend(const Color& a, const Color& b)
    {
        return create_from_lerp(a, b, a.a);
    }
    static Color create_from_lerp(const Color& a, const Color& b, unsigned int l)
    {
        unsigned int il = 255-l;
        l += l >> 7;
        il += il >> 7;
        Color c;
        c.r = ((a.r * l) + (b.r * il)) >> 8;
        c.g = ((a.g * l) + (b.g * il)) >> 8;
        c.b = ((a.b * l) + (b.b * il)) >> 8;
        c.a = ((a.a * l) + (b.a * il)) >> 8;
        return c;
    }
    static Color create_from_yuv(unsigned char y, unsigned char u, unsigned char v)
    {
        Color c;
        c.r = (unsigned char)max(0.0f, min(255.0f, 1.164f*(y-16) + 1.596f*(v-128)));
        c.g = (unsigned char)max(0.0f, min(255.0f, 1.164f*(y-16) - 0.813f*(v-128) - 0.391f*(u-128)));
        c.b = (unsigned char)max(0.0f, min(255.0f, 1.164f*(y-16) + 2.018f*(u-128)));
        return c;
    }

    static Color yuv_to_hsv(unsigned int yuv)
    {
        Color c, d;
        unsigned char y,u,v,val,delta,vmin;
        float hue, sat;
        // v4l2src is sending YUYV, meaning, 4 bytes per 2 pixels.
        // pixel 1 gets the first 8 bit y and both 8 bit U and V.
        // pixel 2 gets the other y and u and v. we are only using pixel 1.
        y = ((yuv>>24)&0xFF);
        u = ((yuv>>16)&0xFF);
        v = (yuv&0xFF);
        // for now, yuv->first. TODO: go straight from yuv to hsv
        d = create_from_yuv(y, u, v);

        val = max(max(d.r, d.g), d.b);
        vmin = min(min(d.r, d.g), d.b);
        delta = val-vmin;
        if (delta == 0) {
            hue = 0.0;
            sat = 0.0;
        } else if (val == d.r) {
            hue = 42.5*(d.g-d.b)/delta;
            sat = 255.0*delta/val;
        } else if (val == d.g) {
            hue = 42.5*(d.b-d.r)/delta + 85.0;
            sat = 255.0*delta/val;
        } else {
            hue = 42.5*(d.r-d.g)/delta + 170.0;
            sat = 255.0*delta/val;
        }
        if (hue < 0.0) hue += 255.0;
        if (hue > 255.0) hue -= 255.0;
        c.r = (unsigned char)max(0.0f, min(255.0f, hue));
        c.g = (unsigned char)max(0.0f, min(255.0f, sat));
        c.b = (unsigned char)max((unsigned char)0, min((unsigned char)255, val));
        return c;       
    }

};

// Structure for passing byte data to and from Python.
struct ByteBuffer
{
    int size;
    void* data;
};

#endif


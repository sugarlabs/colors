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
#ifndef _PALETTE_H_
#define _PALETTE_H_

#include "colorsc.h"
#include "canvas.h"

// The Palette is the color wheel and triangle that are part of the brush controls dialog.
// This class manages rendering the palette as efficiently as possible into GdkImage objects which are
// then displayed on the screen.
// It also manages responding to mouse inputs that are passed from the Python code.
class Palette
{
public:
    static const int WHEEL_WIDTH = 75;

    int size;

    float palette_h, palette_s, palette_v;

    Pos triangle_cursor;
    bool triangle_capture;
    bool wheel_capture;

    Palette(int size) : size(size)
    {
        palette_h = palette_s = palette_v = 0;
        triangle_capture = false;
    }

    float get_wheel_radius()
    {
        return size - WHEEL_WIDTH/2;
    }

    void rgb_to_hsv(float r, float g, float b, float* h, float* s, float* v)
    {
        float mn = min(min(r, g), b);
        float mx = max(max(r, g), b);
        *v = mx;                    // v
        float delta = mx - mn;
        if (mx != 0)
            *s = delta / mx;        // s
        else 
        {
            // r = g = b = 0        // s = 0, v is undefined
            *s = 0;
            *h = -1;
            return;
        }
        if (delta == 0)
            *h = 0;
        else if (r == mx)
            *h = ( g - b ) / delta;     // between yellow & magenta
        else if (g == mx)
            *h = 2 + ( b - r ) / delta; // between cyan & yellow
        else
            *h = 4 + ( r - g ) / delta; // between magenta & cyan
        *h *= 60;                       // degrees
        if (*h < 0)
            *h += 360;
    }

    void hsv_to_rgb(float* r, float* g, float* b, float h, float s, float v)
    {
        if (s == 0) 
        {
            // achromatic (grey)
            *r = *g = *b = v;
            return;
        }
        h /= 60;                    // sector 0 to 5
        int i = int(floorf(h));
        float f = h - i;            // factorial part of h
        float p = v * (1-s);
        float q = v * (1-s * f);
        float t = v * (1-s * (1 - f));
        switch (i) 
        {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default:
        case 5: *r = v; *g = p; *b = q; break;
        }
    }

    void set_color(const Color& c)
    {
        rgb_to_hsv(c.r/255.0f, c.g/255.0f, c.b/255.0f, &palette_h, &palette_s, &palette_v);

        // I couldn't work out an exact solution to the blobby triangle position, so I'm using a relaxation algorithm.  
        // It usually takes less than 10 iterations to converge and only runs when you first open the palette.
        Pos p0, p1, p2;
        get_triangle_points(&p0, &p1, &p2);
        float side = distance(p0, p1);

        Pos p = (p0+p1+p2)*(1.0f/3.0f);
        float epsilon = 0.001f;
        int max_iterations = 50;
        for (int i = 0; i < max_iterations; i++)
        {
            float d0 = distance(p, p0);
            float d0ok = fabsf((side-d0)-palette_s*side);
            p = p + ((p0-p)/length(p0-p)) * (d0-(1.0f-palette_s)*side);

            float d2 = distance(p, p2);
            float d2ok = fabsf(d2-palette_v*side); 
            p = p + ((p2-p)/length(p2-p)) * (d2-palette_v*side);

            if (d0ok <= epsilon && d2ok <= epsilon) break;
        }
        triangle_cursor = p;
    }

    Color get_color()
    {
        float r, g, b;
        hsv_to_rgb(&r, &g, &b, palette_h, palette_s, palette_v);
        return Color::create_from_float(r, g, b, 1.0f);
    }

    float sqr(float a) 
    { 
        return a*a; 
    }

    float dot(const Pos& a, const Pos& b) 
    { 
        return a.x*b.x + a.y*b.y; 
    }

    float length(const Pos& a)
    {
        return sqrtf(dot(a,a));
    }

    float length_sqr(const Pos& a)
    {
        return dot(a,a);
    }

    float distance(const Pos& a, const Pos& b)
    {
        return length(a-b);
    }

    float distance_sqr(const Pos& a, const Pos& b)
    {
        return length_sqr(a-b);
    }

    Pos normalize(const Pos& a)
    {
        return a/length(a);
    }

    void get_triangle_points(Pos* p0, Pos* p1, Pos* p2)
    {
        Pos center(size/2, size/2);
        *p0 = center + Pos::create_from_angle(palette_h+0.0f, size/2-WHEEL_WIDTH);
        *p1 = center + Pos::create_from_angle(palette_h+120.0f, size/2-WHEEL_WIDTH);
        *p2 = center + Pos::create_from_angle(palette_h+240.0f, size/2-WHEEL_WIDTH);
    }

    template <typename pixel_t> inline
    void _render_wheel(GdkImage* image)
    {
        if (image->width != size || image->height != size)
        {
            fprintf(stderr, "Error: Invalid Palette GdkImage.\n");
            return;
        }

        Color bkg(64, 64, 64, 0);

        float wheel_radius = size/2;
        float ring_min_sqr = sqr(wheel_radius-WHEEL_WIDTH);
        float ring_max_sqr = sqr(wheel_radius);

        pixel_t* pixels = (pixel_t*)image->mem;
        int stride = image->bpl/sizeof(pixel_t);

        for (int y = 0; y < size; y++)
        {
            pixel_t* row = &pixels[y*stride];
            for (int x = 0; x < size; x++)
            {
                // Get radius from center of palette.
                float dx = x-wheel_radius;
                float dy = y-wheel_radius;
                float dist_sqr = dx*dx + dy*dy;

                // Inside color wheel ring?  Draw the wheel.
                if (dist_sqr >= ring_min_sqr && dist_sqr < ring_max_sqr)
                {
                    // Calculate HSV from wheel angle.
                    float h = atan2f(dy, dx)*180.0f/PI;
                    while (h<0) h += 360.0f;
                    while (h>360.0f) h -= 360.0f;
                    float r, g, b;
                    hsv_to_rgb(&r, &g, &b, h, 1.0f, 1.0f);
                    Color::create_from_float(r, g, b, 1).to_pixel(row);
                }
                else
                    bkg.to_pixel(row);
                ++row;
            }
        }
    }

    // The wheel never changes, so it can be rendered once here.  This also clears the image background.
    void render_wheel(GdkImage* image)
    {
        if (image->depth == 16)
            _render_wheel<depth16_t>(image);
        else
            _render_wheel<depth24_t>(image);
    }

    template <typename pixel_t>
    void _render_triangle(GdkImage* image)
    {
        if (image->width != size || image->height != size || (size&1))
        {
            fprintf(stderr, "Error: Invalid Palette GdkImage.\n");
            return;
        }

        Color bkg(64, 64, 64, 0);
        pixel_t bkgc;
        bkg.to_pixel(&bkgc);

        Pos p0, p1, p2;
        get_triangle_points(&p0, &p1, &p2);
        float triangle_side = distance(p0, p1);
        float triangle_side_sqr = triangle_side*triangle_side;
        float inv_triangle_side = 1.0f/triangle_side;

        float wheel_radius = size/2;
        float ring_min_sqr = sqr(wheel_radius-WHEEL_WIDTH);

        int x0 = WHEEL_WIDTH;
        int x1 = size-WHEEL_WIDTH;

        pixel_t* pixels = (pixel_t*)image->mem;
        int stride = image->bpl/sizeof(pixel_t);

        Pos p(x0,x0);
        for (int y = x0; y < x1; y+=2, p.y += 2.0f)
        {
            p.x = x0;
            pixel_t* __restrict row0 = &pixels[(y+0)*stride+x0];
            pixel_t* __restrict row1 = &pixels[(y+1)*stride+x0];
            for (int x = x0; x < x1; x+=2, p.x += 2.0f)
            {
                // Calculate position inside triangle.  If inside, then use as HSV.  
                float d0_sqr = distance_sqr(p, p0);
                float d1_sqr = distance_sqr(p, p1);
                float d2_sqr = distance_sqr(p, p2);
                if (d0_sqr <= triangle_side_sqr && d1_sqr <= triangle_side_sqr && d2_sqr <= triangle_side_sqr)
                {
                    float r, g, b;
                    hsv_to_rgb(&r, &g, &b, palette_h, 1.0f-sqrtf(d0_sqr)*inv_triangle_side, sqrtf(d2_sqr)*inv_triangle_side);
                    pixel_t c;
                    Color::create_from_float(r, g, b, 1).to_pixel(&c);
                    row0[0] = c; 
                    row0[1] = c;
                    row1[0] = c; 
                    row1[1] = c;
                }
                else
                {
                    // Inside ring?  Clear to background.
                    if (distance_sqr(p, Pos(wheel_radius, wheel_radius)) < ring_min_sqr)
                    {
                        row0[0] = bkgc;
                        row0[1] = bkgc;
                        row1[0] = bkgc;
                        row1[1] = bkgc;
                    }
                }
                row0 += 2;
                row1 += 2;
            }
        }
    }

    // Clears out the inner circle and redraws the color triangle, as fast as possible.  
    // Scales up implicitly by 2x to improve performance.
    // The appearance was an accident which creates a kind of blobby triangle, like the intersection of three circles.
    // But I like the look so I'm keeping it!
    void render_triangle(GdkImage* image)
    {
        if (image->depth == 16)
            _render_triangle<depth16_t>(image);
        else
            _render_triangle<depth24_t>(image);
    }

    Pos get_wheel_pos()
    {
        float a = palette_h*PI/180.0f;
        float r = size/2 - WHEEL_WIDTH/2;
        return Pos(size/2+r*cosf(a), size/2+r*sinf(a));
    }

    Pos get_triangle_pos()
    {
        return triangle_cursor;
    }

    void process_mouse(int mx, int my)
    {
        Pos p0, p1, p2;
        get_triangle_points(&p0, &p1, &p2);

        // If the triangle has the mouse capture, clip the mouse position to within the blobby triangle.
        if (!wheel_capture)
        {
            if (triangle_capture)
            {
                Pos p(mx, my);
                float d0 = distance(p, p0);
                float d1 = distance(p, p1);
                float d2 = distance(p, p2);
                float side = distance(p0, p1)-1;
                if (d0 > side || d1 > side || d2 > side)
                {
                    Pos far_point = p0;
                    float far_dist = d0;
                    if (d1 > far_dist) { far_point = p1; far_dist = d1; }
                    if (d2 > far_dist) { far_point = p2; far_dist = d2; }
                    p = far_point + normalize(p-far_point) * side;
                    mx = int(p.x);
                    my = int(p.y);
                }
            }
    
            // Now check to see if it's on the triangle.
            float side = 1.0f/distance(p0, p1);
            Pos p(mx, my);
            float d0 = distance(p, p0)*side;
            float d1 = distance(p, p1)*side;
            float d2 = distance(p, p2)*side;
            if (d0 <= 1.0f && d1 <= 1.0f && d2 <= 1.0f)
            {
                triangle_capture = true;
                triangle_cursor = p;
                palette_s = 1.0f-d0;
                palette_v = d2;
            }
        }

        if (!triangle_capture)
        {
            // Check to see if the mouse is in the wheel ring.
            int dx = mx-size/2;
            int dy = my-size/2;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist >= size/2-WHEEL_WIDTH || wheel_capture)
            {
                wheel_capture = true;
                float h = atan2f(float(dy), float(dx))*180.0f/PI;
                while (h<0) h += 360.0f;
                while (h>360.0f) h -= 360.0f;
                // Rotate the triangle cursor with the wheel.
                triangle_cursor = Pos::create_from_rotation(triangle_cursor, (p0+p1+p2)/3.0f, (h-palette_h)*PI/180.0f);
                palette_h = h;
            }
        }
    }

    void process_mouse_release()
    {
        triangle_capture = false;
        wheel_capture = false;
    }
};

// The BrushPreview is a simple preview of the current brush as displayed in the Brush Controls panel.
// This class handles rendering the brush preview to a GdkImage to be displayed on the screen.
class BrushPreview
{
public:
    int size;

    Brush brush;

    BrushPreview(int size) : size(size)
    {
    }

    template <typename pixel_t> inline
    void _render(GdkImage* image)
    {
        if (image->width != size || image->height != size || (size&1))
        {
            fprintf(stderr, "Error: Invalid BrushPreview GdkImage.\n");
            return;
        }

        // Minimum brush size.
        if (brush.size<2) brush.size = 2;

        // Calculate interpolation constants
        float db = (BrushType::DIST_TABLE_WIDTH-1) / float(brush.size);

        float xb = BrushType::DIST_TABLE_CENTER - (size/2)*db;
        float yb = BrushType::DIST_TABLE_CENTER - (size/2)*db;

        // Select which line of the brush-lookup-table to use that most closely matches the current brush width
        int brushidx = int(float(BrushType::BRUSH_TABLE_HEIGHT) / brush.size);

        int opacity = int(round(255.0f * brush.opacity));

        pixel_t* pixels = (pixel_t*)image->mem;
        int stride = image->bpl/sizeof(pixel_t);

        Color bkg(0xff, 0xff, 0xff, 0);
        pixel_t bkgc;
        bkg.to_pixel(&bkgc);

        // Interpolate the distance table over the area. For each pixel find the distance, and look the 
        // brush-intensity up in the brush-table
        for (int y = 0; y < size; y+=2)
        {
            float x2b = xb;
            pixel_t* __restrict row0 = &pixels[(y+0)*stride];
            pixel_t* __restrict row1 = &pixels[(y+1)*stride];
            for (int x = 0; x < size; x+=2)
            {
                // Find brush-intensity and mulitply that with incoming opacity
                if (x2b >= 0 && x2b < BrushType::DIST_TABLE_WIDTH && yb >= 0 && yb < BrushType::DIST_TABLE_WIDTH)
                {
                    int lookup = BrushType::distance_tbl[int(x2b)][int(yb)];
                    int intensity = fixed_scale(Brush::brush_type[brush.type].intensity_tbl[lookup][brushidx], opacity);
                    Color i = Color::create_from_a8r8g8b8(0xffffffff);
                    //Color i = Color::create_from_a8r8g8b8(image[y*size+x]);
                    i = Color::create_from_lerp(brush.color, i, intensity);
                    pixel_t c;
                    i.to_pixel(&c);
                    row0[0] = c;
                    row0[1] = c;
                    row1[0] = c;
                    row1[1] = c;
                }
                else
                {
                    row0[0] = bkgc;
                    row0[1] = bkgc;
                    row1[0] = bkgc;
                    row1[1] = bkgc;
                }

                row0 += 2;
                row1 += 2;

                x2b += db*2;
            }
            yb += db*2;
        }
    }

    void render(GdkImage* image)
    {
        if (image->depth == 16)
            _render<depth16_t>(image);
        else
            _render<depth24_t>(image);
    }
};

#endif


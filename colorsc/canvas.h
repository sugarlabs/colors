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
#ifndef _CANVAS_H_
#define _CANVAS_H_

#include <gdk/gdkimage.h>
#include <gst/gstbuffer.h>

#include "colorsc.h"
#include "drwfile.h"

using namespace std;

// Uncomment this to print all executed drawing commands to stdout.
//#define CANVAS_DEBUG_COMMANDS

// Structure for passing pixel data to and from Python.
struct SurfaceA8R8G8B8
{
    int width, height;
    int stride;
    unsigned int* pixels;
};

// Structure for passing buffers of draw commands to and from Python.
struct DrawCommandBuffer
{
    DrawCommandBuffer()
    {
        cmds = NULL;
        ncommands = 0;
    }

    DrawCommandBuffer(const char* _cmds, int _ncommands)
    {
        cmds = (char*)malloc(_ncommands*sizeof(unsigned int));
        memcpy(cmds, _cmds, _ncommands*sizeof(unsigned int));
        ncommands = _ncommands;
    }

    DrawCommandBuffer(const DrawCommandBuffer& b)
    {
        cmds = (char*)malloc(b.ncommands*sizeof(unsigned int));
        memcpy(cmds, b.cmds, b.ncommands*sizeof(unsigned int));
        ncommands = b.ncommands;
    }

    ~DrawCommandBuffer()
    {
        if (cmds)
        {
            free((void*)cmds);
            cmds = NULL;
        }
    }

    const DrawCommandBuffer& operator=(const DrawCommandBuffer& b)
    {
        if (cmds)
            free((void*)cmds);
        cmds = (char*)malloc(b.ncommands*sizeof(unsigned int));
        memcpy(cmds, b.cmds, b.ncommands*sizeof(unsigned int));
        ncommands = b.ncommands;
        return *this;
    }

    void append(const DrawCommandBuffer& b)
    {
        char* newcmds = (char*)malloc((ncommands+b.ncommands)*sizeof(unsigned int));
        if (cmds)
        {
            memcpy(newcmds, cmds, ncommands*sizeof(unsigned int));
            free(cmds);
        }
        memcpy(newcmds + ncommands*sizeof(unsigned int), b.cmds, b.ncommands*sizeof(unsigned int));
        cmds = newcmds;
        ncommands = ncommands + b.ncommands;
    }

    void clear()
    {
        if (cmds)
        {
            free(cmds);
            cmds = NULL;
        }
        ncommands = 0;
    }

    ByteBuffer get_bytes()
    {
        ByteBuffer buf;
        buf.size = ncommands*sizeof(unsigned int);
        buf.data = cmds;
        return buf;
    }

    char* cmds;
    int ncommands;

    static DrawCommandBuffer create_from_string(const char* cmds, int ncommands)
    {
        return DrawCommandBuffer(cmds, ncommands);
    }
};

struct DrawCommand
{
    enum
    {
        TYPE_DRAW         = 0,
        TYPE_DRAWEND      = 1,
        TYPE_COLORCHANGE  = 2,
        TYPE_SIZECHANGE   = 3,
    };

    int type;
    Pos pos;
    Color color;
    int pressure;
    bool flipx;
    bool flipy;
    bool is_text;
    uint32_t text;
    int brush_control;
    int brush_type;
    float size;
    float opacity;

    DrawCommand()
    {
        type = TYPE_DRAW;
        pressure = 0;
        flipx = false;
        flipy = false;
        is_text = false;
        text = 0;
        brush_control = 0;
        brush_type = 0;
        size = 0;
        opacity = 0;
    }

    static DrawCommand create_color_change(const Color& c)
    {
        DrawCommand cmd;
        cmd.type = TYPE_COLORCHANGE;
        cmd.color = c;
        return cmd;
    }

    static DrawCommand create_draw(const Pos& pos, int pressure)
    {
        DrawCommand cmd;
        cmd.type = TYPE_DRAW;
        cmd.pos = pos;
        cmd.pressure = pressure;
        return cmd;
    }

    static DrawCommand create_end_draw(int pressure)
    {
        DrawCommand cmd;
        cmd.type = TYPE_DRAWEND;
        cmd.pressure = pressure;
        return cmd;
    }

    static DrawCommand create_size_change(int brush_control, int brush_type, float size, float opacity)
    {
        DrawCommand cmd;
        cmd.type = TYPE_SIZECHANGE;
        cmd.brush_control = brush_control;
        cmd.brush_type = brush_type;
        cmd.size = size;
        cmd.opacity = opacity;
        return cmd;
    }

    static DrawCommand create_flip(bool flipx)
    {
        DrawCommand cmd;
        cmd.type = TYPE_COLORCHANGE;
        cmd.flipx = flipx;
        cmd.flipy = !flipx;
        return cmd;
    }
};

struct BrushType
{
    enum
    {
        BRUSHTYPE_HARD   = 0,
        BRUSHTYPE_SOFT   = 1,
        BRUSHTYPE_CURSOR = 2,
        NUM_BRUSHES      = 3,
    };

    static const int DIST_TABLE_WIDTH  = 256;                   // Width of distance lookup-table
    static const int DIST_TABLE_CENTER = DIST_TABLE_WIDTH / 2;  // Center of distance lookup-table

    static const int BRUSH_TABLE_WIDTH  = 256;    // Width of brush lookup-table
    static const int BRUSH_TABLE_HEIGHT = 65;     // Height of 65 allows a brushsize down to 1.0f

    static const float EXTRA_BRUSH_SCALE  = 1.023f;  // Scales down the brush-size so we don't index-out-of-range.

    static unsigned char distance_tbl[DIST_TABLE_WIDTH][DIST_TABLE_WIDTH];

    unsigned char intensity_tbl[BRUSH_TABLE_WIDTH][BRUSH_TABLE_HEIGHT];

    // Creates a simple sqrt-lookup table.
    static void create_distance_table()
    {
        for (int x = 0; x < DIST_TABLE_WIDTH; x++)
            for (int y = 0; y < DIST_TABLE_WIDTH; y++)
            {
                int dx = x - DIST_TABLE_CENTER;
                int dy = y - DIST_TABLE_CENTER;
                float dist = sqrtf(float(dx * dx + dy * dy));
                distance_tbl[x][y] = (unsigned char)min(255.0f, dist*255/DIST_TABLE_CENTER);
            }
    }

    // Calculates a gradient between 0 and 1 where the derivate of both 0 and 1 is 0.
    // This function is used to calculate the falloff of the brushes.
    float smooth_step(float a)
    {
        return sinf((a*a - 0.5f) * 3.14159f) * 0.5f + 0.5f;
    }

    void create_brush(float brush_border, float amp)
    {
        // Find at what range from brush-center the brush intensity goes below 2
        float max_r = 0;
        for (int i = BRUSH_TABLE_WIDTH-1; i >= 0; i--)
        {
            float f = float(i) / BRUSH_TABLE_WIDTH;
            float f2 = 1.0f - (f - brush_border) / (1.0f - brush_border);
            if (round(smooth_step(f2) * amp) >= 2)
            {
                max_r = i;
                break;
            }
        }

        // Calculate a scale-factor so the brush optimally uses the area
        float r = float(max_r + 2) / BRUSH_TABLE_WIDTH / BRUSH_TABLE_WIDTH;

        for (int y = 0; y < BRUSH_TABLE_HEIGHT; y++)
        {
            // Each line in the brush-table is calculated for a specific brush-size
            // This has two functions:
            // 1. Be able to simulate the effect of resampling of the "perfect" big brush to a smaller one to improve precision
            // 2. Compensate for the need to scale small brushes to avoid index out of range during rastering

            // Calculate scale for this width
            float brushscale = EXTRA_BRUSH_SCALE + y * 2.0f / 64.0f;

            // Calculate brush
            unsigned int intensity_row[BRUSH_TABLE_WIDTH];
            for (int i = 0; i < BRUSH_TABLE_WIDTH; i++)
            {
                float f = min(i * r * brushscale, 1.0f); // Apply the two different scales
                if (f < brush_border)
                    intensity_row[i] = int(amp);
                else
                {
                    float f2 = 1.0f - (f - brush_border) / (1.0f - brush_border);
                    f2 = smooth_step(f2) * amp; // Make sure the border-falloff is smooth
                    intensity_row[i] = int(round(f2));
                }
            }

            // Simulate the effect of resampling
            int blurradius = int(round(y * BRUSH_TABLE_WIDTH / (brushscale * 64.0f)));
            float maxintensity = 0;
            for (int x = 0; x < BRUSH_TABLE_WIDTH; x++)
            {
                float l = 0;
                for (int x2 = x - blurradius; x2 < x + blurradius + 1; x2++)
                {
                    int i = min(max(x2, 0), BRUSH_TABLE_WIDTH-1);
                    if (i < BRUSH_TABLE_WIDTH)
                        l += intensity_row[i];
                }
                float intensity = l / (blurradius * 2 + 1);
                if (intensity > maxintensity)
                    maxintensity = intensity;
                intensity_tbl[x][y] = int(intensity);
            }
        }
    }

    void create_hard_brush()
    {
        create_brush(0.8f, 255);
    }

    void create_soft_brush()
    {
        create_brush(0.0f, 128);
    }

    void create_cursor()
    {
    }
};

class Brush
{
public:
    enum
    {
        BRUSHCONTROL_VARIABLEOPACITY = 1,
        BRUSHCONTROL_VARIABLESIZE    = 2,
    };

    static BrushType brush_type[BrushType::NUM_BRUSHES];

    Color color;
    int type;
    int size;
    int control;
    float opacity;

    Brush()
    {
        type = BrushType::BRUSHTYPE_HARD;
        color = Color(255, 255, 255, 255);
        size = 32;
        control = 0;
        opacity = 1.0f;
    }
    
    Brush(const Brush& a)
    {
        type = a.type;
        color = a.color;
        size = a.size;
        control = a.control;
        opacity = a.opacity;
    }
};

// The canvas represents the current state of the user's painting.  It maintains both the pixels representing the
// image, and also the complete list of drawing commands that contributed to the image.
class Canvas
{
public:
    // Dimensions of the reference picture (webcam snapshot).
    static const int REFERENCE_WIDTH  = 640;
    static const int REFERENCE_HEIGHT = 480;

    // Dimensions of the videopaint buffer.
    static const int VIDEO_WIDTH  = 80;
    static const int VIDEO_HEIGHT = 60;

    enum 
    {
        DRAWBRUSH_TYPE_NORMAL    = 0,
        DRAWBRUSH_TYPE_OLDCURSOR = 1,
        DRAWBRUSH_TYPE_DIRECT    = 2,
        DRAWBRUSH_TYPE_GETCOLOR  = 3,
        DRAWBRUSH_TYPE_CURSOR    = 4,
    };

    // List of drawing commands that make up the painting.
    vector<DrawCommand> commands;

    // Canvas dimensions.
    int width;
    int height;

    // Current state of the canvas pixels.
    unsigned int* image;

    // Backup and alpha channel are used for multiple purposes.  See description in Drawing section.
    unsigned int* image_backup;
    unsigned char* alpha;

    // Shared (master) picture for collaborative painting.
    unsigned int* image_shared;

    // Reference (webcam snapshot) picture.
    unsigned short* image_reference;

    // Videopaint variables.
    unsigned int* image_video[2];
    int video_idx;
    Pos videopaint_pos;
    float videopaint_pressure;

    // Current brush state.
    Brush brush;

    // Variables for interpolating the brush across strokes.
    Pos lastpos;
    Pos lastorgpos;
    float lastpressure;

    // Dimensions of the canvas that have been modified since the last call to reset_dirty_rect.
    Pos dirtymin;
    Pos dirtymax;

    // Dimensions and state of the current stroke.
    Pos strokemin;
    Pos strokemax;
    bool stroke;
    int idle_while_drawing;
    int drawtype;

    // VCR playback variables.
    bool playing;
    int playback;
    int playback_speed;

    // True if the canvas has been modified since the last save.
    bool modified;

    Canvas(int width, int height) : width(width), height(height)
    {
        image = new unsigned int[width*height];
        image_backup = new unsigned int[width*height];
        alpha = new unsigned char[width*height];

        image_shared = new unsigned int[width*height];

        image_reference = new unsigned short[REFERENCE_WIDTH*REFERENCE_HEIGHT];
        memset(image_reference, 0, REFERENCE_WIDTH*REFERENCE_HEIGHT*sizeof(unsigned short));

        image_video[0] = new unsigned int[VIDEO_WIDTH*VIDEO_HEIGHT];
        image_video[1] = new unsigned int[VIDEO_WIDTH*VIDEO_HEIGHT];
        memset(image_video[0], 0, VIDEO_WIDTH*VIDEO_HEIGHT*sizeof(unsigned int));
        memset(image_video[1], 0, VIDEO_WIDTH*VIDEO_HEIGHT*sizeof(unsigned int));
        video_idx = 0;

        clear();

        // Initialize lookup table.
        BrushType::create_distance_table();

        // Initialize brushes.
        Brush::brush_type[BrushType::BRUSHTYPE_HARD].create_hard_brush();
        Brush::brush_type[BrushType::BRUSHTYPE_SOFT].create_soft_brush();
        Brush::brush_type[BrushType::BRUSHTYPE_CURSOR].create_cursor();

        reset_brush();

        lastpos = Pos(0,0);
        lastorgpos = Pos(0,0);
        lastpressure = 0;

        dirtymin = Pos(FLT_MAX,FLT_MAX);
        dirtymax = Pos(-FLT_MAX,-FLT_MAX);

        strokemin = Pos(0,0);
        strokemax = Pos(0,0);
        stroke = false;

        playing = false;
        playback = 0;
        playback_speed = 1;
        modified = false;

        idle_while_drawing = 0;

        drawtype = DRAWBRUSH_TYPE_NORMAL;
    }

    ~Canvas()
    {
        delete[] image;
        delete[] image_backup;
        delete[] image_shared;
        delete[] alpha;
    }

    // Clears the entire canvas (command history and image).
    void clear()
    {
        commands.clear();
        clear_image();
    }

    // Changes the size of the canvas.
    // Rather than trying to repaint everything from scratch, we simply quickly rescale it.
    void resize(int new_width, int new_height)
    {
        unsigned int* new_image = new unsigned int[new_width*new_height];
        unsigned int* new_image_backup = new unsigned int[new_width*new_height];
        unsigned char* new_alpha = new unsigned char[new_width*new_height];
        unsigned int* new_image_shared = new unsigned int[new_width*new_height];

        int dx = (1<<16) * width / new_width;
        int dy = (1<<16) * height / new_height;
        int ry = 0;
        for (int y = 0; y < new_height; y++)
        {
            int rx = 0;
            for (int x = 0; x < new_width; x++)
            {
                int sofs = (ry>>16)*width + (rx>>16);
                int dofs = y*new_width+x;
                new_image[dofs] = image[sofs];
                new_image_backup[dofs] = image_backup[sofs];
                new_alpha[dofs] = alpha[sofs];
                new_image_shared[dofs] = image_shared[sofs];
                rx += dx;
            }
            ry += dy;
        }
 
        delete[] image;
        delete[] image_backup;
        delete[] alpha;
        delete[] image_shared;
        
        width = new_width;
        height = new_height;
        
        image = new_image;
        image_backup = new_image_backup;
        alpha = new_alpha;
        image_shared = new_image_shared;
    }
    
    // Resets the brush to a random color and a default size and type.
    void reset_brush()
    {
        // Choose a suitable random brush color.
        srand(clock());
        int c0 = rand()%3;
        int c1 = c0 + 1 + rand()%2;
        if (c1 > 2)
            c1 -= 3;
        brush.type = BrushType::BRUSHTYPE_HARD;
        brush.color = Color::create_from_a8r8g8b8(0xff000000 | (255 << (c0 * 8) | ((rand()%255) << (c1 * 8))));
        brush.size = width/16;
        brush.control = Brush::BRUSHCONTROL_VARIABLESIZE;
        brush.opacity = 1.0f;
    }

    //---------------------------------------------------------------------------------------------
    // Shared image.
    // 
    // The shared image is used in collaborative painting, and contains the current 'master' state of the canvas
    // 
    // The user can paint ahead of the master state, but when a new master state is received the canvas
    // state will be reset to the shared image.  Before this happens though, the user commands are transmitted 
    // to the activity host, so they will be received again as a new master image later and not be lost.
    void save_shared_image()
    {
        memcpy(image_shared, image, width*height*sizeof(unsigned int));
    }

    void restore_shared_image()
    {
        memcpy(image, image_shared, width*height*sizeof(unsigned int));
        memcpy(image_backup, image_shared, width*height*sizeof(unsigned int));
    }

    //---------------------------------------------------------------------------------------------
    // Drawing
    // 
    // These methods are for drawing to and clearing the canvas.
    // 
    // Drawing is not done directly to the canvas.  The way the brush system works, the brush shapes are 
    // actually drawn into the alpha channel of the canvas, and the alpha channel is used to blend the
    // brush color over the backup image to create the real image.  
    // 
    // The net effect this is that during a stroke, which may overlap itself many times over, the brush color
    // will only be applied to any particular canvas pixel up to the defined brush transparency level.  
    // This is the core of our "natural media" engine.

    void clear_image()
    {
        memset(image, 0xff, width*height*sizeof(unsigned int));
        memset(image_backup, 0xff, width*height*sizeof(unsigned int));
        memset(alpha, 0, width*height*sizeof(unsigned char));

        memset(image_shared, 0xff, width*height*sizeof(unsigned int));

        dirtymin = Pos(0, 0);
        dirtymax = Pos(width, height);
    }

    // Called from command_draw and calculates a temporary brush size depending on pressure/alpha (0-255).
    float get_variable_brush_size(float pressure)
    {
        if (brush.control & Brush::BRUSHCONTROL_VARIABLESIZE)
        {
            float size = pressure * brush.size / 255.0f;
            if (size < 2.0f)
                size = 2.0f;
            return size;
        }
        else
            return brush.size;
    }

    // Called each tick while stylus is touching the screen and draws the selected brush into the Alpha of the Canvas.
    void command_draw(const Pos& pos, int pressure, bool forced)
    {
        lastorgpos = pos;

        if (brush.control == 0)
            pressure = 255;

        int size, opacity;
        if (!stroke)
        {
            // This is a new stroke. Just draw the brush on incoming position, store the information needed for interpolation and wait for next call
            if (brush.control & Brush::BRUSHCONTROL_VARIABLESIZE)
                size = int(get_variable_brush_size(pressure));
            else
                size = brush.size;

            if (brush.control & Brush::BRUSHCONTROL_VARIABLEOPACITY)
                opacity = int(round(pressure * brush.opacity));
            else
                opacity = int(round(255.0f * brush.opacity));
            draw_brush(pos, size, opacity);

            // Reset stroke dirty regions
            strokemin = pos;
            strokemax = pos;

            lastpos = pos;
            lastpressure = pressure;
            idle_while_drawing = 0;
            stroke = true;
        }
        else
        {
            // This is continous stroke. Interpolate from last postion/pressure

            // Calculate the stroke-distance
            float distx = pos.x - lastpos.x;
            float disty = pos.y - lastpos.y;
            float dista = pressure - lastpressure;
            float distance = sqrtf(distx * distx + disty * disty);
            if (distance == 0.0f) // To avoid division by zero. None or very small movements are handled later
                distance = 0.0001f;

            // Calculate interpolation constants
            float dx = distx / distance;
            float dy = disty / distance;
            float da = dista / distance;

            // Calculate the spacing between two brush-stamp.  Normal spacing is 25% of brushsize.
            float spacing = 0.225f;

            // Do this special spacing only for just VariableOpacity with Hard brush to avoid banding
            // Decrease spacing if pressure is changing rapidly
            if (da != 0 && brush.control == Brush::BRUSHCONTROL_VARIABLEOPACITY && brush.type == BrushType::BRUSHTYPE_HARD)
                spacing = min(0.225f, max(fabsf(15.0f / brush.size / (da * brush.opacity)), 0.05f));

            // Calculate the distance between two brushes from spacing
            float spacingdistance = get_variable_brush_size(lastpressure) * spacing;
            if (distance < spacingdistance)
            {
                // Too small movement to interpolate
                idle_while_drawing++;
                if (idle_while_drawing > 15 || forced)
                {
                    // We've been idling too long. Draw the brush and reduce idle-counter
                    // Idle-counter is invalid during playback, since playback only records input that actually drew something
                    idle_while_drawing = 10;
    
                    lastpos = pos;
                    lastpressure = pressure;
                    if (brush.control & Brush::BRUSHCONTROL_VARIABLEOPACITY)
                        draw_brush(pos, int(get_variable_brush_size(lastpressure)), int(round(pressure * brush.opacity)));
                    else
                        draw_brush(pos, int(get_variable_brush_size(lastpressure)), int(round(255.0f * brush.opacity)));
                }
                return;
            }

            if (brush.control & Brush::BRUSHCONTROL_VARIABLESIZE)
            {
                // Brush size is controlled by pressure
                while (distance >= spacingdistance)
                {
                    // Interpolate stroke
                    lastpressure += da * spacingdistance;
                    lastpos.x += dx * spacingdistance;
                    lastpos.y += dy * spacingdistance;
                    distance -= spacingdistance;

                    float brushsize = get_variable_brush_size(int(lastpressure));
                    if (brush.control & Brush::BRUSHCONTROL_VARIABLEOPACITY)
                        draw_brush(lastpos, int(get_variable_brush_size(int(lastpressure))), int(round(pressure * brush.opacity)));
                    else
                        draw_brush(lastpos, int(get_variable_brush_size(int(lastpressure))), int(round(255.0f * brush.opacity)));

                    // Since brush-size may have changed, we need to calculate new spacing
                    spacingdistance = brushsize * spacing;
                }
            }
            else
            {
                // Brush size is static, so we can pre-multiply the interpolation constants
                dx *= spacingdistance;
                dy *= spacingdistance;
                da *= spacingdistance;
                while (distance >= spacingdistance)
                {
                    lastpressure += da;
                    lastpos.x += dx;
                    lastpos.y += dy;
                    distance -= spacingdistance;

                    draw_brush(lastpos, brush.size, int(round(lastpressure * brush.opacity)));
                }
            }
        }
    }

    // Called when stylus stops touching the screen.
    void command_enddraw()
    {
        if (!stroke)
            return;

        // Copy current image to backup image and clear alpha in the region of the stroke.
        int x0 = max(min(int(strokemin.x), width), 0);
        int x1 = max(min(int(strokemax.x), width), 0);
        int y0 = max(min(int(strokemin.y), height), 0);
        int y1 = max(min(int(strokemax.y), height), 0);
        for (int y = y0; y < y1; y++)
        {
            memcpy(&image_backup[y*width+x0], &image[y*width+x0], (x1-x0)*sizeof(unsigned int));
            memset(&alpha[y*width+x0], 0, (x1-x0)*sizeof(unsigned char));
        }

        stroke = false;
    }

    // The canvas keeps track of the area that has been modified by draw commands, this is called the 'dirty' rectangle.
    // The dirty rectangle keeps accumulating until reset_dirty_rect is called, at which point it is cleared to empty.
    void reset_dirty_rect()
    {
        dirtymin = Pos(FLT_MAX,FLT_MAX);
        dirtymax = Pos(-FLT_MAX,-FLT_MAX);
    }

    // Rasters a brush with specified width and opacity into alpha at a specified position using lookup-tables.
    void draw_brush(const Pos& pos, int brushwidth, int opacity)
    {
        //printf("draw_brush %f,%f width=%d opacity=%d\n", pos.x, pos.y, brushwidth, opacity);

        // Enforce minimum brush size.
        if (brushwidth<2) brushwidth = 2;

        // Calculate drawing rectangle.
        float halfwidth = brushwidth/2;
        float p0x = pos.x - halfwidth;
        float p0y = pos.y - halfwidth;
        float p1x = pos.x + halfwidth + 1;
        float p1y = pos.y + halfwidth + 1;

        int x0 = int(max(min(p0x, p1x), 0.0f));
        int x1 = int(min(max(p0x, p1x), float(width)));
        int y0 = int(max(min(p0y, p1y), 0.0f));
        int y1 = int(min(max(p0y, p1y), float(height)));

        // Accumulate dirty regions.
        strokemin = Pos::create_from_min(strokemin, Pos(x0, y0));
        strokemax = Pos::create_from_max(strokemax, Pos(x1, y1));
        dirtymin = Pos::create_from_min(dirtymin, Pos(x0, y0));
        dirtymax = Pos::create_from_max(dirtymax, Pos(x1, y1));

        // Calculate interpolation constants
        float db = (BrushType::DIST_TABLE_WIDTH-1) / float(brushwidth);

        float xb = max(0.0f, BrushType::DIST_TABLE_CENTER - (pos.x - x0) * db);
        float yb = max(0.0f, BrushType::DIST_TABLE_CENTER - (pos.y - y0) * db);

        // Select which line of the brush-lookup-table to use that most closely matches the current brush width
        int brushidx = int(float(BrushType::BRUSH_TABLE_HEIGHT) / brushwidth);

        // Interpolate the distance table over the area. For each pixel find the distance, and look the 
        // brush-intensity up in the brush-table
        if (drawtype == DRAWBRUSH_TYPE_NORMAL)
        {
            for (int y = y0; y < y1; y++)
            {
                float x2b = xb;
                for (int x = x0; x < x1; x++)
                {
                    // Find brush-intensity and mulitply that with incoming opacity
                    int lookup = BrushType::distance_tbl[int(x2b)][int(yb)];
                    int intensity = fixed_scale(Brush::brush_type[brush.type].intensity_tbl[lookup][brushidx], opacity);

                    // New Alpha = Brush Intensity + Old Alpha - (Brush Intensity * Old Alpha)
                    // Also make sure the result is clamped to the incoming opacity and isn't lower than the alpha 
                    // already stored
                    int base = alpha[y*width+x];
                    int a = max(min(intensity + base - ((intensity * base) >> 8), opacity), base);
                    alpha[y*width+x] = a;

                    Color i = Color::create_from_a8r8g8b8(image_backup[y*width+x]);
                    i = Color::create_from_lerp(brush.color, i, a);
                    image[y*width+x] = i.get_a8r8g8b8();

                    x2b += db;
                }
                yb += db;
            }
        }
        else if (drawtype == DRAWBRUSH_TYPE_GETCOLOR)
        {
            // Calculate color from a weighted average of the area that the brush touches.
            Color c(0, 0, 0, 0);
            for (int y = y0; y < y1; y++)
            {
                float x2b = xb;
                for (int x = x0; x < x1; x++)
                {
                    int lookup = BrushType::distance_tbl[int(x2b)][int(yb)];
                    int intensity = fixed_scale(Brush::brush_type[brush.type].intensity_tbl[lookup][brushidx], opacity);
                    Color i = Color::create_from_a8r8g8b8(image[y*width+x]);
                    c.r += i.r * intensity;
                    c.g += i.g * intensity;
                    c.b += i.b * intensity;
                    c.a += intensity;
                    x2b += db;
                }
                yb += db;
            }
            brush.color.r = c.r / c.a;
            brush.color.g = c.g / c.a;
            brush.color.b = c.b / c.a;
        }
    }

    // Return the color underneath the pos.
    Color pickup_color(const Pos& pos)
    {
        int x = int(max(min(pos.x, float(width)), 0.0f));
        int y = int(max(min(pos.y, float(height)), 0.0f));
        return Color::create_from_a8r8g8b8(image[y*width+x]);
    }

    //---------------------------------------------------------------------------------------------
    // Playback
    // 
    // These allow the canvas to be treated like a VCR, playing back and rewinding drawing commands
    // to recreate the state of the canvas at different times.

    void add_command(const DrawCommand& cmd)
    {
        commands.push_back(cmd);
        modified = true;
    }

    void play_command(const DrawCommand& cmd, bool add)
    {
        if (cmd.type == DrawCommand::TYPE_DRAW)
        {
#ifdef CANVAS_DEBUG_COMMANDS
            printf("TYPE_DRAW x=%f y=%f pressure=%d\n", cmd.pos.x, cmd.pos.y, cmd.pressure);
#endif
            Pos relpos = cmd.pos * Pos(width, height);
            bool forcedraw = !add;  // If we are not adding, we are playing back
            command_draw(relpos, cmd.pressure, forcedraw);
        }
        else if (cmd.type == DrawCommand::TYPE_DRAWEND)
        {
#ifdef CANVAS_DEBUG_COMMANDS
            printf("TYPE_DRAWEND pressure=%d\n", cmd.pressure);
#endif
            //if self.stroke and (self.lastpos.x != self.lastorgpos.x or self.lastpos.y != self.lastorgpos.y):
            //    self.command_draw(self.lastorgpos, cmd.pressure, add)
            command_enddraw();
        }
        else if (cmd.type == DrawCommand::TYPE_COLORCHANGE)
        {
#ifdef CANVAS_DEBUG_COMMANDS
            printf("TYPE_COLORCHANGE flipx=%d flipy=%d r=%d g=%d b=%d a=%d\n", cmd.flipx, cmd.flipy, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
#endif
            if (cmd.flipx || cmd.flipy)
            {
                // fixme
                //pygame.transform.flip(self.image, cmd.flipx, cmd.flipy)
            }
            else
                brush.color = cmd.color;
        }
        else if (cmd.type == DrawCommand::TYPE_SIZECHANGE)
        {
#ifdef CANVAS_DEBUG_COMMANDS
            printf("TYPE_SIZECHANGE size=%f control=%d type=%d opacity=%f\n", cmd.size, cmd.brush_control, cmd.brush_type, cmd.opacity);
#endif
            brush.size = int(cmd.size * width);
            if (brush.size < 2)
                brush.size = 2;
            brush.control = cmd.brush_control;
            brush.type = cmd.brush_type;
            if (cmd.opacity > 0)
                brush.opacity = cmd.opacity;
        }

#ifdef CANVAS_DEBUG_COMMANDS
        fflush(stdout);
#endif

        if (add)
            add_command(cmd);
    }

    bool playback_done()
    {
        return playback < 0 || playback >= (int)commands.size();
    }

    int playback_length()
    {
        return (int)commands.size();
    }

    int playback_pos()
    {
        return playback;
    }

    void start_playback()
    {
        command_enddraw();
        clear_image();
        playback = 0;
        playing = true;
    }

    void pause_playback()
    {
        playing = false;
    }

    void resume_playback()
    {
        playing = true;
    }

    void stop_playback()
    {
        command_enddraw();
        playback = -1;
        playing = false;
    }

    void finish_playback()
    {
        while (!playback_done())
            play_command(commands[playback++], false);
    }

    // This is used to avoid leaving the playback state in the middle of a stroke.
    void playback_finish_stroke()
    {
        while (stroke && !playback_done())
            play_command(commands[playback++], false);
    }

    void playback_to(int pos)
    {
        while (playback < pos && !playback_done())
            play_command(commands[playback++], false);
    }

    void playback_step_to(int pos)
    {
        if (playback < pos && !playback_done())
            play_command(commands[playback++], false);
    }

    // Same as playback_to, except it breaks if more than timeout seconds are taken.
    void playback_to_timed(int pos, float timeout)
    {
        clock_t start = clock();
        clock_t end = (clock_t)(start + timeout * CLOCKS_PER_SEC);
        printf("start: %d end: %d CLOCKS_PER_SEC: %d\n", (int)start, (int)end, (int)CLOCKS_PER_SEC);
        while (playback < pos && !playback_done() && clock() < end)
            play_command(commands[playback++], false);
        //if (clock() > end)
        //      printf("killed by timeout.\n");
    }

    void set_playback_speed(int speed)
    {
        playback_speed = speed;
    }

    void truncate_at_playback()
    {
        commands.resize(playback+1);
    }

    void update_playback()
    {
        if (playing)
        {
            for (int i = 0; i < playback_speed; i++)
            {
                if (!playback_done())
                    play_command(commands[playback++], false);
            }
        }
    }

    int get_num_commands()
    {
        return commands.size();
    }

    void play_range(int from, int to)
    {
        for (int i = from; i < to; i++)
            play_command(commands[i], false);
    }

    //---------------------------------------------------------------------------------------------
    // Blit
    // 
    // Draws a region of the canvas into a GdkImage for display on the screen, with optional scaling
    // and darkening.

    inline
    void to_pixel(guint src, depth16_t *dst)
    {
        guint r = (((src>>16)&0xff)>>3)<<11;
        guint g = (((src>> 8)&0xff)>>2)<<5;
        guint b = (((src>> 0)&0xff)>>3);
        *dst = r|g|b;
    }

    inline
    void to_pixel(guint src, depth24_t *dst)
    {
        *dst = src & 0xffffff;
    }

    struct scale1_t
    {
        template <typename pixel_t> inline static
        void fill_pixel(pixel_t **rows, pixel_t value)
        {
            *rows[0]++ = value;
        }
    };

    struct scale2_t
    {
        template <typename pixel_t> inline static
        void fill_pixel(pixel_t **rows, pixel_t value)
        {
            *rows[0]++ = value; *rows[0]++ = value;
            *rows[1]++ = value; *rows[1]++ = value;
        }
    };

    struct scale4_t
    {
        template <typename pixel_t> inline static 
        void fill_pixel(pixel_t **rows, pixel_t value)
        {
            *rows[0]++ = value; *rows[0]++ = value;
            *rows[0]++ = value; *rows[0]++ = value;
            *rows[1]++ = value; *rows[1]++ = value;
            *rows[1]++ = value; *rows[1]++ = value;
            *rows[2]++ = value; *rows[2]++ = value;
            *rows[2]++ = value; *rows[2]++ = value;
            *rows[3]++ = value; *rows[3]++ = value;
            *rows[3]++ = value; *rows[3]++ = value;
        }
    };

    struct scale8_t
    {
        template <typename pixel_t> inline static
        void fill_pixel(pixel_t **rows, pixel_t value)
        {
            *rows[0]++ = value; *rows[0]++ = value;
            *rows[0]++ = value; *rows[0]++ = value;
            *rows[0]++ = value; *rows[0]++ = value;
            *rows[0]++ = value; *rows[0]++ = value;
            *rows[1]++ = value; *rows[1]++ = value;
            *rows[1]++ = value; *rows[1]++ = value;
            *rows[1]++ = value; *rows[1]++ = value;
            *rows[1]++ = value; *rows[1]++ = value;
            *rows[2]++ = value; *rows[2]++ = value;
            *rows[2]++ = value; *rows[2]++ = value;
            *rows[2]++ = value; *rows[2]++ = value;
            *rows[2]++ = value; *rows[2]++ = value;
            *rows[3]++ = value; *rows[3]++ = value;
            *rows[3]++ = value; *rows[3]++ = value;
            *rows[3]++ = value; *rows[3]++ = value;
            *rows[3]++ = value; *rows[3]++ = value;
            *rows[4]++ = value; *rows[4]++ = value;
            *rows[4]++ = value; *rows[4]++ = value;
            *rows[4]++ = value; *rows[4]++ = value;
            *rows[4]++ = value; *rows[4]++ = value;
            *rows[5]++ = value; *rows[5]++ = value;
            *rows[5]++ = value; *rows[5]++ = value;
            *rows[5]++ = value; *rows[5]++ = value;
            *rows[5]++ = value; *rows[5]++ = value;
            *rows[6]++ = value; *rows[6]++ = value;
            *rows[6]++ = value; *rows[6]++ = value;
            *rows[6]++ = value; *rows[6]++ = value;
            *rows[6]++ = value; *rows[6]++ = value;
            *rows[7]++ = value; *rows[7]++ = value;
            *rows[7]++ = value; *rows[7]++ = value;
            *rows[7]++ = value; *rows[7]++ = value;
            *rows[7]++ = value; *rows[7]++ = value;
        }
    };

    template <typename pixel_t, typename scale_t, int scale> inline
    void blit(GdkImage *img, int src_x, int src_y, int dest_x,
            int dest_y, int dest_w, int dest_h, bool overlay)
    {
        pixel_t *pixels = (pixel_t*)img->mem;
        int pitch = img->bpl/sizeof(pixel_t);
        
        // Clip rectangle.
        // Clip destination rectangle.  Source clipping is handled per pixel.
        if (dest_x < 0)
        {
            dest_w += dest_x;
            dest_x = 0;
        }
        if (dest_y < 0)
        {
            dest_h += dest_y;
            dest_y = 0;
        }
        if (dest_x + dest_w > img->width-scale)
            dest_w = (img->width-scale) - dest_x;
        if (dest_y + dest_h > img->height-scale)
            dest_h = (img->height-scale) - dest_y;

        int csy = src_y;
        for (int cdy = dest_y; cdy < dest_y+dest_h; cdy += scale)
        {
            pixel_t* __restrict rows[scale];

            rows[0] = &pixels[cdy*pitch+dest_x];
            for (int i = 1; i < scale; ++i) rows[i] = rows[i-1] + pitch;

            if (csy < 0 || csy >= height) 
            {
                for (int cdx = 0; cdx < dest_w; cdx += scale)
                {
                    pixel_t p = 0;
                    scale_t::fill_pixel((pixel_t**)rows, p);
                }
            }
            else
            {
                unsigned int* __restrict src = &image[csy*width+src_x];

                int cdx = 0;
                int csx = src_x;

                while (csx < 0 && cdx < dest_w)
                {
                    pixel_t p = 0;
                    scale_t::fill_pixel((pixel_t**)rows, p);
                    src++;
                    csx++;
                    cdx += scale;
                }
                
                if (overlay)
                {
                    while (csx < width && cdx < dest_w)
                    {
                        unsigned int p = *src;
                        p &= ~0x03030303;
                        p >>= 2;
                        pixel_t rgb;
                        to_pixel(p, &rgb);
                        scale_t::fill_pixel((pixel_t**)rows, rgb);
                        src++;
                        csx++;
                        cdx += scale;
                    }
                }
                else
                {
                    while (csx < width && cdx < dest_w)
                    {
                        pixel_t rgb;
                        to_pixel(*src, &rgb);
                        scale_t::fill_pixel((pixel_t**)rows, rgb);
                        src++;
                        csx++;
                        cdx += scale;
                    }
                }
                
                while (cdx < dest_w)                
                {
                    pixel_t p = 0;
                    scale_t::fill_pixel((pixel_t**)rows, p);
                    src++;
                    csx++;
                    cdx += scale;
                }
            }

            csy++;
        }
    }

    template <typename scale_t, int scale> inline
    void blit_x(GdkImage *img, int src_x, int src_y, int dest_x, int dest_y,
            int dest_w, int dest_h, bool overlay)
    {
        if (img->depth == 16)
            blit<depth16_t, scale_t, scale>(img, src_x, src_y, dest_x, dest_y,
                    dest_w, dest_h, overlay);
        else
            blit<depth24_t, scale_t, scale>(img, src_x, src_y, dest_x, dest_y,
                    dest_w, dest_h, overlay);
    }

    void blit_1x(GdkImage* img, int src_x, int src_y, int dest_x, int dest_y,
            int dest_w, int dest_h, bool overlay)
    {
        blit_x<scale1_t, 1> (img, src_x, src_y, dest_x, dest_y, dest_w, dest_h,
                overlay);
    }

    void blit_2x(GdkImage* img, int src_x, int src_y, int dest_x, int dest_y,
            int dest_w, int dest_h, bool overlay)
    {
        blit_x<scale2_t, 2> (img, src_x, src_y, dest_x, dest_y, dest_w, dest_h,
                overlay);
    }

    void blit_4x(GdkImage* img, int src_x, int src_y, int dest_x, int dest_y,
            int dest_w, int dest_h, bool overlay)
    {
        blit_x<scale4_t, 4> (img, src_x, src_y, dest_x, dest_y, dest_w, dest_h,
                overlay);
    }


    void blit_8x(GdkImage* img, int src_x, int src_y, int dest_x, int dest_y,
            int dest_w, int dest_h, bool overlay)
    {
        blit_x<scale8_t, 8> (img, src_x, src_y, dest_x, dest_y, dest_w, dest_h,
                overlay);
    }

    //---------------------------------------------------------------------------------------------
    // Videopaint
    // 
    // This code currently attempts to track a green object in the webcam and treat it as a
    // mouse cursor, using the detected size to control the brush size.

    void downsize_video(unsigned int* dest_pixels, GstBuffer* buf, int vwidth, int vheight)
    {
        if (vwidth != VIDEO_WIDTH*8 || vheight != VIDEO_HEIGHT*8 || buf->size != vwidth*vheight*sizeof(unsigned short))
        {
            printf("Invalid Gst video buffer size %d (%dx%d)\n", buf->size, vwidth, vheight);
            return;
        }

        unsigned int* source_pixels = (unsigned int*)buf->data;

        for (int y = 0; y < VIDEO_HEIGHT; y++)
        {
            unsigned int* __restrict src = &source_pixels[(y*4)*vwidth];
            unsigned int* __restrict dest = &dest_pixels[y*VIDEO_WIDTH];
            for (int x = 0; x < VIDEO_WIDTH; x++)
            {
                *dest = *src;
                src += 4;
                dest++;
            }
        }
    }

    void videopaint_motion(GstBuffer* buf, int vwidth, int vheight)
    {
        downsize_video(image_video[1], buf, vwidth, vheight);

        unsigned int* pixels0 = image_video[0];
        unsigned int* pixels1 = image_video[1];
        double cx = 0, cy = 0, cnt = 0;
        for (int y = 0; y < VIDEO_HEIGHT; y++)
        {
            unsigned int* __restrict row = &pixels1[y*VIDEO_WIDTH];
            unsigned int* destrow = &pixels0[y*VIDEO_WIDTH];
            for (int x = 0; x < VIDEO_WIDTH; x++)
            {
                // Convert YUYV to HSV
                Color c = Color::yuv_to_hsv(*row);
                // Threshold green
                if ((c.r > 80) && (c.r < 150) && (c.g > 100)) {
                    destrow[0] = 0xffff;
                    cnt++;
                    cx += (80-x);
                    cy += y;
                } else {
                    destrow[0] = 0;
                }
                row++;
                destrow++;
            }
        }

        if (cnt > 0)
        {
            cx /= cnt;
            cy /= cnt;
            // The mouse coordinates are scaled somewhat, as the nature of the video processing causes the blob to leave
            // the screen partially and become smaller (and less significant) towards the edges.
            videopaint_pos = Pos(
                map_range(cx, 0, VIDEO_WIDTH, -0.2f, 1.2f),
                map_range(cy, 0, VIDEO_HEIGHT, -0.2f, 1.2f));
            // todo- estimate size
            videopaint_pressure = map_range(cnt, 0, (VIDEO_WIDTH*VIDEO_HEIGHT)/8, 0, 255);
            // Mark mouse pixel in red.
                        int rx = int(cx);
                        int ry = int(cy);
                        unsigned short red = Color(255,0,0,0).get_r5g6b5();
            for (int x = -3; x <= 3; x++)
                for (int y = -3; y <= 3; y++)
                    image_video[0][ry+y*VIDEO_WIDTH+rx+x] = red;
        }
    }

    void blit_videopaint(GdkImage* img)
    {
                unsigned short* pixels = (unsigned short*)img->mem;
                int pitch = img->bpl/sizeof(unsigned short);
                
                for (int y = 0; y < VIDEO_HEIGHT; y++)
                {
                        unsigned int* __restrict src = &image_video[0][y*VIDEO_WIDTH];
                        unsigned short* __restrict dest = &pixels[y*pitch];
                        for (int x = 0; x < VIDEO_WIDTH; x++)
                        {
                                unsigned int p = *src++;
                                unsigned int r = (((p>>16)&0xff)>>3)<<11;
                                unsigned int g = (((p>> 8)&0xff)>>2)<<5;
                                unsigned int b = (((p>> 0)&0xff)>>3);
                                unsigned int rgb = r|g|b;
                                *dest++ = rgb;
                        }
                }
    }

    //---------------------------------------------------------------------------------------------
    // Reference image
    // 
    // The reference image is a snapshot taken by the webcam that can transparently displayed over the canvas.
    // Note that painting currently cannot take place while the reference image is shown.

    void set_reference_buffer(GstBuffer* buf, int vwidth, int vheight)
    {
        if (vwidth != REFERENCE_WIDTH || vheight != REFERENCE_HEIGHT || buf->size != vwidth*vheight*sizeof(unsigned short))
        {
            printf("Invalid Gst reference buffer size %d\n", buf->size);
            return;
        }
        memcpy(image_reference, buf->data, min(size_t(buf->size), size_t(REFERENCE_WIDTH*REFERENCE_HEIGHT*sizeof(unsigned short))));
    }

    void render_reference_overlay()
    {
        // Uses image_backup to blend over the canvas without affecting the contents of the canvas.
        // Scales up from whatever REFERENCE_WIDTH/REFERENCE_HEIGHT are to the canvas size.
        int dx = (1<<16) * REFERENCE_WIDTH / width;
        int dy = (1<<16) * REFERENCE_HEIGHT / height;
        int ry = 0;
        for (int y = 0; y < height; y++, ry += dy)
        {
            int rx = 0;
            for (int x = 0; x < width; x++, rx += dx)
            {
                Color r = Color::create_from_r5g6b5(image_reference[(ry>>16)*REFERENCE_WIDTH+(rx>>16)]);
                r = Color::create_from_yuv(r.r, r.g, r.b);
                Color b = Color::create_from_a8r8g8b8(image_backup[y*width+x]);
                image[y*width+x] = Color::create_from_lerp(r, b, 192).get_a8r8g8b8();
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // Overlay
    // 
    // These functions basically just temporarily darken the entire canvas so that a PyGTK overlay 
    // like the brush controls can be drawn on top, and look like it has a translucent black background.

    void render_overlay()
    {
        // Since the image is backed up in image_backup, it's ok to destroy the contents of image since
        // clear_overlay will just restore it from image_backup.
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                unsigned int i = image_backup[y*width+x];
                i &= ~0x03030303;
                i >>= 2;
                image[y*width+x] = i;
            }
    }

    void clear_overlay()
    {
        memcpy(image, image_backup, width*height*sizeof(unsigned int));
    }

    //---------------------------------------------------------------------------------------------
    // Load & Save

    void upgrade_drw_header(DRW_Header* hdr, DRW_Command* cmds)
    {
        if (hdr->version == DRW_Header::ID) // Paying for old bug
            hdr->version = 1002;

        if (hdr->version < DRW_VERSION)
        {
            for(int i = 0; i < hdr->ncommands; i++)
            {
                DRW_Command* cmd = &cmds[i];
                if (hdr->version < 1001)
                {
                    if (cmd->type == DrawCommand::TYPE_DRAW)
                    {
                        cmd->x = int(round(cmd->x * 1024.0f / 2047.0f + 512.0f));
                        cmd->x = int(round(cmd->y * 1024.0f / 2047.0f + 512.0f));
                    }
                }

                if (hdr->version < 1002)
                {
                    if (cmd->type == DrawCommand::TYPE_SIZECHANGE)
                    {
                        int type = (cmd->brushtype << 2) | cmd->brushcontrol;
                        switch(type)
                        {
                        case 0: cmd->brushtype = BrushType::BRUSHTYPE_HARD; cmd->brushcontrol = Brush::BRUSHCONTROL_VARIABLEOPACITY; break;
                        case 2: cmd->brushtype = BrushType::BRUSHTYPE_SOFT; cmd->brushcontrol = Brush::BRUSHCONTROL_VARIABLEOPACITY; break;
                        case 4: cmd->brushtype = BrushType::BRUSHTYPE_HARD; cmd->brushcontrol = 0; break;
                        case 6: cmd->brushtype = BrushType::BRUSHTYPE_SOFT; cmd->brushcontrol = 0; break;
                        }
                        cmd->size -= (1 << 6);
                    }
                }
            }
            hdr->version = DRW_VERSION;
        }
    }

    bool load(const char* filename)
    {
        FILE* drwfile = fopen(filename, "rb");
        if (!drwfile)
            return false;

        int r;

        DRW_Header header;
        r = fread(&header, 1, sizeof(DRW_Header), drwfile);

        // Backward compatible code for early versions when there was no header and the filesize is used
        // to determine the number of commands.
        if (header.id != DRW_Header::ID)
        {
            header.colorsversion_initial = 0;
            fseek(drwfile, 0, SEEK_END);
            header.ncommands = ftell(drwfile) / 4;
            fseek(drwfile, 0, SEEK_SET);
        }

        DRW_Command* cmds = (DRW_Command*)malloc(header.ncommands*sizeof(DRW_Command));
        r = fread(cmds, 1, header.ncommands*sizeof(DRW_Command), drwfile);
    
        fclose(drwfile);

        upgrade_drw_header(&header, cmds);

        clear();
        convert_from_drw(cmds, 0, header.ncommands);

        free(cmds);

        return true;
    }
    
    bool save(const char* filename)
    {
        FILE* drwfile = fopen(filename, "wb");
        if (!drwfile)
            return false;

        int r;

        DRW_Header header;
        header.id = DRW_Header::ID;
        header.version = DRW_VERSION;
        header.colorsversion_initial = DRW_VERSION;
        header.colorsversion_saved = DRW_VERSION;
        header.strokes = 0;
        header.time = 0;
        header.timessaved = 0;
        header.ncommands = commands.size();
        r = fwrite(&header, 1, sizeof(DRW_Header), drwfile);

        DRW_Command* cmds;
        convert_to_drw(&cmds, 0, commands.size());

        r = fwrite(cmds, sizeof(DRW_Command), commands.size(), drwfile);
        free(cmds);

        fclose(drwfile);

        return true;
    }

    void convert_from_drw(DRW_Command* cmds, int start, int ncommands)
    {
        commands.resize(start+ncommands);
        for (int i = 0; i < ncommands; i++)
        {
            DRW_Command* drw = &cmds[i];
            DrawCommand* cmd = &commands[start+i];

            cmd->type = drw->type;
            cmd->pos.x = (drw->x-512.0f) / 1024.0f;
            cmd->pos.y = (drw->y-512.0f) / 1024.0f;
            cmd->pressure = drw->alpha;

            cmd->color = Color::create_from_a8r8g8b8(drw->col);
            cmd->flipx = drw->flipx;
            cmd->flipy = drw->flipy;

            cmd->brush_control = drw->brushcontrol;
            cmd->brush_type = drw->brushtype;
            cmd->size = drw->size / float(1 << 15);
            cmd->opacity = drw->opacity / 255.0f;
        }
    }

    void convert_to_drw(DRW_Command** cmds, int start, int ncommands)
    {
        *cmds = (DRW_Command*)malloc(sizeof(DRW_Command) * ncommands);

        for (int i = 0; i < ncommands; i++)
        {
            DRW_Command* drw = &(*cmds)[i];
            DrawCommand* cmd = &commands[start+i];

            drw->type = cmd->type;

            if (cmd->type == DrawCommand::TYPE_DRAW)
            {
                drw->x = int((cmd->pos.x*1024)+512);
                drw->y = int((cmd->pos.y*1024)+512);
                drw->alpha = cmd->pressure;
            }
            else if (cmd->type == DrawCommand::TYPE_DRAWEND)
            {
                drw->alpha = cmd->pressure;
            }
            else if (cmd->type == DrawCommand::TYPE_COLORCHANGE)
            {
                drw->flipx = cmd->flipx;
                drw->flipy = cmd->flipy;
                drw->col = cmd->color.get_a8r8g8b8();
            }
            else if (cmd->type == DrawCommand::TYPE_SIZECHANGE)
            {
                drw->brushcontrol = cmd->brush_control;
                drw->brushtype = cmd->brush_type;
                drw->size = int(cmd->size * float(1<<15));
                drw->opacity = int(cmd->opacity * 255.0f);
            }
        }
    }

    DrawCommandBuffer send_drw_commands(int start, int ncommands)
    {
        DrawCommandBuffer buf;
        buf.ncommands = ncommands;
        convert_to_drw((DRW_Command**)&buf.cmds, start, ncommands);
        return buf;
    }

    void receive_drw_commands(const DrawCommandBuffer& buf, int start)
    {
        convert_from_drw((DRW_Command*)buf.cmds, start, buf.ncommands);
    }
};

#endif


# Copyright 2008 by Jens Andersson and Wade Brainerd.  
# This file is part of Colors! XO.
#
# Colors is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# Colors is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with Colors.  If not, see <http://www.gnu.org/licenses/>.
#!/usr/bin/env python
"""Colors! XO painting activity.  Based on Colors! by Collecting Smiles."""

# Note- I had to rename the self.canvas object *self.easel* for now, because the stupid sugar.window.Window
# super class of Activity has its own "canvas" member.  If anyone has a better name, feel free to change it.

# Reference links:
# 
# Colors!    - http://wiki.laptop.org/go/Colors!
# PyGTK      - http://www.pygtk.org/pygtk2reference/
# Sugar      - http://dev.laptop.org/~cscott/joyride-1477-api/
# GStreamer  - http://pygstdocs.berlios.de/pygst-reference/index.html
# Activities - http://wiki.laptop.org/go/Sugar_Activity_Tutorial
# Sharing    - http://wiki.laptop.org/go/Shared_Sugar_Activities

# Import standard Python modules.
import logging, os, math, time, copy, json
from gettext import gettext as _

# Import the C++ component of the activity.
from colorsc import *

# Import PyGTK.
import gobject, pygtk, gtk, pango
# Needed to avoid thread crashes with GStreamer
gobject.threads_init()  

# Import DBUS and mesh networking modules.
import dbus, telepathy, telepathy.client
from dbus import Interface
from dbus.service import method, signal
from dbus.gobject_service import ExportedGObject
from sugar.presence.tubeconn import TubeConnection
from sugar.presence import presenceservice

# Import Sugar UI modules.
from sugar import graphics
from sugar.activity import activity
from sugar.graphics import *
from sugar.graphics import toggletoolbutton

# Import GStreamer (for camera access).
import pygst, gst

# Initialize logging.
log = logging.getLogger('Colors run')
log.setLevel(logging.DEBUG)
logging.basicConfig()

# Track memory leaks.
#import gc
#gc.set_debug(gc.DEBUG_LEAK)

# DBUS identifiers are used to uniquely identify the activity for network communcations.
DBUS_IFACE   = "org.laptop.community.Colors"
DBUS_PATH    = "/org/laptop/community/Colors"
DBUS_SERVICE = DBUS_IFACE

# This is the overlay that appears when the user presses the Palette toobar button.  It covers the entire screen,
# and offers controls for brush type, size, opacity, and color.
# 
# The color wheel and triangle are rendered into GdkImage objects by C++ code in palette.h / palette.cpp which
# is compiled into the colorsc module.
class BrushControlsPanel(gtk.HBox):
    PALETTE_SIZE = 500
    PREVIEW_SIZE = 250
    BRUSHTYPE_SIZE = 100

    def __init__ (self):
        gtk.HBox.__init__(self)
        self.set_property("spacing", 20)
        self.set_border_width(50)

        # Locally managed Brush object.
        self.brush = Brush()

        # Palette wheel widget.
        palbox = gtk.VBox()
        self.palette = Palette(BrushControlsPanel.PALETTE_SIZE)
        self.paletteimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), BrushControlsPanel.PALETTE_SIZE, BrushControlsPanel.PALETTE_SIZE)
        self.palette.render_wheel(self.paletteimage)
        self.palette.render_triangle(self.paletteimage)
        self.palettearea = gtk.DrawingArea()
        self.palettearea.set_size_request(BrushControlsPanel.PALETTE_SIZE, BrushControlsPanel.PALETTE_SIZE)
        self.palettearea.add_events(gtk.gdk.POINTER_MOTION_MASK|gtk.gdk.BUTTON_PRESS_MASK|gtk.gdk.BUTTON_RELEASE_MASK)
        self.palettearea.connect('expose-event', self.on_palette_expose)
        self.palettearea.connect('motion-notify-event', self.on_palette_mouse)
        self.palettearea.connect('button-press-event', self.on_palette_mouse)
        self.palettearea.connect('button-release-event', self.on_palette_mouse)
        palbox.pack_start(self.palettearea, False, False)
        self.pack_start(palbox, False)

        # Brush size scrollbar, label and pressure sensitivity checkbox.
        sizebox = gtk.VBox()
        sizelabel = gtk.Label(_('Brush Size'))
        sizebox.pack_end(sizelabel, False)
        self.size = gtk.Adjustment(50, 1, 260, 1, 10, 10)
        self.sizebar = gtk.VScale(self.size)
        self.sizebar.set_property("draw-value", False)
        self.sizebar.set_property("inverted", True)
        self.sizebar.connect('value-changed', self.on_size_change)
        sizebox.pack_end(self.sizebar)
        self.sizecheck = gtk.CheckButton(_('Sensitive'))
        self.sizecheck.connect('toggled', self.on_variable_size_toggle)
        sizebox.pack_end(self.sizecheck, False)
        self.pack_start(sizebox, False)

        # Brush opacity scrollbar, label and pressure sensitivity checkbox.
        opacitybox = gtk.VBox()
        opacitylabel = gtk.Label(_('Brush Opacity'))
        opacitybox.pack_end(opacitylabel, False)
        self.opacity = gtk.Adjustment(0, 0, 1.1, 0.001, 0.1, 0.1)
        self.opacitybar = gtk.VScale(self.opacity)
        self.opacitybar.set_property("draw-value", False)
        self.opacitybar.set_property("inverted", True)
        self.opacitybar.connect('value-changed', self.on_opacity_change)
        opacitybox.pack_end(self.opacitybar)
        self.opacitycheck = gtk.CheckButton(_('Sensitive'))
        self.opacitycheck.connect('toggled', self.on_variable_opacity_toggle)
        opacitybox.pack_end(self.opacitycheck, False)
        self.pack_start(opacitybox, False)

        # Force column scrollbars to be equal width.
        group = gtk.SizeGroup(gtk.SIZE_GROUP_HORIZONTAL)
        group.add_widget(sizebox)
        group.add_widget(opacitybox)

        # Brush preview widget.
        brushbox = gtk.VBox()
        brushbox.set_property("spacing", 20)

        self.preview = BrushPreview(BrushControlsPanel.PREVIEW_SIZE)
        self.previewimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), BrushControlsPanel.PREVIEW_SIZE, BrushControlsPanel.PREVIEW_SIZE)
        self.previewarea = gtk.DrawingArea()
        self.previewarea.set_size_request(BrushControlsPanel.PREVIEW_SIZE, BrushControlsPanel.PREVIEW_SIZE)
        self.previewarea.connect('expose-event', self.on_preview_expose)
        brushbox.pack_start(self.previewarea, False)

        # Brush type selection widgets.
        self.brushbtns = []
        brushtbl = gtk.Table(1, 2)
        brushtbl.set_col_spacings(5)
        brushtbl.attach(self.create_brushtype_widget(BrushType.BRUSHTYPE_SOFT), 0, 1, 0, 1)
        brushtbl.attach(self.create_brushtype_widget(BrushType.BRUSHTYPE_HARD), 1, 2, 0, 1)

        brushbox.pack_start(brushtbl, False)

        self.pack_start(brushbox, False)

        self.in_toggle_cb = False

    def create_brushtype_widget (self, type):
        brusharea = gtk.DrawingArea()
        brusharea.set_size_request(BrushControlsPanel.BRUSHTYPE_SIZE, BrushControlsPanel.BRUSHTYPE_SIZE)
        brusharea.connect('expose-event', self.on_brushtype_expose)
        brusharea.preview = BrushPreview(BrushControlsPanel.BRUSHTYPE_SIZE)
        brusharea.preview.brush.size = int(BrushControlsPanel.BRUSHTYPE_SIZE*0.75)
        brusharea.preview.brush.type = type
        brusharea.preview.brush.color = Color(0,0,0,0)
        brusharea.previewimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), BrushControlsPanel.BRUSHTYPE_SIZE, BrushControlsPanel.BRUSHTYPE_SIZE)
        brusharea.preview.render(brusharea.previewimage)
        brushbtn = gtk.ToggleButton()
        brushbtn.set_image(brusharea)
        brushbtn.connect('toggled', self.on_brushtype_toggle)
        brushbtn.brushtype = type
        self.brushbtns.append(brushbtn)
        return brushbtn

    def set_brush (self, brush):
        self.brush = brush
        self.opacity.set_value(brush.opacity)
        self.size.set_value(brush.size)
        self.palette.set_color(brush.color)
        self.opacitycheck.set_active((brush.control & Brush.BRUSHCONTROL_VARIABLEOPACITY) != 0)
        self.sizecheck.set_active((brush.control & Brush.BRUSHCONTROL_VARIABLESIZE) != 0)
        self.update_brushtype_btns()

    def on_size_change (self, event):
        self.brush.size = int(self.size.get_value())
        self.previewarea.queue_draw()

    def on_opacity_change (self, event):
        self.brush.opacity = self.opacity.get_value()
        self.previewarea.queue_draw()

    def on_variable_size_toggle (self, event):
        if self.sizecheck.get_active():
            self.brush.control |= Brush.BRUSHCONTROL_VARIABLESIZE
        else:
            self.brush.control &= ~Brush.BRUSHCONTROL_VARIABLESIZE

    def on_variable_opacity_toggle (self, event):
        if self.opacitycheck.get_active():
            self.brush.control |= Brush.BRUSHCONTROL_VARIABLEOPACITY
        else:
            self.brush.control &= ~Brush.BRUSHCONTROL_VARIABLEOPACITY

    def on_palette_expose (self, widget, event):
        self.cur_style = self.palettearea.get_style()
        self.cur_gc = self.cur_style.fg_gc[gtk.STATE_NORMAL]

        # Draw palette image.
        self.palette.render_triangle(self.paletteimage)
        self.palettearea.window.draw_image(self.cur_gc, self.paletteimage, 0, 0, 0, 0, -1, -1)

        # Draw circles to indicate selected color.
        # todo- Better looking circles.
        r = int(self.palette.WHEEL_WIDTH*0.75)

        self.cur_gc.foreground = self.palettearea.get_colormap().alloc_color(16384,16384,16384)
        self.cur_gc.line_width = 2

        wheel_pos = self.palette.get_wheel_pos()
        tri_pos = self.palette.get_triangle_pos()

        self.palettearea.window.draw_arc(self.cur_gc, False, int(wheel_pos.x-r/2+2), int(wheel_pos.y-r/2+2), r-4, r-4, 0, 360*64)
        self.palettearea.window.draw_arc(self.cur_gc, False, int(tri_pos.x-r/2+2), int(tri_pos.y-r/2+2), r-4, r-4, 0, 360*64)

        self.cur_gc.foreground = self.palettearea.get_colormap().alloc_color(65535,65535,65535)
        self.cur_gc.line_width = 2

        self.palettearea.window.draw_arc(self.cur_gc, False, int(wheel_pos.x-r/2), int(wheel_pos.y-r/2), r, r, 0, 360*64)
        self.palettearea.window.draw_arc(self.cur_gc, False, int(tri_pos.x-r/2), int(tri_pos.y-r/2), r, r, 0, 360*64)

    def on_palette_mouse (self, widget, event):
        if event.state & gtk.gdk.BUTTON1_MASK:
            widget.grab_focus()
            self.palette.process_mouse(int(event.x), int(event.y))
            self.palettearea.queue_draw()
            self.brush.color = self.palette.get_color()
            self.previewarea.queue_draw()
        if event.type == gtk.gdk.BUTTON_RELEASE:
            self.palette.process_mouse_release()

    def on_preview_expose (self, widget, event):
        self.preview.brush = self.brush  
        self.preview.brush.size = self.brush.size*2  # Mimic 2x canvas scaling.
        self.preview.render(self.previewimage)
        self.previewarea.window.draw_image(widget.get_style().fg_gc[gtk.STATE_NORMAL], self.previewimage, 0, 0, 0, 0, -1, -1)

    def on_brushtype_expose (self, widget, event):
        widget.window.draw_image(widget.get_style().fg_gc[gtk.STATE_NORMAL], widget.previewimage, 0, 0, 0, 0, -1, -1)

    # Manually implemented radio button using ToggleButtons.
    def update_brushtype_btns (self):
        for b in self.brushbtns:
            b.set_active(b.brushtype == self.brush.type)

    def on_brushtype_toggle (self, widget):
        if self.in_toggle_cb:
            return
        self.in_toggle_cb = True

        self.brush.type = widget.brushtype
        self.update_brushtype_btns()
        self.previewarea.queue_draw()

        self.in_toggle_cb = False

# This is the overlay that appears when playing back large numbers of drawing commands.
# It simply shows how much work is left to do and that progress is taking place. 
class ProgressPanel(gtk.VBox):
    def __init__ (self):
        gtk.VBox.__init__(self)
        self.set_border_width(50)

        self.label = gtk.Label()
        self.label.set_markup("<span foreground='white' size='xx-large'>"+_("Working...")+"</span>")
        self.progress = gtk.ProgressBar()
        self.progress.set_fraction(0.5)
        self.progress.set_orientation(gtk.PROGRESS_LEFT_TO_RIGHT)

        vbox = gtk.VBox()
        vbox.set_property("spacing", 20)

        vbox.pack_start(self.label, False)
        vbox.pack_start(self.progress, False)

        self.pack_start(vbox, True, False)

# This is the main Colors! activity class.
# 
# It owns the main application window, the painting canvas, and all the various toolbars and options.
class Colors(activity.Activity, ExportedGObject):
    # Application mode definitions.
    MODE_INTRO     = 0
    MODE_PLAYBACK  = 1
    MODE_CANVAS    = 2
    MODE_SCROLL    = 3
    MODE_PALETTE   = 4
    MODE_REFERENCE = 5

    # Button definitions
    BUTTON_PALETTE    = 1<<0
    BUTTON_REFERENCE  = 1<<1
    BUTTON_VIDEOPAINT = 1<<2
    BUTTON_SCROLL     = 1<<3
    BUTTON_PICK       = 1<<4
    BUTTON_ZOOM_IN    = 1<<5
    BUTTON_ZOOM_OUT   = 1<<6
    BUTTON_SIZE_0     = 1<<7
    BUTTON_SIZE_1     = 1<<8
    BUTTON_SIZE_2     = 1<<9
    BUTTON_SIZE_3     = 1<<10
    BUTTON_TOUCH      = 1<<11

    # Number of drawing steps to execute between progress bar updates.  More updates means faster overall drawing
    # but a less responsive UI.
    PROGRESS_DELTA  = 50

    def __init__ (self, handle):
        activity.Activity.__init__(self, handle)
        self.set_title(_("Colors!"))

        # Uncomment to test out a bunch of the C++ heavy lifting APIs.  Takes awhile on the XO though.
        #self.benchmark()

        # Get activity size. What we really need is the size of the canvasarea, not including the toolbox.
        # This will be figured out on the first paint event, once everything is resized.
        self.width = gtk.gdk.screen_width()
        self.height = gtk.gdk.screen_height()

        # Set up various systems.
        self.init_input()
        self.init_zoom()
        self.init_scroll()

        # Build the toolbar.
        self.build_toolbar()

        # Set up drawing canvas (which is also the parent for any popup widgets like the brush controls).
        self.build_canvas()

        # Build the brush control popup window.
        self.build_brush_controls()

        # Build the progress display popup window.
        self.build_progress()

        # Start camera processing.
        self.init_camera()

        # Set the initial mode to None, it will be set to Intro on the first update.
        self.mode = None

        # Set up mesh networking.
        self.init_mesh()

        # Start the framerate timer.
        gobject.idle_add(self.tick)

        # This has to happen last, because it calls the read_file method when restoring from the Journal.
        self.set_canvas(self.easelarea)
        
        # Reveal the main window (but not the panels).
        self.show_all()
        self.brush_controls.hide()
        self.progress.hide()
        self.overlay_active = False

    #-----------------------------------------------------------------------------------------------------------------
    # User interface construction

    def build_canvas (self):
        # The canvasarea is the main window which covers the entire screen below the toolbar.
        self.easelarea = gtk.Layout()
        self.easelarea.set_size_request(gtk.gdk.screen_width(), gtk.gdk.screen_height())
        self.easelarea.set_flags(gtk.CAN_FOCUS)

        self.set_double_buffered(False)
        self.easelarea.set_double_buffered(False)

        # Set up GTK events for the canvasarea.
        self.easelarea.add_events(gtk.gdk.POINTER_MOTION_MASK|gtk.gdk.POINTER_MOTION_HINT_MASK)
        self.easelarea.add_events(gtk.gdk.BUTTON_PRESS_MASK|gtk.gdk.BUTTON_RELEASE_MASK)
        self.easelarea.add_events(gtk.gdk.KEY_PRESS_MASK|gtk.gdk.KEY_RELEASE_MASK)

        # The actual drawing canvas is at 1/2 resolution, which improves performance by 4x and still leaves a decent
        # painting resolution of 600x400 on the XO.
        self.easel = Canvas(self.width/2, self.height/2)
        self.set_brush(self.easel.brush)

        # The Canvas internally stores the image as 32bit.  When rendering, it scales up and blits into canvasimage, 
        # which is in the native 565 resolution of the XO.  Then canvasimage is drawn into the canvasarea DrawingArea.
        self.easelimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), self.width, self.height)

        # Now that we have a canvas, connect the rest of the events.
        self.easelarea.connect('expose-event', self.on_canvasarea_expose)
        self.easelarea.connect('expose-event', self.on_canvasarea_expose)
        self.easelarea.connect('key-press-event', self.on_key_event)
        self.easelarea.connect('key-release-event', self.on_key_event)
        self.easelarea.connect('button-press-event', self.on_mouse_button)
        self.easelarea.connect('button-release-event', self.on_mouse_button)
        self.easelarea.connect('motion-notify-event', self.on_mouse_motion)

    def build_brush_controls (self):
        self.brush_controls = BrushControlsPanel()
        self.brush_controls.set_size_request(gtk.gdk.screen_width(), gtk.gdk.screen_height())
        self.easelarea.put(self.brush_controls, 0, 0)

    def build_progress (self):
        self.progress = ProgressPanel()
        self.progress.set_size_request(gtk.gdk.screen_width(), gtk.gdk.screen_height())
        self.easelarea.put(self.progress, 0, 0)

    def build_toolbar (self):
        # Painting controls (palette, zoom, etc)
        self.palettebtn = toggletoolbutton.ToggleToolButton('palette')
        self.palettebtn.set_tooltip(_("Palette"))
        self.palettebtn.connect('clicked', self.on_palette)

        # todo- Color picker button, similar semantics to scroll button.

        self.zoomoutbtn = toolbutton.ToolButton('zoom-out')
        self.zoomoutbtn.set_tooltip(_("Zoom Out"))
        self.zoominbtn = toolbutton.ToolButton('zoom-in')
        self.zoominbtn.set_tooltip(_("Zoom In"))

        self.refsep = gtk.SeparatorToolItem()

        self.takerefbtn = toolbutton.ToolButton('take-reference')
        self.takerefbtn.set_tooltip(_("Take Reference Picture"))
        self.takerefbtn.connect('clicked', self.on_take_reference)
        self.take_reference = False

        self.showrefbtn = toggletoolbutton.ToggleToolButton('show-reference')
        self.showrefbtn.set_tooltip(_("Show Reference Picture"))
        self.showrefbtn.connect('clicked', self.on_show_reference)

        self.videopaintsep = gtk.SeparatorToolItem()

        self.videopaintbtn = toggletoolbutton.ToggleToolButton('video-paint')
        self.videopaintbtn.set_tooltip(_("Video Paint"))
        self.videopaintbtn.connect('clicked', self.on_videopaint)
        self.videopaintpreview = gtk.DrawingArea()
        self.videopaintpreview.set_size_request(Canvas.VIDEO_WIDTH, Canvas.VIDEO_HEIGHT)
        self.videopaintpreview.connect('expose-event', self.on_videopaintpreview_expose)
        self.videopaintitem = gtk.ToolItem()
        self.videopaintitem.add(self.videopaintpreview)
        self.videopaintimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), Canvas.VIDEO_WIDTH, Canvas.VIDEO_HEIGHT)
        self.videopaint_enabled = False

        self.clearsep = gtk.SeparatorToolItem()
        self.clearsep.set_expand(True)
        self.clearsep.set_draw(False)

        self.clearbtn = toolbutton.ToolButton('erase')
        self.clearbtn.set_tooltip(_("Start Over"))
        self.clearbtn.connect('clicked', self.on_clear)

        paintbox = gtk.Toolbar()
        paintbox.insert(self.palettebtn, -1)
        paintbox.insert(self.zoomoutbtn, -1)
        paintbox.insert(self.zoominbtn, -1)
        paintbox.insert(self.refsep, -1)
        paintbox.insert(self.takerefbtn, -1)
        paintbox.insert(self.showrefbtn, -1)
        paintbox.insert(self.videopaintsep, -1)
        paintbox.insert(self.videopaintbtn, -1)
        paintbox.insert(self.videopaintitem, -1)
        paintbox.insert(self.clearsep, -1)
        paintbox.insert(self.clearbtn, -1)

        # Playback controls
        self.startbtn = toolbutton.ToolButton('media-playback-start')
        self.startbtn.set_tooltip(_("Start Playback"))
        self.startbtn.connect('clicked', self.on_play)

        self.pausebtn = toolbutton.ToolButton('media-playback-pause')
        self.pausebtn.set_tooltip(_("Pause Playback"))
        self.pausebtn.connect('clicked', self.on_pause)

        self.beginbtn = toolbutton.ToolButton('media-seek-backward')
        self.beginbtn.set_tooltip(_("Skip To Beginning"))
        self.beginbtn.connect('clicked', self.on_skip_begin)

        self.endbtn = toolbutton.ToolButton('media-seek-forward')
        self.endbtn.connect('clicked', self.on_skip_end)
        self.endbtn.set_tooltip(_("Skip To End"))

        # Position bar
        self.playbackpossep = gtk.SeparatorToolItem()
        self.playbackpossep.set_draw(True)

        self.playbackpos = gtk.Adjustment(0, 0, 110, 1, 10, 10)
        self.playbackposbar = gtk.HScale(self.playbackpos)
        self.playbackposbar.connect('value-changed', self.on_playbackposbar_change)
        self.playbackposbar.ignore_change = 0

        self.playbackpositem = gtk.ToolItem()
        self.playbackpositem.set_expand(True)
        self.playbackpositem.add(self.playbackposbar)

        playbox = gtk.Toolbar()
        playbox.insert(self.startbtn, -1)
        playbox.insert(self.pausebtn, -1)
        playbox.insert(self.beginbtn, -1)
        playbox.insert(self.endbtn, -1)
        playbox.insert(self.playbackpossep, -1)
        playbox.insert(self.playbackpositem, -1)

        # Sample files to learn from.  Reads the list from an INDEX file in the data folder.
        samplebox = gtk.Toolbar()
        self.samplebtns = []

        samples = []
        fd = open(activity.get_bundle_path() + '/data/INDEX', 'r')
        try:
            samples = json.read(fd.read())
        finally:
            fd.close()

        log.debug("Samples: %r", samples)
        for s in samples:
            btn = toolbutton.ToolButton('media-playback-start')
            btn.filename = activity.get_bundle_path() + '/data/' + s['drw']
            btn.set_tooltip(s['title'])
            img = gtk.Image()
            img.set_from_file(activity.get_bundle_path() + '/data/' + s['icon'])
            btn.set_icon_widget(img)
            btn.connect('clicked', self.on_sample)
            samplebox.insert(btn, -1)
            self.samplebtns.append(btn)

        toolbar = activity.ActivityToolbox(self)
        toolbar.add_toolbar(_("Paint"),paintbox)
        toolbar.add_toolbar(_("Watch"),playbox)
        toolbar.add_toolbar(_("Learn"),samplebox)
        toolbar.show_all()
        self.set_toolbox(toolbar)

    #-----------------------------------------------------------------------------------------------------------------
    # Camera access
    # 
    # todo- Consider going straight to video4linux2's C API, we aren't really using any features of GStreamer.

    def init_camera (self):
        self.gstpipe = gst.parse_launch("v4l2src ! fakesink name=fakesink signal-handoffs=true")
        self.gstfakesink = self.gstpipe.get_by_name('fakesink')
        self.gstfakesink.connect("handoff", self.on_gst_buffer)
        self.easelarea.connect("destroy", self.on_gst_destroy)

        self.gstpipe.set_state(gst.STATE_PLAYING)

    def on_gst_buffer(self, element, buffer, pad):
        if self.take_reference:
            self.easel.set_reference_buffer(buffer, 640, 480)
            # Bring up the reference screen and update.
            self.showrefbtn.set_active(True)
            self.easel.render_reference_overlay()
            self.flush_entire_canvas()
            self.take_reference = False

        if self.videopaint_enabled:
            self.easel.videopaint_motion(buffer, 640, 480)

    # todo- This isn't working right, for some reason the GST pipe keeps playing after the window is destroyed, leading
    # to Python exceptions in on_gst_buffer.
    def on_gst_destroy (self, widget):
        self.gstpipe.set_state(gst.STATE_NULL)

    #-----------------------------------------------------------------------------------------------------------------
    # Mesh networking
    # 
    # The mesh networking system is a little bit wacky, but works reasonably well at the moment.  It might need to
    # be redone in the future for less stable networking environments.
    # 
    # Each user maintains the current state of the canvas, as well as a 'shared image' state which represents the 
    # 'master' state of the canvas that is shared by all the users.  The command index in the drawing command list 
    # that corresponds to the master state is also recorded.
    # 
    # Each user is allowed to paint 'ahead' of the master state, by appending commands to their local command list.
    # After each stroke, the commands of the stroke are broadcast as the new master state to the other users.
    # 
    # Whenever a new master state is received, it will contain a list of commands that follow the old master state
    # to reach the new one.  The user simply rewinds their canvas to the old master state, replaces their local image
    # with the shared one, and appends the received commands to reach the new master state.
    # 
    # The net effect is that when the user paints something, they broadcast their own commands, and then *receive* 
    # their own commands, rewinding and then playing them back immediately.  So, every users canvas state is simply the
    # sum of all the broadcasts from themselves and the other users.  Since the broadcasts are serialized, every
    # user has the same state all the time.

    def init_mesh (self):
        self.connected = False   # If True, the activity is shared with other users.
        self.initiating = False  # If True, this instance started the activity.  Otherwise, we joined it.

        # Set up the drawing send and receive state.
        # See send_and_receive_draw_commands for more information.
        self.draw_command_sent = 0
        self.draw_command_received = 0
        self.draw_command_queue = DrawCommandBuffer()

        # Get the presence server and self handle.
        self.pservice = presenceservice.get_instance()
        self.owner = self.pservice.get_owner()

        self.connect('shared', self.on_shared)  # Called when the user clicks the Share button in the toolbar.
        self.connect('joined', self.on_join)    # Called when the activity joins a remote activity.

    def on_shared (self, activity):
        self.initiating = True
        self.setup_sharing()

        # Offer a DBus tube that everyone else can connect to.
        self.tubes_chan[telepathy.CHANNEL_TYPE_TUBES].OfferDBusTube(DBUS_SERVICE, {})

        # Cancel the intro if playing.
        self.set_mode(Colors.MODE_CANVAS)

    def on_list_tubes_reply(self, tubes):  # Called by on_join.
        for tube_info in tubes:
            self.on_tube(*tube_info)

    def on_list_tubes_error(self, e):      # Called by on_join.
        pass

    def on_join (self, activity):
        self.initiating = False
        self.setup_sharing()

        # List existing tubes.  There should only be one, which will invoke the on_new_tube callback.
        self.tubes_chan[telepathy.CHANNEL_TYPE_TUBES].ListTubes(
            reply_handler=self.on_list_tubes_reply, error_handler=self.on_list_tubes_error)

        # Limit UI choices for non-host users.
        self.disable_shared_commands()

        # Cancel the intro if playing.
        self.set_mode(Colors.MODE_CANVAS)

    def setup_sharing (self):
        """Called to initialize mesh networking objects when the activity becomes shared (on_shared) or joins an 
           existing shared activity (on_join)."""
        # Cache connection related objects.
        self.conn = self._shared_activity.telepathy_conn
        self.tubes_chan = self._shared_activity.telepathy_tubes_chan
        self.text_chan = self._shared_activity.telepathy_text_chan

        # This will get called as soon as the connection is established.
        self.tubes_chan[telepathy.CHANNEL_TYPE_TUBES].connect_to_signal('NewTube', self.on_tube)

        # Called when a buddy joins us or leaves (does nothing right now).
        self._shared_activity.connect('buddy-joined', self.on_buddy_joined)
        self._shared_activity.connect('buddy-left', self.on_buddy_left)

    def on_tube (self, id, initiator, type, service, params, state):
        """Called by the NewTube callback or the ListTubes enumeration, when a real connection finally exists."""
        if (type == telepathy.TUBE_TYPE_DBUS and service == DBUS_SERVICE):
            # If the new tube is waiting for us to finalize it, do so.
            if state == telepathy.TUBE_STATE_LOCAL_PENDING:
                self.tubes_chan[telepathy.CHANNEL_TYPE_TUBES].AcceptDBusTube(id)

            if not self.connected:
                # Create the TubeConnection object to manage the connection.
                self.tube = TubeConnection(self.conn, 
                    self.tubes_chan[telepathy.CHANNEL_TYPE_TUBES],
                    id, group_iface=self.text_chan[telepathy.CHANNEL_INTERFACE_GROUP])
                ExportedGObject.__init__(self, self.tube, DBUS_PATH)

                # Set up DBUS Signal receiviers.
                self.tube.add_signal_receiver(self.ReceiveHello,        'BroadcastHello',        DBUS_IFACE, path=DBUS_PATH)
                self.tube.add_signal_receiver(self.ReceiveCanvasMode,   'BroadcastCanvasMode',   DBUS_IFACE, path=DBUS_PATH)
                self.tube.add_signal_receiver(self.ReceiveClear,        'BroadcastClear',        DBUS_IFACE, path=DBUS_PATH)
                self.tube.add_signal_receiver(self.ReceiveDrawCommands, 'BroadcastDrawCommands', DBUS_IFACE, path=DBUS_PATH)
                self.tube.add_signal_receiver(self.ReceivePlayback,     'BroadcastPlayback',     DBUS_IFACE, path=DBUS_PATH)

                log.debug("Connected.")
                self.connected = True

                # Announce our presence to the server.
                if not self.initiating:
                    self.BroadcastHello()

    # Notes about DBUS signals:
    # - When you call a @signal function, its implementation is invoked, and the registered callback is invoked on all
    #   the peers (including the one who invoked the signal!).  So it's usually best for the @signal function to do
    #   nothing at all.
    # - The 'signature' describes the parameters to the function.

    @signal(dbus_interface=DBUS_IFACE, signature='')
    def BroadcastHello (self):
        """Broadcast signal sent when a client joins the shared activity."""
        pass
    def ReceiveHello (self):
        if not self.initiating: return  # Only the initiating peer responds to Hello commands.
        log.debug("Received Hello.  Responding with canvas state (%d commands).", self.easel.playback_length())
        self.BroadcastCanvasMode()
        self.BroadcastClear()
        buf = self.easel.send_drw_commands(0, self.easel.get_num_commands())
        self.BroadcastDrawCommands(buf.get_bytes(), buf.ncommands)

    @signal(dbus_interface=DBUS_IFACE, signature='')
    def BroadcastCanvasMode (self):
        """Broadcast signal for forcing clients into Canvas mode."""
        pass
    def ReceiveCanvasMode (self):
        log.debug("ReceiveCanvasMode")
        if self.mode != Colors.MODE_CANVAS:
            self.set_mode(Colors.MODE_CANVAS)

    @signal(dbus_interface=DBUS_IFACE, signature='')
    def BroadcastClear (self):
        """Broadcast signal for clearing the canvas."""
        pass
    def ReceiveClear (self):
        log.debug("ReceiveClear")
        self.easel.clear()
        self.easel.save_shared_image()

    @signal(dbus_interface=DBUS_IFACE, signature='ayi')
    def BroadcastDrawCommands (self, cmds, ncommands):
        """Broadcast signal for drawing commands."""
        pass
    def ReceiveDrawCommands (self, cmds, ncommands):
        log.debug("ReceiveDrawCommands")
        s = "".join(chr(b) for b in cmds)  # Convert dbus.ByteArray to Python string.
        self.draw_command_queue.append(DrawCommandBuffer(s, ncommands))

    @signal(dbus_interface=DBUS_IFACE, signature='bii')
    def BroadcastPlayback (self, playing, playback_pos, playback_speed):
        """Broadcast signal controlling playback.  Not yet used."""
        pass
    def ReceivePlayback (self):
        log.debug("ReceivePlayback")
        if playing:
            if self.mode != Colors.MODE_PLAYBACK:
                self.set_mode(Colors.MODE_PLAYBACK)
        else:
            if self.mode == Colors.MODE_PLAYBACK:
                self.set_mode(Colors.MODE_CANVAS)
        self.easel.playback_to(playback_pos)
        self.easel.set_playback_speed(playback_speed)

    def on_buddy_joined (self, activity, buddy):
        log.debug('Buddy %s joined', buddy.props.nick)

    def on_buddy_left (self, activity, buddy):
        log.debug('Buddy %s left', buddy.props.nick)

    def send_and_receive_draw_commands (self):
        if self.connected:
            # Broadcast drawing commands that were generated by this user since the last call to this function.
            if self.draw_command_sent < self.easel.get_num_commands():
                buf = self.easel.send_drw_commands(self.draw_command_sent, self.easel.get_num_commands()-self.draw_command_sent)
                self.BroadcastDrawCommands(buf.get_bytes(), buf.ncommands)

            # Play any queued draw commands that were received from the host.  If there are any, we first reset the
            # canvas contents back to the last received state and then play them back.
            if self.draw_command_queue.ncommands:
                # Also, we have to save and restore the brush around the queued commands.
                saved_brush = self.easel.brush
                self.easel.receive_drw_commands(self.draw_command_queue, self.draw_command_sent)
                self.easel.restore_shared_image()
                self.easel.play_range(self.draw_command_sent, self.easel.get_num_commands())
                self.easel.save_shared_image()
                self.draw_command_queue.clear()
                self.set_brush(saved_brush)
                self.flush_dirty_canvas()

            # Note that resetting the state above means "undoing" the commands we just broadcast.  We will receive them 
            # again by our ReceiveDrawCommands callback immediately, and will play them back so the user shouldn't notice.
            self.draw_command_sent = self.easel.get_num_commands()

    def disable_shared_commands (self):
        """Disables UI controls which cannot be activated by non-host peers."""
        # Cannot clear the canvas.
        self.clearbtn.set_sensitive(False)

        # Cannot control playback.
        self.startbtn.set_sensitive(False)
        self.pausebtn.set_sensitive(False)
        self.beginbtn.set_sensitive(False)
        self.endbtn.set_sensitive(False)
        self.playbackposbar.set_sensitive(False)

        # Cannot activate sample drawings.
        for s in self.samplebtns:
            s.set_sensitive(False)

    #-----------------------------------------------------------------------------------------------------------------
    # Input code

    def init_input (self):
        self.cur_buttons = 0
        self.pending_press = 0
        self.pending_release = 0
        self.mx = 0
        self.my = 0
        self.pressure = 255
        self.lastmx = 0
        self.lastmy = 0
        self.lastr = 0

    def on_key_event (self, widget, event):
        log.debug("on_key_event: %d", event.keyval)  # Useful for manually working out keyvals for OLPC keys.

        # OLPC keymap is designed to allow left or right handed stylus use, with lots of redundancy.
        # So each major key should appear at least once on each side of the keyboard.
        button = 0

        # Space bar for Palette (todo- need something better!).
        if event.keyval == ord(' '): 
            button = Colors.BUTTON_PALETTE

        # 'r' for Reference (todo- need something better!).
        if event.keyval == ord('r'): 
            button = Colors.BUTTON_REFERENCE

        # 'v' for Videopaint (todo- need something better!).
        if event.keyval == ord('v'): 
            button = Colors.BUTTON_VIDEOPAINT

        # 's' hotkey to save PNG thumbnail of the current canvas as 'thumb.png'.
        if event.keyval == ord('s'): 
            self.save_thumbnail(activity.get_bundle_path() + '/thumb.png')

        # OLPC 'hand' buttons for scrolling.
        if event.keyval == ord('c') or event.keyval == 311 or event.keyval == 312 or event.keyval == ord('s'): 
            button = Colors.BUTTON_SCROLL

        # OLPC 'size' buttons for intensity.
        #if event.keyval == 286: button = Colors.BUTTON_SIZE_0
        #if event.keyval == 287: button = Colors.BUTTON_SIZE_1
        #if event.keyval == 288: button = Colors.BUTTON_SIZE_2
        #if event.keyval == 289: button = Colors.BUTTON_SIZE_3

        # Arrow keys, gamepad 'face' buttons, or 'z' and 'x' for Zoom.
        if event.keyval == ord('z') or event.keyval == 264 or event.keyval == 265 or event.keyval == 273:
            button = Colors.BUTTON_ZOOM_IN
        if event.keyval == ord('x') or event.keyval == 258 or event.keyval == 259 or event.keyval == 274:
            button = Colors.BUTTON_ZOOM_OUT

        # Either Alt key for pick.
        if event.keyval == 313 or event.keyval == 308:
            button = Colors.BUTTON_PICK

        if button != 0:
            if event.type == gtk.gdk.KEY_PRESS:
                self.pending_press = self.pending_press | button
            else:
                self.pending_release = self.pending_release | button

    def on_mouse_button(self, widget, event):
        if self.overlay_active:
            return
        if event.button == 1:
            if event.type == gtk.gdk.BUTTON_PRESS:
                self.pending_press = self.pending_press | Colors.BUTTON_TOUCH
            if event.type == gtk.gdk.BUTTON_RELEASE:
                self.pending_release = self.pending_release | Colors.BUTTON_TOUCH
        #else:
        #    if event.type == gtk.gdk.BUTTON_PRESS:
        #        self.pending_press = self.pending_press | Colors.BUTTON_PALETTE
        #    if event.type == gtk.gdk.BUTTON_RELEASE:
        #        self.pending_release = self.pending_release | Colors.BUTTON_PALETTE
        if not widget.is_focus():
            widget.grab_focus()
        self.update()
        return True

    def on_mouse_motion (self, widget, event):
        if self.overlay_active:
            return
        # Process mouse event normally.
        if event.is_hint:
            x, y, state = event.window.get_pointer()
        else:
            x = event.x
            y = event.y
            state = event.state
        self.mx = int(x)
        self.my = int(y)
        if not widget.is_focus():
            widget.grab_focus()
        self.update()
        self.flush_cursor()
        return True

    def update_input (self):
        buttons = self.cur_buttons

        buttons = buttons | self.pending_press
        self.pending_press = 0

        buttons = buttons & ~self.pending_release
        self.pending_release = 0

        hold = buttons & self.cur_buttons

        if self.cur_buttons != buttons:
            changed = self.cur_buttons ^ buttons
            press = changed & buttons
            release = changed & self.cur_buttons
            self.cur_buttons = buttons
            if press != 0:
                self.on_press(press)
            if release != 0:
                self.on_release(release)

        if hold != 0:
            self.on_hold(hold)

    def on_press (self, button):
        if button & Colors.BUTTON_ZOOM_IN:
            self.zoom_in()
            return

        if button & Colors.BUTTON_ZOOM_OUT:
            self.zoom_out()
            return

        if button & Colors.BUTTON_VIDEOPAINT:
            self.videopaintbtn.set_active(not self.videopaint_enabled)
            return

        if self.mode == Colors.MODE_CANVAS:
            if button & Colors.BUTTON_PALETTE:
                self.set_mode(Colors.MODE_PALETTE)
                return
            if button & Colors.BUTTON_REFERENCE:
                self.set_mode(Colors.MODE_REFERENCE)
                return
            if button & Colors.BUTTON_SCROLL:
                self.set_mode(Colors.MODE_SCROLL)
                return
    
        if self.mode == Colors.MODE_PALETTE:
            if button & Colors.BUTTON_PALETTE:
                self.set_mode(Colors.MODE_CANVAS)
                return

        if self.mode == Colors.MODE_REFERENCE:
            if button & Colors.BUTTON_REFERENCE:
                self.set_mode(Colors.MODE_CANVAS)
                return

    def on_release (self, button):
        if self.mode == Colors.MODE_SCROLL:
            if button & Colors.BUTTON_SCROLL:
                self.set_mode(Colors.MODE_CANVAS)
                return

    def on_hold (self, button):
        pass

    #-----------------------------------------------------------------------------------------------------------------
    # Scroll code
    # 
    # todo- Scrolling is currently broken.  It needs to be bound to a key and the issues need to be fixed.

    def init_scroll (self):
        self.scroll = Pos(0,0)
        self.scrollref = None

    def scroll_to (self, pos):
        self.scroll = pos

        # Clamp scroll position to within absolute limits.
        self.scroll.x = min(max(self.scroll.x, -self.easel.width * 0.125), self.easel.width * 0.875)
        self.scroll.y = min(max(self.scroll.y, -self.easel.height * 0.125), self.easel.height * 0.875)

        log.debug("scroll: %f, %f" % (self.scroll.x, self.scroll.y))
        
    def snap_scroll (self):
        """Animates the scroll position back towards reasonable bounds."""
        if self.scroll.x < self.easel.width *-0.05: self.scroll.x = self.scroll.x * 0.9
        if self.scroll.y < self.easel.height*-0.05: self.scroll.y = self.scroll.y * 0.9
        if self.scroll.x+self.width > self.easel.width*1.05:
            self.scroll.x = (self.easel.width - self.width)*0.5 + self.scroll.x*0.5
        if self.scroll.y+self.height > self.easel.height*1.05:
            self.scroll.y = (self.easel.height - self.height)*0.5 + self.scroll.x*0.5

    #-----------------------------------------------------------------------------------------------------------------
    # Zoom code

    def init_zoom (self):
        self.zoom = 1.0
        self.zoomref = None

    def zoom_to (self, zoom):
        # End any current stroke.
        self.end_draw()

        # Adjust scroll position to keep the same point centered on screen while the zoom changes.
        # This is either the reference point (the center of the last stroke) or else the screen center.
        if self.zoomref != None:
            scrollcenter = self.zoomref
        else:
            scrollcenter = self.scroll + Pos(self.width*0.5/self.zoom, self.height*0.5/self.zoom)
        self.zoom = zoom
        self.scroll_to(scrollcenter - Pos(self.width*0.5/self.zoom, self.height*0.5/self.zoom))
        self.zoomref = None
        
        log.debug('zoom %f', self.zoom)
        self.flush_entire_canvas()

    def zoom_in (self):
        #if self.zoom == 0.75:
        #    self.zoom_to(1.0)
        #    self.scroll_to(Pos(0,0))
        if self.zoom == 1.0:
            self.zoom_to(2.0)

    def zoom_out (self):
        if self.zoom == 2.0:
            self.zoom_to(1.0)
            self.scroll_to(Pos(0,0))
        #elif self.zoom == 1.0:
        #    self.zoom_to(0.75)

    #-----------------------------------------------------------------------------------------------------------------
    # Drawing commands
    # 
    # These commands instruct the underlying canvas to execute commands which modify the canvas.  
    # 
    # All drawing operations become commands which are executed immediately and also recorded so that the painting can 
    # be played back.

    def draw (self, pos):
        relpos = pos * Pos(self.easel.width, self.easel.height) / Pos(self.zoom, self.zoom) - self.scroll
        relpos = relpos / Pos(self.easel.width, self.easel.height)
        self.easel.play_command(DrawCommand.create_draw(relpos, int(self.pressure)), True)

    def end_draw (self):
        if self.easel.stroke:
            self.easel.play_command(DrawCommand.create_end_draw(int(self.pressure)), True)

        # Send to sharing participants.
        self.send_and_receive_draw_commands()

        # Record a new default zoom-in focal point.
        #self.zoomref = (self.easel.strokemin + self.easel.strokemax) * Pos(0.5,0.5)
        self.zoomref = Pos(self.mx, self.my)

    def set_brush (self, brush):
        self.easel.play_command(DrawCommand.create_color_change(brush.color), True)
        self.easel.play_command(DrawCommand.create_size_change(brush.control, brush.type, brush.size/float(self.easel.width), brush.opacity), True)

    def play_to_playbackpos (self):
        """Plays drawing commands until playbackpos.get_value() is reached.  This may involve resetting the canvas if
        the given position is before the current position, since it is impossible to play commands backwards.
        Called to skip the playback position, for example when fast forwarding or rewinding or dragging the scrollbar."""
        self.playbackposbar.ignore_change += 1

        total_left = 0

        # This used to be in the while True: loop, such that the overlay would only appear when
        # there is a lot of work to do.  It might go back there later.
        self.progress.set_size_request(self.width, self.height)
        self.progress.show_all()
        self.easelarea.set_double_buffered(True)
        self.overlay_active = True
        self.flush_entire_canvas()
        # Display and run the GTK loop.  This hack takes a set number of events per loop,
        # as checking the gtk.events_pending() function leads to an infinite loop.
        for i in range(0, 5):
            if gtk.main_iteration(False):
                return

        # Keep looping until the position is reached.  Since we activate the GTK event loop processing from within
        # our inner loop, the user can actually move the scrollbar while this function is running!
        while True:
            to = int(self.playbackpos.get_value()/100.0*self.easel.playback_length())
            if self.easel.playback_pos() == to:
                break

            # Rewind if needed.
            if self.easel.playback_pos() > to:
                self.easel.clear_image()
                self.easel.start_playback()

            total_left = max(total_left, to - self.easel.playback_pos())

            # Advance playback by as much as we can in 1/10th of a second.
            startpos = self.easel.playback_pos()
            starttk = time.time()
            endtk = starttk + 1.0/10.0
            while self.easel.playback_pos() < to:
                self.easel.playback_step_to(to)
                if time.time() >= endtk:
                    break
            #log.debug("from %d to %d in %fs", startpos, self.easel.playback_pos(), time.time()-starttk)

            # This is currently disabled as it leads to the playback position passing the requested position, which
            # can cause infinite "Working..." loops when the mouse is held down.
            # Leaving it out can lead to inaccurate playback, when the play-to position is mid-stroke.
            #self.easel.playback_finish_stroke()

            # Update the progress bar.
            if total_left > 0:
                f = 1.0-float(to - self.easel.playback_pos())/total_left
                self.progress.progress.set_fraction(f)

            # Comment this in to watch the painting and slow things down.
            #self.flush_entire_canvas()

            # Display and run the GTK loop.  This hack takes a set number of events per loop,
            # as checking the gtk.events_pending() function leads to an infinite loop.
            for i in range(0, 5):
                if gtk.main_iteration(False):
                    return

        self.overlay_active = False
        self.progress.hide_all()
        self.easelarea.set_double_buffered(False)

        self.flush_entire_canvas()
        self.playbackposbar.ignore_change -= 1

    # Canvas repainting.  These methods draw portions of the canvas to the canvasarea.
    def flush_dirty_canvas (self):
        """Causes a redraw of the canvas area which has been modified since the last call to this function."""
        self.easelarea.queue_draw_area(
            int(self.easel.dirtymin.x*2), int(self.easel.dirtymin.y*2), 
            int(self.easel.dirtymax.x*2-self.easel.dirtymin.x*2+1), 
            int(self.easel.dirtymax.y*2-self.easel.dirtymin.y*2+1)) 
        self.easel.reset_dirty_rect()

    def flush_entire_canvas (self):
        """Causes a redraw of the entire canvas area."""
        self.easelarea.queue_draw()

    def flush_cursor (self):
        """Causes a redraw of the canvas area covered by the cursor."""
        try:
            r = int(self.easel.brush.size*2*self.pressure/256)
            x0 = min(self.lastmx-self.lastr/2, self.mx-r/2)
            y0 = min(self.lastmy-self.lastr/2, self.my-r/2)
            x1 = max(self.lastmx+self.lastr/2, self.mx+r/2)
            y1 = max(self.lastmy+self.lastr/2, self.my+r/2)
            self.easelarea.queue_draw_area(x0, y0, x1-x0+2, y1-y0+2)
        except:
            # WTB- Temporary workaround for invalid values!
            self.lastmx = 0
            self.lastmy = 0
            self.lastr = 0

    #-----------------------------------------------------------------------------------------------------------------
    # Application states
    #
    # Colors is controlled by a finite state machine which keeps track of the current application mode (painting,
    # playing back, scrolling, zooming, etc) and manages transitions between states.
    # 
    # Each state is allowed to process code when it is entered or left, for the purpose of initializing variables and 
    # cleaning up afterwards.
    # 
    # todo- Consider breaking up into enter_intro, enter_playback, enter_canvas, etc.

    def enter_mode (self):
        if self.mode == Colors.MODE_INTRO:
            # Load and play intro movie.  It was created on a DS at 60hz, so we need to speed it up drastically to
            # make it watchable.
            self.easel.clear()
            self.easel.load(activity.get_bundle_path() + "/data/intro.drw")
            self.easel.set_playback_speed(8)
            self.easel.start_playback()

        if self.mode == Colors.MODE_PLAYBACK:
            self.easel.set_playback_speed(1)
            self.easel.start_playback()

        if self.mode == Colors.MODE_CANVAS:
            # Clear any existing button pressure to avoid a blotch on the screen when entering Canvas mode.
            self.cur_buttons = 0
            # Reset the brush.
            self.set_brush(self.easel.brush)

        if self.mode == Colors.MODE_SCROLL:
            self.scrollref = None

        if self.mode == Colors.MODE_PALETTE:
            # This simply darkens the canvas slightly to prepare for an overlay to be drawn on top.
            #self.easel.render_overlay()
            # Show the brush controls window.
            self.easelarea.set_double_buffered(True)
            self.brush_controls.set_brush(self.easel.brush)
            self.brush_controls.show_all()
            self.flush_entire_canvas()
            self.overlay_active = True
            self.palettebtn.set_active(True)
            
        if self.mode == Colors.MODE_REFERENCE:
            self.easel.render_reference_overlay()
            self.flush_entire_canvas()
            self.showrefbtn.set_active(True)

    def leave_mode (self):
        if self.mode == Colors.MODE_INTRO:
            self.easel.stop_playback()
            self.easel.clear()
            self.easel.reset_brush()
            self.flush_entire_canvas()

        if self.mode == Colors.MODE_PLAYBACK:
            self.easel.stop_playback()
            # Progress bar goes to 100 when painting.
            self.playbackposbar.ignore_change += 1
            self.playbackpos.set_value(100)
            self.playbackposbar.ignore_change -= 1

        if self.mode == Colors.MODE_CANVAS:
            self.end_draw()

        if self.mode == Colors.MODE_SCROLL:
            pass

        if self.mode == Colors.MODE_PALETTE:
            self.set_brush(self.brush_controls.brush)
            #self.easel.clear_overlay()
            self.brush_controls.hide()
            self.easelarea.set_double_buffered(False)
            self.flush_entire_canvas()
            self.overlay_active = False
            self.palettebtn.set_active(False)

        if self.mode == Colors.MODE_REFERENCE:
            self.easel.clear_overlay()
            self.flush_entire_canvas()
            self.showrefbtn.set_active(False)

    def update_mode (self):
        if self.mode == None:
            self.set_mode(Colors.MODE_INTRO)

        if self.mode == Colors.MODE_INTRO:
            self.easel.update_playback()
            if not self.easel.playback_done():
                self.flush_dirty_canvas()
            if self.cur_buttons & Colors.BUTTON_TOUCH:
                self.set_mode(Colors.MODE_CANVAS)
                return

        if self.mode == Colors.MODE_PLAYBACK:
            self.easel.update_playback()
            if not self.easel.playback_done():
                self.flush_dirty_canvas()
            # Update the progress bar.
            progress_percent = int(100*float(self.easel.playback)/(self.easel.playback_length()+1))
            if self.easel.playing and progress_percent != self.playbackpos.get_value():
                self.playbackposbar.ignore_change += 1
                self.playbackpos.set_value(progress_percent)
                self.playbackposbar.ignore_change -= 1
            # Painting during playback mode allows you start where the painter left off.
            if self.cur_buttons & Colors.BUTTON_TOUCH:
                self.easel.truncate_at_playback()
                self.set_mode(Colors.MODE_CANVAS)
                return

        if self.mode == Colors.MODE_CANVAS:
            # Receive any drawing commands from peers, if not currently drawing.
            if not self.easel.stroke:
                self.send_and_receive_draw_commands()
            # Update drawing.
            if self.cur_buttons & Colors.BUTTON_TOUCH:
                if self.mx != self.lastmx or self.my != self.lastmy:
                    mpos = Pos(float(self.mx)/self.width, float(self.my)/self.height)
                    self.draw(mpos)
                    self.flush_dirty_canvas()
            elif self.easel.stroke:
                self.end_draw()
                self.flush_dirty_canvas()

        if self.mode == Colors.MODE_SCROLL:
            if self.cur_buttons & Colors.BUTTON_TOUCH:
                mpos = Pos(self.mx, self.my)
                if self.scrollref == None:
                    self.scrollref = mpos
                else:
                    move = mpos - self.scrollref
                    if move.x != 0 or move.y != 0:
                        self.scroll_to(self.scroll - move)
                        self.scrollref = mpos
            else:
                self.scrollref = None
                # Smoothly pull back towards the image when out of reasonable bounds.
                #self.snap_scroll()
            self.flush_entire_canvas()

        if self.mode == Colors.MODE_PALETTE:
            pass

        if self.mode == Colors.MODE_REFERENCE:
            pass

    def set_mode (self, mode):
        log.debug("set mode %d", mode)
        if self.mode != None:
            self.leave_mode()
        self.mode = mode
        self.enter_mode()

    def update (self):
        if self.easel == None:
            return
        self.update_input()
        if not self.overlay_active:
            self.update_mode()

    def tick (self):
        # todo- Return False when nothing is going on (to idle the XO), then restart the idle event when something happens.
        self.update()
        return True

    #-----------------------------------------------------------------------------------------------------------------
    # Event handlers

    def on_canvasarea_resize (self):
        rect = self.easelarea.get_allocation()
        log.debug("Canvas resized to %dx%d", rect[2], rect[3])
        self.width = rect[2]
        self.height = rect[3]
        
        # Rebuild easelimage.
        self.easelimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), rect[2], rect[3])

        # Reconfigure canvas.
        self.easel.resize(rect[2]/2, rect[3]/2)
        
        # Resize panels.
        self.brush_controls.set_size_request(rect[2], rect[3])
        self.progress.set_size_request(rect[2], rect[3])

        return True

    def on_canvasarea_expose (self, widget, event):
        if not self.easelarea.bin_window:
            return True
        
        rect = self.easelarea.get_allocation()
        if self.easelimage is None or (rect[2] != self.width or rect[3] != self.height):
            self.on_canvasarea_resize()

        gc = self.easelarea.get_style().fg_gc[gtk.STATE_NORMAL]

        # Blit dirty rectangle of canvas into the image.
        if self.zoom == 1.0:
            self.easel.blit_2x(
                self.easelimage, 
                int(event.area.x), int(event.area.y), int(event.area.width), int(event.area.height),
                int(-self.scroll.x), int(-self.scroll.y),
                self.overlay_active)
        elif self.zoom == 2.0:
            self.easel.blit_4x(
                self.easelimage, 
                int(event.area.x), int(event.area.y), int(event.area.width), int(event.area.height),
                int(-self.scroll.x/2), int(-self.scroll.y/2),
                self.overlay_active)

        # Then draw the image to the screen.
        self.easelarea.bin_window.draw_image(
            gc, self.easelimage, 
            event.area.x, event.area.y, 
            event.area.x, event.area.y, event.area.width, event.area.height)

        # Draw introduction text.
        if self.mode == Colors.MODE_INTRO:
            context = self.easelarea.create_pango_context()
            layout = self.easelarea.create_pango_layout(_('Click anywhere to begin painting!'))
            layout.set_font_description(pango.FontDescription('Times 14'))    
            size = layout.get_size()
            x = (self.width-size[0]/pango.SCALE)/2
            y = self.height-50-size[1]/pango.SCALE
            self.easelarea.bin_window.draw_layout(gc, x, y, layout)

        #self.draw_cursor()

        # Hack to keep toolbar up to date.  For some reason it fails to draw pretty often.
        #self.toolbox.queue_draw()

    def draw_cursor (self):
        r = int(self.easel.brush.size*2*self.pressure/256)
        self.easelarea.bin_window.draw_arc(self.easelarea.get_style().black_gc, False, self.mx-r/2, self.my-r/2, r, r, 0, 360*64)
        self.lastr = r
        self.lastmx = self.mx
        self.lastmy = self.my

    def on_palette (self, button):
        if button.get_active():
            if self.mode != Colors.MODE_PALETTE:
                self.set_mode(Colors.MODE_PALETTE)
        else:
            self.set_mode(Colors.MODE_CANVAS)

    def on_take_reference (self, button):
        self.take_reference = True

    def on_show_reference (self, button):
        if button.get_active():
            if self.mode != Colors.MODE_REFERENCE:
                self.set_mode(Colors.MODE_REFERENCE)
        else:
            self.set_mode(Colors.MODE_CANVAS)

    def on_videopaint (self, button):
        self.videopaint_enabled = button.get_active()
        if button.get_active():
            gobject.timeout_add(33, self.on_videopaint_tick)

    def on_videopaintpreview_expose (self, widget, event):
        self.easel.blit_videopaint(self.videopaintimage)
        size = self.videopaintpreview.window.get_size()
        x = (size[0]-self.easel.VIDEO_WIDTH)/2
        y = (size[1]-self.easel.VIDEO_HEIGHT)/2
        self.videopaintpreview.window.draw_image(
            self.videopaintpreview.get_style().fg_gc[gtk.STATE_NORMAL], 
            self.videopaintimage, 
            0, 0, x, y, -1, -1)

    def on_videopaint_tick (self):
        if not self.videopaint_enabled:
            return False
        
        # Update "pressure" from image (uses blob size).
        self.pressure = self.easel.videopaint_pressure

        # Move the mouse to follow the videopaint cursor.  
        # Note that we do not allow it to reach the corners, to avoid bringing up the Sugar overlay.
        size = self.window.get_size()
        self.mx = int(25+max(0.0, min(1.0, self.easel.videopaint_pos.x))*(size[0]-50))
        self.my = int(25+max(0.0, min(1.0, self.easel.videopaint_pos.y))*(size[1]-50))
        #gtk.gdk.display_get_default().warp_pointer(self.get_screen(), self.mx, self.my)
        self.flush_cursor()

        # Update the preview window.
        self.videopaintpreview.queue_draw()

        # Process drawing.
        self.update()

        return True

    def on_clear (self, button):
        if self.mode != Colors.MODE_CANVAS:
            self.set_mode(Colors.MODE_CANVAS)
        self.easel.clear()
        self.flush_entire_canvas()

    def on_play (self, button):
        # Change to playback mode when the Play button is first pressed.
        if self.mode != Colors.MODE_PLAYBACK:
            self.set_mode(Colors.MODE_PLAYBACK)
        # Resume playback.
        self.easel.resume_playback()

    def on_pause (self, button):
        if self.mode != Colors.MODE_PLAYBACK:
            self.set_mode(Colors.MODE_PLAYBACK)
        self.easel.pause_playback()

    def on_skip_begin (self, button):
        if self.mode != Colors.MODE_PLAYBACK:
            self.set_mode(Colors.MODE_PLAYBACK)
        self.playbackpos.set_value(0)

    def on_skip_end (self, button):
        if self.mode != Colors.MODE_PLAYBACK:
            self.set_mode(Colors.MODE_PLAYBACK)
        self.playbackpos.set_value(100)

    def on_sample (self, button):
        self.set_mode(Colors.MODE_PLAYBACK)
        self.easel.clear()
        self.easel.load(button.filename)
        self.easel.set_playback_speed(8)
        self.flush_entire_canvas()
        self.toolbox.set_current_toolbar(2) # Switch to 'watch' toolbar.

    def on_playbackposbar_change (self, progress):
        if self.playbackposbar.ignore_change > 0:
            return
        if self.mode != Colors.MODE_PLAYBACK:
            self.set_mode(Colors.MODE_PLAYBACK)
        self.play_to_playbackpos()
        self.easel.pause_playback()

    #-----------------------------------------------------------------------------------------------------------------
    # Journal integration

    def read_file(self, file_path):
        log.debug("Loading from journal %s", file_path)
        self.set_mode(Colors.MODE_CANVAS)
        self.easel.clear()
        self.easel.load(file_path.encode())
        log.debug("Playing back %d commands", self.easel.playback_length())
        self.set_mode(Colors.MODE_PLAYBACK)
        self.playbackpos.set_value(100)

    def write_file(self, file_path):
        log.debug("Saving to journal %s", file_path)
        log.debug("Saving %d commands", self.easel.playback_length())
        self.easel.save(file_path.encode())

    def take_screenshot (self):
        if self.easelarea and self.easelarea.bin_window:
            self._preview.take_screenshot(self.easelarea)

    def save_thumbnail(self, filename):
        pbuf = gtk.gdk.Pixbuf(gtk.gdk.COLORSPACE_RGB, False, 8, self.width, self.height)
        pbuf = pbuf.get_from_image(self.easelimage, self.easelarea.get_colormap(), 0, 0, 0, 0, self.width, self.height)
        pbuf = pbuf.scale_simple(80, 60, gtk.gdk.INTERP_BILINEAR)
        pbuf.save(filename, "png")

    #-----------------------------------------------------------------------------------------------------------------
    # Benchmarking
    # 
    # Simply causes the C++ code to do a bunch of work and prints out the time used.  Useful for testing the benefits
    # of optimization.

    def benchmark (self):
        # Benchmark a Canvas object.
        canvas = Canvas(600, 400)
        canvas.clear()
        canvas.load(activity.get_bundle_path() + "/data/intro.drw")
        start = time.time()
        for i in range(0,100):
            canvas.start_playback()
            canvas.finish_playback()
        log.debug("Canvas playback benchmark: %f sec", time.time()-start)

        #canvasimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), 600, 400)
        #start = time.time()
        #for i in range(0,100):
        #    canvas.blit_2x(canvasimage, 0, 0, 600, 400)
        #log.debug("Canvas 1.0x blit benchmark: %f sec", time.time()-start)

        canvasimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), 1200, 800)
        start = time.time()
        for i in range(0,100):
            canvas.blit_2x(canvasimage, 0, 0, 600, 400)
        log.debug("Canvas 2.0x blit benchmark: %f sec", time.time()-start)

        # Benchmark a Palette object.
        palette = Palette(500)
        paletteimage = gtk.gdk.Image(gtk.gdk.IMAGE_FASTEST, gtk.gdk.visual_get_system(), 500, 500)
        start = time.time()
        for i in range(0,100):
            palette.render_wheel(paletteimage)
        log.debug("Palette wheel benchmark: %f sec", time.time()-start)

        start = time.time()
        for i in range(0,100):
            palette.render_triangle(paletteimage)
        log.debug("Palette triangle benchmark: %f sec", time.time()-start)


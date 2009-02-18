// This file serves to allow access to a limited subset of GTK and GStreamer objects passed from PyGTK, without the full 
// GLib + GTK + GST development environment. 
// 
// Obviously, it is limited to working with a specific version of GTK, PyGTK and GStreamer, but these structures appear 
// to be fairly stable.
#ifndef GTK_TYPES_H
#define GTK_TYPES_H

#include <stdint.h>

typedef uint8_t guint8;
typedef int32_t gint;
typedef uint32_t guint;
typedef uint16_t guint16;
typedef uint64_t guint64;
typedef void* gpointer;

struct GTypeClass;
struct GData;

struct _GTypeInstance
{
  /*< private >*/
  GTypeClass *g_class;
};

typedef struct _GTypeInstance GTypeInstance;

struct  _GObject
{
  GTypeInstance g_type_instance;
  
  /*< private >*/
  guint         ref_count;
  GData        *qdata;
};

typedef struct _GObject GObject;

typedef enum
{
  GDK_IMAGE_NORMAL,
  GDK_IMAGE_SHARED,
  GDK_IMAGE_FASTEST
} GdkImageType;

typedef enum
{
  GDK_LSB_FIRST,
  GDK_MSB_FIRST
} GdkByteOrder;

struct GdkVisual;
struct GdkColormap;

typedef struct {
  GObject parent_instance;

  
  GdkImageType	type; /* read only. */
  GdkVisual    *visual;	    /* read only. visual used to create the image */
  GdkByteOrder	byte_order; /* read only. */
  gint		width; /* read only. */
  gint		height; /* read only. */
  guint16	depth; /* read only. */
  guint16	bpp;	        /* read only. bytes per pixel */
  guint16	bpl;	        /* read only. bytes per line */
  guint16       bits_per_pixel; /* read only. bits per pixel */
  gpointer	mem;

  GdkColormap  *colormap; /* read only. */
} GdkImage;

struct GSList;

typedef struct {
    PyObject_HEAD
    GObject *obj;
    PyObject *inst_dict; /* the instance dictionary -- must be last */
    PyObject *weakreflist; /* list of weak references */
    GSList *closures;
} PyGObject;

struct _GstMiniObject {
  GTypeInstance instance;
  /*< public >*/ /* with COW */
  gint refcount;
  guint flags;

  /*< private >*/
  gpointer _gst_reserved;
};

typedef struct _GstMiniObject GstMiniObject;

struct GstCaps;

typedef guint64	GstClockTime;

#define GST_PADDING		4

struct _GstBuffer {
  GstMiniObject		 mini_object;

  /*< public >*/ /* with COW */
  /* pointer to data and its size */
  guint8		*data;
  guint			 size;

  /* timestamp */
  GstClockTime		 timestamp;
  GstClockTime		 duration;

  /* the media type of this buffer */
  GstCaps		*caps;

  /* media specific offset */
  guint64		 offset;
  guint64		 offset_end;

  guint8                *malloc_data;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

typedef struct _GstBuffer GstBuffer;

#endif


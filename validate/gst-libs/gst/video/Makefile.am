ORC_SOURCE=video-orc
include $(top_srcdir)/common/orc.mak

libgstvalidatevideo_@GST_API_VERSION@_la_SOURCES = gstvalidatessim.c gssim.c video-converter.c video-dither.c video-scaler.c video-resampler.c video-orc-dist.c
libgstvalidatevideo_@GST_API_VERSION@include_HEADERS = gstvalidatessim.h gssim.h video-converter.h video-dither.h video-scaler.h video-resampler.h video-orc-dist.h

libgstvalidatevideo_@GST_API_VERSION@_la_CFLAGS = $(GST_ALL_CFLAGS) $(CAIRO_CFLAGS) $(GST_VIDEO_CFLAGS) $(ORC_CFLAGS) -I$(top_builddir)
libgstvalidatevideo_@GST_API_VERSION@_la_LIBADD = $(GST_ALL_LIBS) $(CAIRO_LIBS) $(GST_VIDEO_LIBS) $(ORC_LIBS) $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la
libgstvalidatevideo_@GST_API_VERSION@_la_LDFLAGS = $(GST_ALL_LDFLAGS) $(CAIRO_LDFLAGS) $(GST_VIDEO_LDFLAGS)

lib_LTLIBRARIES = libgstvalidatevideo-@GST_API_VERSION@.la

libgstvalidatevideo_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/lib/validate/video

CLEANFILES =

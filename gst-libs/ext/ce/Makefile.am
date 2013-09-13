lib_LTLIBRARIES = libgstcebase-@GST_API_VERSION@.la

libgstcebase_@GST_API_VERSION@_la_DEPENDENCIES = \
	$(top_builddir)/gst-libs/ext/cmem/libgstcmem-@GST_API_VERSION@.la

libgstcebase_@GST_API_VERSION@_la_SOURCES = \
	gstceutils.c		\
	gstcevidenc.c		\
	gstceimgenc.c		\
	gstceaudenc.c

libgstcebase_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/ce
libgstcebase_@GST_API_VERSION@include_HEADERS = \
	gstcevidenc.h		\
	gstceimgenc.h		\
	gstceaudenc.h

libgstcebase_@GST_API_VERSION@_la_CFLAGS = \
    $(GST_CFLAGS) $(CODECS_CFLAGS) -I$(top_srcdir)/gst-libs/ext/cmem
libgstcebase_@GST_API_VERSION@_la_LIBADD = \
    $(GST_LIBS) $(CODECS_LIBS) $(top_builddir)/gst-libs/ext/cmem/libgstcmem-@GST_API_VERSION@.la
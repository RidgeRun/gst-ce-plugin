
lib_LTLIBRARIES = libgstcmem-@GST_API_VERSION@.la

libgstcmem_@GST_API_VERSION@_la_SOURCES = \
	gstcmemallocator.c \
	gstceslicepool.c

libgstcmem_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/ext/cmem
libgstcmem_@GST_API_VERSION@include_HEADERS = \
	gstcmemallocator.h \
	gstceslicepool.h

libgstcmem_@GST_API_VERSION@_la_CFLAGS = $(GST_CFLAGS) $(CODECS_CFLAGS) -I@top_srcdir@/ext/
libgstcmem_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS) $(CODECS_LIBS)
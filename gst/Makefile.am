plugin_LTLIBRARIES = libgstce.la

# sources used to compile this plugin
libgstce_la_SOURCES = gstce.c \
 gstcevidenc.c gstceh264enc.c

# compiler and linker flags used to compile this plugin
libgstce_la_CFLAGS = $(GST_CFLAGS)
libgstce_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)

libgstce_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstce_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstce.h \
 gstcevidenc.h gstceh264enc.h

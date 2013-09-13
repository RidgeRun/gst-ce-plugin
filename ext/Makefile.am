plugin_LTLIBRARIES = libgstce.la

# sources used to compile this plugin
libgstce_la_SOURCES = \
	gstce.c \
	gstceh264enc.c \
	gstcejpegenc.c \
	gstceaacenc.c

# compiler and linker flags used to compile this plugin
libgstce_la_CFLAGS = $(GST_CE_CFLAGS) $(CODECS_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstce_la_LIBADD = $(GST_CE_LIBS) $(CODECS_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	-lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS)

libgstce_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstce_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = \
	gstceh264enc.h \
	gstcejpegenc.h \
	gstceaacenc.h

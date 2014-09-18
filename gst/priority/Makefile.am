plugin_LTLIBRARIES = libgstpriority.la

# sources used to compile this plug-in
libgstpriority_la_SOURCES = gstpriority.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstpriority_la_CFLAGS = $(GST_CFLAGS)
libgstpriority_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) 
libgstpriority_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpriority_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstpriority.h

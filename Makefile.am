if BUILD_EXTERNAL
if USE_CODECS
SUBDIRS_EXT = ext
else
SUBDIRS_EXT =
endif
else
SUBDIRS_EXT =
endif

SUBDIRS = 			\
	gst-libs 		\
	gst 		 	\
	$(SUBDIRS_EXT)		\
	tests 			\
	docs			\
	common			\
	pkgconfig

DIST_SUBDIRS = 			\
	docs			\
	gst-libs		\
	gst			\
	ext			\
	tests 			\
	common			\
	pkgconfig

EXTRA_DIST = autogen.sh

ACLOCAL_AMFLAGS = -I m4 -I common/m4

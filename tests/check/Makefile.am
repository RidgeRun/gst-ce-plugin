include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir):$(GSTPB_PLUGINS_DIR):$(GST_PLUGINS_DIR)

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check

check_PROGRAMS = \
	generic/plugin-test		\
	generic/states			\
	elements/ce_h264enc		\
	elements/ce_jpegenc		\
	libs/cmem

elements_ce_h264enc_LDADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstapp-@GST_API_VERSION@ \
	$(top_builddir)/gst-libs/gst/cmem/libgstcmem-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

elements_ce_jpegenc_LDADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstapp-@GST_API_VERSION@ \
	$(top_builddir)/gst-libs/gst/cmem/libgstcmem-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

libs_cmem_LDADD = \
	$(top_builddir)/gst-libs/gst/cmem/libgstcmem-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(LDADD)

VALGRIND_TO_FIX =
#	generic/plugin-test

TESTS = $(check_PROGRAMS)

# these tests don't even pass
noinst_PROGRAMS =

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) \
	$(GST_OPTION_CFLAGS) -DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\""

LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE = $(VALGRIND_TO_FIX)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp


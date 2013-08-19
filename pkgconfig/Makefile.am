### all of the standard pc files we need to generate

pcfiles = 							\
	gstreamer-cmem-@GST_API_VERSION@.pc

pcfiles_uninstalled =						\
	gstreamer-cmem-@GST_API_VERSION@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate pc files
%-@GST_API_VERSION@.pc: %.pc
	$(cp_verbose_0)cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
	$(cp_verbose_0)cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST =							\
	gstreamer-cmem.pc.in					\
	gstreamer-cmem-uninstalled.pc.in

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)

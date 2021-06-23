bin_PROGRAMS = lxplug-updater-install

lxplug_updater_install_CFLAGS = \
	-I$(top_srcdir) \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)/lxpanel"\" \
	-DPACKAGE_UI_DIR=\""$(datadir)/lxpanel/ui"\" \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

lxplug_updater_install_SOURCES = installer.c

lxplug_updater_install_includedir = $(includedir)

lxplug_updater_install_include_HEADERS =

lxplug_updater_install_LDFLAGS = $(DYNAMIC_FLAGS)
lxplug_updater_install_DEPENDENCIES_EXTRA = $(BUILTIN_PLUGINS)

lxplug_updater_install_LDADD = \
		$(BUILTIN_PLUGINS) \
		$(PACKAGE_LIBS) \
		$(X11_LIBS) \
		$(INTLLIBS)

EXTRA_DIST =

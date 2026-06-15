# This file is part of the dosbox-automation Project.
# License: GPL-3.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

.PHONY: help png windows macos linux clean

PNG_DIR = png
WINDOWS_DIR = windows
MACOS_DIR = macos

# master raster source - all PNGs are downscaled from this
MASTER_PNG = ../../augra-developer-docs/design/dosbox-automation/icons/application-logo-without-background.png

MACOS_ICONSET_DIR = $(MACOS_DIR)/dosbox-automation.iconset

help:
	@echo "Re-generate icons in platform specific formats, or install icons on Linux."
	@echo ""
	@echo "All PNG icons are rendered from the master raster source (1254x1254)"
	@echo "using ImageMagick Lanczos downscale. The SVG is a separate vectorized"
	@echo "version used only for Linux scalable icon packaging."
	@echo ""
	@echo "Available targets:"
	@echo "  png     - re-generate small PNG icons from the master PNG"
	@echo "  windows - re-generate Windows .ico file from the PNG icons"
	@echo "  macos   - re-generate macOS .icns file from the 1024px PNG (needs macOS)"
	@echo "  all     - re-generate all (in above the listed order)"
	@echo ""
	@echo "To install the icons on Linux:"
	@echo "  make install              # install icons to \$${DESTDIR}/usr/local/share/icons/..."
	@echo "  make install prefix=/usr  # install icons to \$${DESTDIR}/usr/share/icons/..."
	@echo "  make install datadir=foo  # install icons to \$${DESTDIR}foo/icons/..."

all: png windows macos

##
#  Re-generate PNG icons from the master raster source.
#  Uses ImageMagick Lanczos downscale for best quality.
#
#  Needs ImageMagick (convert).
#
png:
	convert $(MASTER_PNG) -resize   16x16 -filter Lanczos $(PNG_DIR)/icon_16.png
	convert $(MASTER_PNG) -resize   22x22 -filter Lanczos $(PNG_DIR)/icon_22.png
	convert $(MASTER_PNG) -resize   24x24 -filter Lanczos $(PNG_DIR)/icon_24.png
	convert $(MASTER_PNG) -resize   32x32 -filter Lanczos $(PNG_DIR)/icon_32.png
	convert $(MASTER_PNG) -resize   48x48 -filter Lanczos $(PNG_DIR)/icon_48.png
	convert $(MASTER_PNG) -resize   96x96 -filter Lanczos $(PNG_DIR)/icon_96.png
	convert $(MASTER_PNG) -resize 128x128 -filter Lanczos $(PNG_DIR)/icon_128.png
	convert $(MASTER_PNG) -resize 256x256 -filter Lanczos $(PNG_DIR)/icon_256.png
	convert $(MASTER_PNG) -resize 512x512 -filter Lanczos $(PNG_DIR)/icon_512.png
	convert $(MASTER_PNG) -resize 1024x1024 -filter Lanczos $(PNG_DIR)/icon_1024.png

##
#  Re-generate Windows .ico file from the PNG icons.
#
#  Needs ImageMagick (convert) or icotool from the icoutils package.
#
windows:
	convert $(PNG_DIR)/icon_256.png \
	        $(PNG_DIR)/icon_48.png \
	        $(PNG_DIR)/icon_32.png \
	        $(PNG_DIR)/icon_24.png \
	        $(PNG_DIR)/icon_16.png \
	        $(WINDOWS_DIR)/dosbox-automation.ico

##
#  Re-generate macOS .icns file from the 1024x1024 PNG.
#
#  Needs sips & iconutil (available on macOS out-of-the-box).
#
macos:
	mkdir -p $(MACOS_ICONSET_DIR)

	sips -z  16  16 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_16x16.png"
	sips -z  32  32 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_16x16@2x.png"
	sips -z  32  32 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_32x32.png"
	sips -z  64  64 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_32x32@2x.png"
	sips -z 128 128 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_128x128.png"
	sips -z 256 256 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_128x128@2x.png"
	sips -z 256 256 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_256x256.png"
	sips -z 512 512 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_256x256@2x.png"
	sips -z 512 512 $(PNG_DIR)/icon_1024.png --out "$(MACOS_ICONSET_DIR)/icon_512x512.png"

	cp $(PNG_DIR)/icon_1024.png $(MACOS_ICONSET_DIR)/icon_512x512@2x.png
	cp $(PNG_DIR)/icon_1024.png $(MACOS_ICONSET_DIR)/icon_1024x1024.png

	iconutil -c icns $(MACOS_ICONSET_DIR)

##
#  Install icons on Linux.
##
prefix = /usr/local
datadir = $(prefix)/share
install:
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/16x16/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/22x22/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/24x24/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/32x32/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/48x48/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/96x96/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/128x128/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/256x256/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/512x512/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/1024x1024/apps/"
	mkdir -m 0755 -p "$(DESTDIR)$(datadir)/icons/hicolor/scalable/apps"

	install -p -m 0644 $(PNG_DIR)/icon_16.png "$(DESTDIR)$(datadir)/icons/hicolor/16x16/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_22.png "$(DESTDIR)$(datadir)/icons/hicolor/22x22/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_24.png "$(DESTDIR)$(datadir)/icons/hicolor/24x24/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_32.png "$(DESTDIR)$(datadir)/icons/hicolor/32x32/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_48.png "$(DESTDIR)$(datadir)/icons/hicolor/48x48/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_96.png "$(DESTDIR)$(datadir)/icons/hicolor/96x96/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_128.png "$(DESTDIR)$(datadir)/icons/hicolor/128x128/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_256.png "$(DESTDIR)$(datadir)/icons/hicolor/256x256/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_512.png "$(DESTDIR)$(datadir)/icons/hicolor/512x512/apps/dosbox-automation.png"
	install -p -m 0644 $(PNG_DIR)/icon_1024.png "$(DESTDIR)$(datadir)/icons/hicolor/1024x1024/apps/dosbox-automation.png"
	install -p -m 0644 svg/dosbox-automation.svg "$(DESTDIR)$(datadir)/icons/hicolor/scalable/apps/dosbox-automation.svg"

clean:
	rm -rf $(MACOS_ICONSET_DIR)

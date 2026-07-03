# Icon guide

## Overview

This directory contains the source and the derived icon files used by the
application and the installer for our supported platforms (Windows, macOS, and
Linux). The derived files are also checked in, so they should only be
regenerated with the `make` command when making changes to the source icon's
design (more on that below).

All PNG icons are rendered from a master raster source (1254x1254, transparent
background) using ImageMagick Lanczos downscale. The SVG is a separate
vectorized version used only for Linux scalable icon packaging.

The subfolders contain the following files:

- `macos` -- The 1024px source PNG file and the derived `.icns` file to be
   used in the macOS app bundle.

- `old` -- The old DOSBox icons (unused in our packages, just for archival
   purposes).

- `png` -- PNG icons at various sizes, rendered from the master source.
   These are used directly for Linux and to derive the Windows `.ico` icon file.

- `svg` -- The vectorized SVG icon, used as the scalable icon for Linux
   desktop environments. This is derived from the same master artwork, not
   the other way around.

- `windows` -- This contains the `.ico` file derived from the small PNG files
  that gets compiled into the Windows executable as a resource, and BMP files
  used in the Windows installer (if applicable).


## Prerequisites

To regenerate the derived icon files, you need ImageMagick (`convert`).
For Windows `.ico` generation, ImageMagick or `icotool` (from `icoutils`).
For macOS `.icns` generation, you need `sips` and `iconutil` (macOS only).


## Regenerating the icons

The master source PNG lives outside this directory, in the design docs at
`augra-developer-docs/design/dosbox-automation/icons/application-logo-without-background.png`.

Run `make all` to regenerate all derived files, or use individual targets:
`make png`, `make windows`, `make macos`. Run `make help` for details.


## Platform-specific notes

### Windows

The `.ico` file contains bitmap icons at various sizes; it is built from the
small PNG icons using ImageMagick.

The 16px icon is used in the window's title bar, the 24px one is the taskbar
icon. If the 24px icon is missing, Windows will pick the wrong icon for the
window's titlebar as well (it will scale down the biggest icon it can find, so
the result will be blurry).


### macOS

macOS does not support vector icons, so we generate the `.icns` file from the
1024px PNG using `sips` and `iconutil`.

When building the app bundle, the `.icns` file must be placed in
`Contents/SharedSupport/Resources/`. The `Info.plist` file contains the name
of the icon file.


### Linux

`svg/dosbox-automation.svg` is the scalable icon for Linux desktop
environments. When packaging for Linux, copy it into the distro specific
directory (usually `/usr/share/icons/hicolor/scalable/apps/`).

You will need an additional
[.desktop](https://specifications.freedesktop.org/desktop-entry-spec/latest/)
file to correlate the icon file with the application.

# Resources

dosbox-automation needs its bundled resource files at runtime: the DOS
utilities on the Y: drive, translations, code pages, the SoundFont, and
similar data. Release packages already contain them next to the binary.
If you build from source, you can run the executable straight from the
build directory and everything is found. This document is for packagers
and for anyone moving a binary out of its build tree: the `resources`
directory has to come along.

## Lookup order

At startup the executable checks these locations for a `resources`
directory, first hit wins (see `get_resource_parent_paths()` in
`src/misc/support.cpp`):

1. The current working directory: `./resources`.
2. Next to the executable: `<exe-dir>/resources`, then one level up,
   `<exe-dir>/../resources` (the "one up" variant is what lets unit
   tests find resources from the build tree). On macOS the bundle
   layout `<exe-dir>/../Resources` is used instead.
3. The data directory set at compile time:
   `${CMAKE_INSTALL_FULL_DATADIR}/dosbox-automation`
   (typically `/usr/local/share/dosbox-automation`).
4. On Linux and the BSDs: `$XDG_DATA_HOME/dosbox-automation`, then each
   entry of `$XDG_DATA_DIRS/dosbox-automation`.
5. On Linux and the BSDs: `<exe-dir>/../share/dosbox-automation`, a
   deliberate executable-relative fallback so a `--prefix`-style
   install tree stays portable when moved.
6. As a last resort, the user's configuration directory (the same one
   holding `dosbox-automation.conf`).

Mind item 1: the working directory wins over everything, including the
resources shipped next to the binary. That is convenient during
development and a trap during packaging tests. A release binary or
AppImage started from a source checkout will silently use the checkout's
`resources` instead of its own. When validating a package, run it from a
neutral directory.

## Packaging

Two sane layouts:

1. Portable package: put `resources` next to the executable and ship
   the pair as one unit (zip, installer payload, AppImage internals).
   This is how the project packages are built and the layout to prefer
   for wrapped formats.
2. System install: put the resources into the compile-time data dir or
   an XDG data path, named `dosbox-automation`.

## Why not embed the resources in the executable?

Raw files stay inspectable: anyone can diff them, use them with third
party tools, or update them without a development environment. Encoding
them as source would bury tens of thousands of hex lines in the tree,
where they slow editors and pollute every grep. The files are the
better source of truth.

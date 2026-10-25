# Security policy

## Supported versions

The [`main` branch](https://github.com/dosbox-automation/dosbox-automation/tree/main)
and the [latest release](https://github.com/dosbox-automation/dosbox-automation/releases)
are supported with security updates.

## Reporting a vulnerability

For issues that should not be public before a fix exists, mail
<dosbox-automation-project@trinity2k.net>. You will get a human reply.

For everything else, raise a
[bug report](https://github.com/dosbox-automation/dosbox-automation/issues/new?template=bug_report.yml&title=Security%20issue:%20).
We believe in open technical discourse about security; findings, analysis,
and patches are welcome in public.

## Security model in short

For further details, see the full description in the manual:
<https://dosbox-automation.org/0.85.1/automation/security/>.

The REST API gives full control over the emulated machine, which makes the
webserver an attack surface by design. The short version:

- Every API request needs a bearer token (64-char random hex, fresh per
  start, never fully logged, constant-time comparison). No default
  credential, no way to disable authentication.
- Tool pages such as the Cheat Workbench receive the token written into
  the HTML document itself, and only when the browser connects over
  loopback; every other peer gets the page with an empty token field.
  The token never appears in a script, JSON or other response a browser
  could execute cross-origin, no CORS header is ever set, and the API
  still demands the Authorization header on every call.
  `webserver_tool_token = prompt` turns the delivery off.
- The webserver binds to localhost only by default. Host header validation
  rejects DNS rebinding. No CORS headers are set, OPTIONS preflight is
  refused. Request bodies are capped.
- Every MOUNT, BOOT, and drive-swap path is validated before a drive is
  constructed: paths must resolve, symlink components are rejected, system
  directories are blocked, and disk images must pass structural validation.
  A path the guest names through a mounted drive (MOUNT, SUBST, BOOT) is
  mapped to the host file the drive holds and judged as that file.
  The files a CD image names itself, a cue sheet's FILE lines and an
  mds's mdf, must sit directly beside the image: a name with a directory
  or drive in it counts only by its last component, matched without
  regard to case in the image's own directory, and links, dot-named
  files and device names are refused.
  The blocked directories come from one table with a row per host OS
  (Linux, Windows, macOS, FreeBSD, NetBSD, OpenBSD, DragonFly), checked
  against the canonical path. On the Unix rows a mount is refused when
  it sits inside a blocked directory and also when it contains one, so a
  parent such as `/private` on macOS cannot be mounted to reach the
  named system directories below it; on Windows only the directory, its
  subtree and every drive root are refused, because that list carries
  the user's own temp directory. The bare root is refused everywhere.
  `/opt` is blocked on every Unix build with `/opt/games` left open, and
  `/var/home` (Linux) and `/usr/home` (FreeBSD, DragonFly) stay open on
  every build under those spellings, for the installs where `/home` is a
  symlink into them; a path that goes through the `/home` symlink is
  refused as a symlink component, as any symlinked path is. The table is tested on every
  host, so a Linux build checks the macOS and BSD rows too. All of this
  is from 0.86.0; earlier releases apply the 17-entry Linux list to
  every non-Windows build, without `/opt` and without the parent rule.
  With the webserver enabled, directory mounts, API disk swaps, and the
  image mounts of MOUNT and BOOT are all whitelist-restricted, including
  when they are typed in the guest shell or run from autoexec, and a
  one-way mount lock can freeze the mount configuration until restart.
  The directory holding the loaded -conf file counts as an allowed root
  for all of them, so a recipe can reach the files sitting next to it.
  Beyond that anchor, whitelists are read from the primary config only,
  so a bundled game config cannot widen them.
- Archive mounts (zip, RAR, 7z, tar) go through the same path validation
  as disk images, then a structure check on the file's own bytes, and the
  archive is read by a separate helper process that links the reader
  library alone. The archive itself is never written. A game's writes
  land in a sidecar directory under the saves directory (XDG data home,
  `%LOCALAPPDATA%` or `~/Library/Application Support`). A new saves
  directory is created with owner-only permissions; an existing one keeps
  its own. A saves directory that is a link (to another disk, say) is
  followed, and the place it resolves to must not be a system path; the
  archive cache, which a trim deletes in, refuses a link outright. A
  custom path through `-sidecar` is
  validated like any directory mount; a sidecar found beside the archive
  is adopted
  only when that directory passes the directory whitelist for the mount's
  origin. Nothing in the sidecar that is a symlink is written to or
  through, the deletions and renames of base files are kept as DOS names
  only, and a name that fails the DOS rules on load is dropped. One
  sidecar serves one mount at a time, held by a lock file and an
  in-process key; a second mount of the same archive is refused unless it
  asks for no sidecar. Unpacked members live under the cache directory
  with caps on size, free space and age. A mount with `-scratch` writes
  into a private directory under the archive cache instead, locked to
  that mount and removed when it is unmounted; after a crash the cache's
  start-up sweep removes it, without following links.
  Zip members packed with PKZIP 1.x's shrink and implode methods are
  decoded by our port of Info-ZIP's decoders, inside the same helper
  process, stopped at the member's declared size and checked against its
  CRC-32.
  An image named inside an archive (`game.zip/disks/disk1.img`) takes
  the policy on the archive's host path, whitelist and all; the file the
  image code then opens is the engine's own copy. A CD image is served
  from the cache; a floppy or hard-disk image is copied once into the
  game's sidecar under `.images`, where its writes stay, and that copy is
  locked to one engine at a time. A disk served from the cache, or any
  disk of a read-only mount and its swaps, is opened read-only, because
  other engines lease the same file; a swap on a read-write drive opens
  its disk read-write, and a read-only drive never takes the writable
  object BOOT keeps in its list. A cue sheet from an archive is
  rewritten so every `FILE` line names a member's cached file beside it;
  a cue that names anything else refuses the mount.
  A compressed single image (`disk1.img.xz`, `game.imz`, `game.isz`) is
  served the same way, as the one member of its file. libarchive reads
  the xz, gzip, bzip2 and zstd forms as one image only when the file name
  and the first bytes agree, and only where it decodes that format
  itself; a stream it cannot decode, or a tar under an image name, is
  refused. Our own ISZ reader checks the header, the chunk table and
  every chunk length against the file before it decodes anything, and it
  refuses encrypted and segmented ISZ files.
  A member path may pass through up to three archives, or an ISO 9660
  image, inside the file on disk (`outer.zip/game.zip/disk1.img`, and
  `MOUNT C outer.zip/game.zip` for the inner archive as the drive); the
  policy judges the file on disk, every level is read by the same helper
  under the same caps, counted across the whole path, and a zip stored
  inside a zip is read in place within bounds the engine checks.
  The management commands (`MOUNT -cache`, `--archive-cache`,
  `--sidecar list`) read an archive only through the same helper, and the
  cache's "in use" check creates no lock files.
- `MOUNT ... -run NAME`, and an archive given on the command line, start
  a program from the drive that MOUNT has just made, and with exactly the
  trust of that MOUNT: whoever could type the MOUNT could type the program.
  The name is an 8.3 path on that drive, never a host path; a drive
  letter, `..`, a leading `-`, and every character a DOS command line
  reads as syntax (`> < | & "` and spaces among them) refuse it, and so
  does a file that is not a .COM, .EXE or .BAT. The program is started by
  its full path on that drive, so a name such as TYPE.EXE cannot reach a
  built-in command. `MOUNT -run` queues its lines through the shell's
  verbatim path, which keeps `%` intact without expansion. The command
  line's `--run` doubles `%` for the AUTOEXEC.BAT batch expansion.
  The name is echoed back with control characters replaced and its length
  capped.
- The program menu (UniMenu, `Y:\DOS\UNIMENU.EXE`) starts only what MOUNT
  offered it. The engine keeps the list and starts the chosen entry through
  the same resolver and checks as `-run`, so nothing the menu sends becomes
  a command line, and outside a menu that MOUNT opened it starts nothing.
  The menu turn goes only to the bundled UniMenu: with another drive or an
  overlay at Y:, MOUNT does not run whatever sits at that path.
  The overlay commands from DOS answer only while the webserver has switched
  them on. A `CHOOSER.TML` from the archive is read through the
  mounted drive, at most 16 KiB; labels lose their control characters and
  are cut at 60 characters. An entry is dropped when its program is not an
  8.3 path or carries `..`, control characters or `> < | & % ;`, and when
  its arguments run over 40 characters or carry control characters or
  those six.
- Lua scripts run sandboxed: no filesystem or process access, bytecode
  rejected, dangerous globals removed, instruction and pattern-complexity
  limits against runaway scripts.

If a security fix changes any of this, this file and the manual page above
are updated together with the fix.

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
<https://dosbox-automation.org/0.84-da3/automation/security/>.

The REST API gives full control over the emulated machine, which makes the
webserver an attack surface by design. The short version:

- Every API request needs a bearer token (64-char random hex, fresh per
  start, never fully logged, constant-time comparison). No default
  credential, no way to disable authentication.
- The webserver binds to localhost only by default. Host header validation
  rejects DNS rebinding. No CORS headers are set, OPTIONS preflight is
  refused. Request bodies are capped.
- Every MOUNT, BOOT, and drive-swap path is validated before a drive is
  constructed: paths must resolve, symlink components are rejected, system
  directories are blocked, and disk images must pass structural validation.
  With the webserver enabled, directory mounts are whitelist-restricted,
  and a one-way mount lock can freeze the mount configuration until
  restart. Whitelists are read from the primary config only, so a bundled
  game config cannot widen them.
- Lua scripts run sandboxed: no filesystem or process access, bytecode
  rejected, dangerous globals removed, instruction and pattern-complexity
  limits against runaway scripts.

If a security fix changes any of this, this file and the manual page above
are updated together with the fix.

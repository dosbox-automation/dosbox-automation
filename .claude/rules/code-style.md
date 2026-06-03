---
description: C++ code style rules
globs: src/**/*.{cpp,h}
---

# General principles

- No "easy fixes", "simple fixes", "quick fixes", any "something fixes" or any shortcut 
  that suppresses symptoms instead of addressing the root cause. Understand *why* something 
  is broken, then fix it properly.
- Safe and secure code is paramount. Write code defensively.
- Functions should validate their input. Fail early, fail loudly.
- Keep code readable and easy to maintain.
- Small, focused, pure functions; single responsibility per component
- Look for existing patterns in the codebase first. Don't reinvent the wheel.
- Self-documenting code; no restating the obvious in comments
- Prefer loose coupling and high cohesion
- Keep testability in mind
- New code or changes that have no unit tests: add meaningful unit tests.
- New unit tests should also cover potential exploits.
- When in doubt, keep code readable and understandable.

# Security posture

This is an emulator with a REST API. The webserver is an attack surface.

- All HTTP input is untrusted. Validate types, ranges, and sizes before use.
  Never pass raw request data into emulator internals without checking it first.
- Bounds-check array indices, buffer sizes, and numeric parameters from API
  requests. A malformed JSON payload must not crash or corrupt the emulator.
- Sanitize file paths from API input. Canonicalize with `realpath()` or
  `std::filesystem::canonical()` and reject anything outside allowed roots.
  Watch for `../`, symlinks, and null bytes.
- Cap resource consumption: request body sizes, event array lengths, allocation
  counts. No endpoint should allow an attacker to exhaust memory or CPU.
- The emulator core trusts its own internal state. The webserver layer does not
  trust anything that arrived over HTTP. The Bridge is the trust boundary:
  commands crossing it must carry already-validated data.

# Scope

These rules apply to `src/` only. Vendored code in `src/libs/` follows its own
conventions — do not reformat it.

# After any code change, verify:

1. `clang-format -i <changed files>` or `./scripts/tools/format-commit.sh`

# Language

- C++23; use the standard library over C-style alternatives
- Never use C++ iostreams
- Never use C string functions — only `std::string` and `std::string_view`
- Never write `using namespace std;`
- Emulator core (`src/hardware/`, `src/cpu/`, `src/dos/`): avoid exceptions.
  Use return values, error codes, or assertions. Performance and C interop
  matter here.
- Webserver layer (`src/webserver/`): exceptions are fine and already in use
  (httplib throws, json::parse throws, Bridge throws on timeout). Use them
  where they're natural. Catch at handler boundaries so a bad request
  returns an error response, not a crash.
- Avoid complex template metaprogramming and complex macros
- Use RAII and smart pointers for resource management
- Prefer `constexpr`, `static_assert`, lambdas, range-based loops
- Prefer pre-increment/decrement (`++i`, `--i`)
- Use namespaces for grouping, not name prefixes
- Restrict SDL to rendering, window management, OpenGL, input, and OS-level
  audio/MIDI — check if C++ stdlib or SDL already provides what you need
  before adding custom code

# Naming

- `UpperCamelCase` for types, free functions, methods, enums, constants
- `lower_snake_case` for variables, arguments, struct members, static functions
- Don't uppercase acronyms: `PngWriter`, not `PNGWriter`
- No Hungarian notation
- Always include unit suffixes: `delay_ms`, `cutoff_freq_hz`
- Old code uses `MODULENAME_FunctionName` for public interfaces — don't replace
  these with namespaces

# Types

- Use `auto` when the type is obvious from context or too verbose to spell out.
  Don't use it when it hides what you're working with.
- Prefer plain `int` for numbers; don't micro-optimise integer sizes
- Use `int64_t` for large numbers rather than `uint32_t`
- Use `intXX_t`/`uintXX_t` only for binary protocols; `size_t` only for stdlib interfaces
- Prefer enum classes
- Prefer `std::optional` over sentinel values or out-parameters
- Default to `std::vector`; use `std::unordered_map` over `std::map` unless ordering is needed
- Always initialise variables and struct members
- Maintain `const`-correctness throughout

# Includes & headers

- Sort includes per Google C++ Style Guide
- Header guards: `DOSBOX_HEADERNAME_H` or `DOSBOX_MODULENAME_HEADERNAME_H`
- New code: add `CHECK_NARROWING()` and include `"util/checks.h"`

# Comments

- Use `//` for block comments
- End-of-line comments only for tabular data
- Debug logging: wrap with `#if 0` / `#endif` or per-topic define switches

# Contributing to dosbox-automation

Thank you for your interest in contributing to dosbox-automation! There are many
ways to participate, and we appreciate all of them.

- [AI usage](#ai-usage)
- [Feature requests and bug reports](#feature-requests-and-bug-reports)
- [Do something with dosbox-automation](#do-something-with-dosbox-automation)
- [Contributing code](#contributing-code)
  - [General principles](#general-principles)
  - [Security posture](#security-posture)
  - [Language standard](#language-standard)
  - [Naming](#naming)
  - [Types](#types)
  - [Code formatting](#code-formatting)
  - [Code style](#code-style)
  - [Code style examples](#code-style-examples)
  - [Submitting code changes](#submitting-code-changes)
- [Documentation](#documentation)
- [Writing style](#writing-style)
- [Versioning](#versioning)
- [Tools](#tools)
  - [compile-commits](#compile-commits)
  - [clang-format](#clang-format)
  - [count-warnings](#count-warnings)


## AI Usage

This project uses tool assisted development. Your contributions are judged on merit,
not on who or what wrote them. The same rules apply to maintainers and
contributors alike: there are no double standards, where maintainer code gets a 
pass while outside contributors face extra scrutiny for being tool assisted. One set of
coding guidelines, documented here, applies to everyone.

If you use an LLM, we ship a set of instructions in this repository
(`.claude/` directory). If your tooling does not pick them up automatically,
instruct your model to read them.

Those instructions reference the same rules as this document and will cause your model 
to produce code that fits our codebase.

Submissions generated with LLMs must include:

- a declaration of AI use, including which model generated the code (family and version)
- a confirmation that the project's shipped code generation rules were followed


### Development process

For transparency, this how tool assisted development is used.
Contributors using LLMs for code generation should follow the same approach.

1. **Architecture and design decisions** are made by human software engineers.
   Major features start as design documents, before any code is written. The
   LLM does not decide scope, technical direction or trade-offs.

2. **Implementation** is specified by the developer with full context: what to
   build, which patterns to follow, what constraints apply, what to avoid. The
   AI stays within these set boundaries and is not permitted to cross them.

3. **Review and responsibility** rests with the developer. Every change must be
   read and understood before commit. If something is unclear, question it and
   have it explained before greenlighting. This applies to source code,
   commits, release notes, and any public-facing text. 

4. **Testing and verification** goes beyond generated unit tests. Every change
   requires direct testing, debugging, and review by the human developer.

This is not "generate and ship." It is closer to pair programming where one
partner is fast at typing but needs guidance, clear direction and regular correction.


## Feature requests and bug reports

If you find a [feature request][enhancement_label] you're interested in, leave
a comment. This will help us decide where to focus development effort.

Report bugs via our [bugtracker][issues]. Issues and requests sent via comments
on social media or private messages will very likely be lost.

[enhancement_label]: https://github.com/dosbox-automation/dosbox-automation/issues?q=is%3Aissue+is%3Aopen+label%3Aenhancement
[issues]: https://github.com/dosbox-automation/dosbox-automation/issues


## Do something with dosbox-automation

- Have a project using dosbox-automation as an integration runtime or do something great with it? Yes? Contact us,
  so we can build up a showcase page, linking back to your project!
  
- Take your time and audit our code, for security holes and bugs, ideally with patch to fix them. We believe
  in proper public, technical discourse about these things in order to make dosbox-automation a safer dosbox
  for everyone, who wants to play DOS games or uses it for automated usage, integration into launchers or
  other usage as an integration runtime for your own applications.
  
  The current list of open issues is available [here](https://github.com/dosbox-automation/dosbox-automation/issues?q=is%3Aissue+is%3Aopen+label%3Abug).


Code contributions, which improve dosbox-automation are welcome, but please read the section [Submitting code changes](#submitting-code-changes) first.

If you plan to work on a bigger feature, discuss it with us early by
creating a new issue ticket.


## Contributing code

These rules apply to code in `src/` and `include/` directories. Vendored libraries in `src/libs/` follow their own conventions: do not reformat them.

The rules below apply to new code landing in the `main` branch.


### General principles

- No shortcuts that suppress symptoms instead of addressing the root cause.
  Understand *why* something is broken, then fix it properly.
- Safe and secure code is paramount. Write code defensively.
- Functions should validate their input. Fail early, fail loudly.
- Keep code readable and easy to maintain.
- Small, focused, pure functions. Single responsibility per component.
- Look for existing patterns in the codebase first. Don't reinvent the wheel.
- Self-documenting code. Don't restate the obvious in comments.
- Prefer loose coupling and high cohesion.
- Keep testability in mind.
- New code or changes without unit tests: add meaningful unit tests.
  New unit tests should also cover potential exploits.


### Security posture

This is an emulator with a REST API. The webserver is an attack surface.

- All HTTP input is untrusted. Validate types, ranges, and sizes before use.
  Never pass raw request data into emulator internals without checking it.
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


### Language standard

We use C++23 while avoiding the more complex areas of C++ and object-oriented
design. To clarify:

- Avoid designing your code in a complex object-oriented style. This does not
  mean "don't use classes"; it means "don't use stuff like multiple
  inheritance, overblown class hierarchies, operator overloading, iostreams
  for stdout/stderr, etc."

- C++23 has a [rich standard library](https://en.cppreference.com/w/cpp/23),
  use it. We use [STL
  containers](https://en.cppreference.com/w/cpp/container),
  [std::filesystem](https://en.cppreference.com/w/cpp/filesystem), and various
  [concurrency constructs](https://en.cppreference.com/w/cpp/thread) in our
  code. All new code must use the C++ standard library instead of C
  arrays, C filesystem functions, and OS-specific concurrency APIs.

- Before using platform-specific, OS-level APIs, check if the C++
  standard library or SDL provides a cross-platform implementation already.
  If not, look into using battle-tested libraries instead of adding custom
  platform-specific code.

- In general, use well-established libraries for common tasks.

- Our use of SDL is restricted to the texture renderer fallback option, and
  interfacing with OS-specific APIs not covered by the C++ standard: window
  management, OpenGL context creation, input handling, and OS-level audio and
  MIDI APIs. Do not use SDL's thread, timer, filesystem, or similar APIs when
  the C++ standard library provides equivalents (prefer `std::thread`,
  `std::mutex`, `std::lock_guard`, `std::filesystem`, `std::atomic`, etc.)

- Restrict SDL-specific types to the files that directly interface with SDL.
  They should not spread across the codebase.

- Prefer [RAII](https://en.cppreference.com/w/cpp/language/raii) patterns for
  resource lifecycle management. Use [smart
  pointers](https://en.cppreference.com/w/cpp/memory) (`unique_ptr`,
  `shared_ptr`, `weak_ptr`). Raw C pointers should only be used as a last
  resort when interfacing with legacy DOSBox code.

- Use modern C++ features: `constexpr`, `static_assert`, lambda expressions,
  for-each and range-expression loops.

- Do not use C++ iostreams.

- Do not use C string functions or low-level C string manipulation in new
  code. Only use `std::string` and `std::string_view`.

- Emulator core (`src/hardware/`, `src/cpu/`, `src/dos/`): avoid exceptions.
  Use return values, error codes, or assertions. Performance and C interop
  matter here. Let errors like `std::logic_error` or `std::bad_alloc`
  terminate the process so they're caught during testing. The one valid use
  case for exceptions in core code is signalling construction failures in
  RAII-style lifecycle management: that's preferable to two-stage
  initialisation with a separate `Init()` method.

- Webserver layer (`src/webserver/`): exceptions are fine and already in use
  (httplib throws, json::parse throws, Bridge throws on timeout). Use them
  where they're natural. Catch at handler boundaries so a bad request returns
  an error response, not a crash.

- Outside these two areas, prefer not to throw exceptions unless there is a
  clear reason. When in doubt, don't.

- Don't micro-optimise, and avoid "clever code." Always optimise for clarity,
  maintainability, and readability first, and only deviate when warranted by
  performance measurements. Do not attempt to "optimise" anything before
  proving it is a bottleneck, and even then, only optimise things that cause
  measurable, user-observable issues during real-world usage.

- Avoid complex template metaprogramming. Simple templates are fine.

- Avoid complex macros. Write a `constexpr` function or a simple template
  instead when possible.

- Never write `using namespace std;`.

- Prefer namespaces in new code for grouping instead of name prefixes.


### Naming

- `UpperCamelCase` for types, free functions, methods, enums, constants.
- `lower_snake_case` for variables, arguments, struct members, static
  functions.
- Don't uppercase acronyms: `PngWriter`, not `PNGWriter`.
- No Hungarian notation.
- Always include unit suffixes: `delay_ms`, `cutoff_freq_hz`.
- Old code uses `MODULENAME_FunctionName` for public module interfaces. Do
  **not** replace these with namespaces.


### Types

- Use `auto` when the type is obvious from context or too verbose to spell
  out. Don't use it when it hides what you're working with.
- Be `const`-correct. Keep `const` in sync between declarations and
  definitions.
- Prefer plain `int` for numbers. Don't micro-optimise integer sizes. Use
  `int64_t` for large numbers rather than `uint32_t`. Only use
  `intXX_t`/`uintXX_t` for binary protocols, and `size_t` only for stdlib
  interfaces.
- Prefer enum classes.
- Prefer `std::optional` over sentinel values or out-parameters.
- Default to `std::vector`. Use `std::unordered_map` over `std::map` unless
  ordering is needed.
- Always initialise all variables and struct members.
- Struct-nesting is encouraged as logical groupings improve readability.


### Code formatting

For new code, follow K&R style. See [Linux coding style] for examples and
advice on good C coding style.

Following all formatting details by hand is tedious; we use a custom
[clang-format](https://clang.llvm.org/docs/ClangFormat.html) ruleset to make
it clear. See the [clang-format](#clang-format) section to learn how.

[Linux coding style]: https://www.kernel.org/doc/html/latest/process/coding-style.html


### Code style

1. Prefer pre-increment/decrement whenever possible: `++i` and `--i`.

2. Sort includes according to [Google C++ Style
   Guide](https://google.github.io/styleguide/cppguide.html#Names_and_Order_of_Includes).

3. Enable narrowing checks in new code and when reworking a file by adding
   `CHECK_NARROWING()` and including `"util/checks.h"` (search for
   `CHECK_NARROWING()` for examples).

4. Use header guards in the format: `DOSBOX_HEADERNAME_H` or
   `DOSBOX_MODULENAME_HEADERNAME_H`.

5. Use `//` for block comments. End-of-line comments only for tabular data.

6. Surround debug logging with `#if 0` / `#endif` pairs instead of commenting
   log statements out, or better yet, introduce define switches per topic
   (e.g., `#define DEBUG_VGA_DRAW`).


### Code style examples

```cpp
enum class CaptureState { Off, Pending, InProgress };

// Static function
static int32_t get_next_capture_index(const CaptureType type);

// Public function prefixed by MODULENAME_
void CAPTURE_AddFrame(const RenderedImage& image, const float frames_per_second);

// Static inline struct; always initialise members
static struct {
    std_fs::path path     = {};
    bool path_initialised = false;

    struct {
        CaptureState audio = {};
        CaptureState midi  = {};
        CaptureState video = {};
    } state = {};
} capture = {};

// Public struct; always initialise members
struct State {
    CaptureState raw      = {};
    CaptureState upscaled = {};
    CaptureState rendered = {};
    CaptureState grouped  = {};
};

// Class definition
class PngWriter {
public:
    PngWriter() = default;
    ~PngWriter();

    // Use const args whenever possible
    bool InitRgb888(FILE* fp, const uint16_t width, const uint16_t height,
                    const Fraction& pixel_aspect_ratio,
                    const VideoMode& video_mode);

    void WriteRow(std::vector<uint8_t>::const_iterator row);

    // Mark methods as const whenever possible
    bool IsValid() const;

private:
    // Always initialise members
    State state = {};

    std_fs::path rendered_path = {};
};
```

Check out these files for further examples:

- [src/audio/clap/*](https://github.com/dosbox-automation/dosbox-automation/tree/dosbox-automation/src/audio/clap) (all files)
- [src/midi/midi_soundcanvas.cpp](https://github.com/dosbox-automation/dosbox-automation/blob/dosbox-automation/src/midi/midi_soundcanvas.cpp)
- [src/midi/midi_soundcanvas.h](https://github.com/dosbox-automation/dosbox-automation/blob/dosbox-automation/src/midi/midi_soundcanvas.h)
- [src/capture/image/*](https://github.com/dosbox-automation/dosbox-automation/tree/dosbox-automation/src/capture/image) (all files)


### Submitting code changes

Submit code changes via GitHub PRs. The key points:

1. Group related changes into one commit. A fix that touches formatting or
   docs around it is one commit, not three. When reformatting larger code
   blocks (more than 10-20 lines), create a separate reformat commit first,
   followed by the actual code change in a second commit.

2. Make sure all your commits compile individually. This is critical for
   bisecting, and we enforce it. Run `scripts/tools/compile-commits.sh` to
   compile all commits of the PR you're working on.

3. New source files written for this project get our two-line license
   header (see any file under `src/lua/` or `src/webserver/` for the
   format). Files inherited from DOSBox Staging keep their existing SPDX
   headers as they are; do not convert between the two styles.


#### Commit messages

Read [How to Write a Git Commit Message]. Then read it again, and follow
the seven rules. One deviation from that guide: we do not capitalize the
subject line.

No commit prefixes. Just say what changed. The first line is a short summary,
the body explains the why if it's not obvious from the change itself.

Never add Co-Authored-By lines to commits.

[How to Write a Git Commit Message]: https://chris.beams.io/posts/git-commit/


#### Commit messages for patches authored by someone else

- If possible, preserve the code formatting used by the original author in
  your first import commit. Style changes and any other code changes should be
  in subsequent commits.

- Record the correct author name, date when the original author wrote the
  patch (if known), and sign it:

  ```
  $ git commit --amend --author="Original Author <mail-or-identifier>"
  $ git commit --amend --date="15-05-2003 11:45"
  $ git commit --amend --signoff
  ```

- Record the source of the patch so future programmers can find the context
  and discussion surrounding it. Use the following trailer:

  ```
  Imported-from: <url-or-specific-identifier>
  ```

For an example of a commit that followed all of these rules, see commit
[ffe3c5ab](https://github.com/dosbox-automation/dosbox-automation/commit/ffe3c5ab7fb5e28bae78f07ea987904f391a7cf8):

    $ git log -1 ffe3c5ab7fb5e28bae78f07ea987904f391a7cf8


## Documentation

The user manual and the website at [dosbox-automation.org](https://dosbox-automation.org)
live in a separate project, not in this repository. This repository only
carries developer-facing documentation as plain Markdown under `docs/`.

If a code change alters user-visible behavior, a config setting, an API
route, or a Lua function, say so in your PR description so the manual can
be updated to match. Setting descriptions in the manual are copied verbatim
from the code, so changing a setting's help text is a manual change too.


## Writing style

All written output: commits, code comments, docs, PR descriptions.

- Be direct and terse. No verbose noun phrases like "Implement filtered tag
  retrieval with UI integration."
- No filler words: "enhance", "leverage", "utilize", "streamline", "ensure".
- Don't use em-dashes or other typographic artifacts. Standard characters
  only. No emojis.
- Vary sentence structure, use shorthand where natural, skip artificial polish.
- Don't group unrelated changes into one kitchen-sink commit.

This isn't about deception (we're open about AI usage). It's about writing
quality. Good writing doesn't scream "a model wrote this," regardless of
whether one did.


## Versioning

The project name is "dosbox-automation", written exactly in that convention.

Version numbering follows the upstream base version with a "-da" release
suffix. Example: if the upstream base is 0.83 and the release number is 3,
the version is `0.83-da3`.

Full product name example: `dosbox-automation 0.83-da3`.


## Tools

### compile-commits

Make sure that all your commits compile individually. This is critical for
bisecting, and we enforce it. Run `scripts/tools/compile-commits.sh` to
compile all commits of the PR you're working on.


### clang-format

Distributed with the Clang compiler and integrates with [many programming
environments](https://releases.llvm.org/10.0.0/tools/clang/docs/ClangFormat.html).

Outside of your editor, format code by invoking the tool directly:

    $ clang-format -i file.cpp

Better not to reformat entire files at once. Target specific line ranges
(run `clang-format --help`), or use our helper script to format C/C++ code
touched by your latest commit:

    $ git commit -m "Edit some C++ code"
    $ ./scripts/tools/format-commit.sh

Run `./scripts/tools/format-commit.sh --help` for available options.


#### Vim integration

Download `clang-format.py` somewhere and make it executable:

    $ curl "https://raw.githubusercontent.com/llvm/llvm-project/main/clang/tools/clang-format/clang-format.py" > ~/.vim/clang-format.py
    $ chmod +x ~/.vim/clang-format.py

Add the following lines to your `.vimrc`:

    " Integrate clang-format tool
    map <C-K> :py3f ~/.vim/clang-format.py<cr>
    imap <C-K> <c-o>:py3f ~/.vim/clang-format.py<cr>

Read the documentation inside `clang-format.py` if your OS is missing Python 3
support.


#### MSVC integration

[ClangFormat extension on VisualStudio Marketplace](https://marketplace.visualstudio.com/items?itemName=LLVMExtensions.ClangFormat)


### count-warnings

Our quality gating tracks the number of warnings per OS/compiler. To see a
summary of warnings in your build, do a clean build and use the script
`./scripts/ci/count-warnings.py`:

    make -j "$(nproc)" |& tee build.log
    ./scripts/ci/count-warnings.py build.log

Run `./scripts/ci/count-warnings.py --help` for available options.

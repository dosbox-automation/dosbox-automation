# Documentation

The user manual and website live in a separate project
(augrudottir-dosbox-automation-web, published at dosbox-automation.org).
This repository carries only developer-facing Markdown under docs/.

When a change alters user-visible behavior, a config setting, an API
route, or a Lua function, flag it so the manual gets updated. Setting
descriptions in the manual are copied verbatim from the code, so
changing a setting's help text is a manual change too. Also update the
API and Lua tables in CLAUDE.md.

# Writing style

All written output - commits, code comments, docs, PR descriptions - must
read like a human developer wrote it. Avoid telltale AI patterns:

- No Conventional Commits prefixes. Just say what changed.
- No verbose noun phrases. Be direct and terse.
- No kitchen-sink commits with comma-separated unrelated changes.
- No filler vocabulary: "enhance", "leverage", "utilize", "streamline",
  "ensure".
- No em-dashes or typographic artifacts. Standard characters only.
  No emojis.
- Vary sentence structure, use shorthand where natural, skip artificial
  polish.

This isn't about deception (we're open about AI usage), it's about
writing quality. Good writing doesn't scream "a model wrote this."

Target audience for user-facing text is regular users, not developers.
No condescending or nannying tone. Plain language that doesn't insult
knowledgeable readers.

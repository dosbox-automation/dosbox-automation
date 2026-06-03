---
description: Rules for verifying and writing mkdocs documentation
globs: website/**/*.md, website/**/*.yml, website/**/*.html
---

# After any docs/website change, run both before presenting results:

1. `cd website && mkdocs build` -- zero warnings required
2. `cd website && ../scripts/linting/verify-markdown.sh` -- zero warnings required

# Writing style

All written output — commits, code comments, docs, PR descriptions — must read like
a human developer wrote it. Avoid telltale AI patterns:

- **No Conventional Commits prefixes** (`feat:`, `fix:`, `docs:`) - just say what changed
- **No verbose noun phrases** ("Implement filtered tag retrieval with UI integration") - be direct and terse
- **No kitchen-sink commits** with comma-separated unrelated changes
- **No filler vocabulary**: "enhance", "leverage", "utilize", "streamline", "ensure"
- **No em-dashes or typographic artifacts**: don't use any AI typical typographical characters, but 
  standard characters, humans use. No Emojis.
- **Vary sentence structure**, use shorthand where natural, skip artificial polish

- This isn't about deception (we're open about AI usage), it's about writing quality.
  Good writing doesn't scream "a model wrote this."

- Target audience are regular users and not developers. Do not write in a condescending 
  nor nannying language. Explain in easy to understandable language and do not insult
  more knowledgeable people.

- Before adding new formatting or markup patterns, find an existing example in
  the docs and match it

- Most manual pages are divided into two parts: a conversational guide, and a
  reference section (under "Configuration settings")

- We document configuration sections topically; it's okay if settings from the
  same config section are documented in different chapters

- The "Configuration settings" section must copy the setting's description
  from the code verbatim; that's the baseline, then we can enhance it with
  additional info if needed

- Look for opportunities to create cross-references across chapters

- Look for opportunities to include interesting trivia, e.g. games that use a
  given feature (use web search for research). If you add trivia, fact-check them
  first with a web search and at least 2-3 sources have to confirm trivia.

- All referenced games must be linked to a valid pcgamingwiki page.

# If editing SASS

- Regenerate CSS from `extra-scss/extra.scss` and commit both `.scss` and `.css`

# Version bumps

Update ALL of these together:
- Rename `website/docs/<old>/` directories
- `website/docs/versions.json`
- `mkdocs.yml`: nav paths, redirect targets, exclude globs
- `hooks/offline.py`: `DUMMY_INDEX_PAGES`

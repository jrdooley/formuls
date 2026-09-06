# Agent memory (copied from outside the working branch)

Working copies of the notes a Claude session keeps for itself, which otherwise
live in `~/.claude/projects/<project>/memory/` and never reach the repository.
Copied here per the convention in `docs/ai-action-summary/ai-action-summary.md`:
"copy over any artifacts generated that are currently outside the working
branch."

These are a **snapshot**. The live copies are what a session actually reads, so
the two can drift; re-copy them when they change.

- [Keep cache-read cost down](keep-cache-read-cost-down.md) — cache reads were 79% of a $30.88 session; fewer turns, scripts to files not heredocs.
- [Verify, don't assert (formuls)](formuls-verify-dont-assert.md) — compile it, measure it, load the patch; a miscount by eye once reset the branch.

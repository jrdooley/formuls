---
name: keep-cache-read-cost-down
description: "James asked me to cut cache-read spend; it was 79% of a $30.88 session. Fewer turns, and stop parking bulk in context."
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 3a526a53-6893-49a5-9ed3-319a823aab4e
  modified: 2026-09-06T22:01:43.751Z
---

Asked on 6 September 2026, after measuring the formuls audit session: **$30.88
total, of which cache reads were $24.45 (79%)**. Output was only 14%. Reduce
this.

**Why:** cache-read cost is roughly `Σ(context size at each turn)` — every turn
re-reads the entire conversation prefix. That session ran **230 turns** with
context growing 77k → 358k tokens, so the back half alone cost 67% of the
spend. Turn count and context size multiply; both are mine to control.

**How to apply:**

- **Fewer, denser turns.** 195 of 230 turns were Bash calls, many single greps
  that could have been combined. Every extra turn pays for the whole context
  again. Batch independent checks into one call.
- **Write scripts to the scratchpad and run them by path.** Inline `python3
  <<'PY'` heredocs cost **43,896 tokens of tool arguments** that session, and
  every one is re-read on every later turn. `python3 /tmp/x.py` is ~10 tokens.
  This was the single most avoidable cost.
- **Redirect bulk output to a file, then grep it.** The ten largest tool results
  were 57% of all result volume, one of them 18k tokens. Pipe to a file and pull
  only the lines that matter.
- **Don't re-dump.** I printed the same `ffx.lib` and `_main.pd` regions, and
  full diffs, several times. Read once, refer back.

**Not at the expense of rigour.** The same session's value came from actually
compiling, measuring struct sizes, and running two real app builds to compare —
that verification is why the findings held up. Cut the transport overhead
(heredocs, re-dumps, one-line turns), not the checking. See
[[formuls-verify-dont-assert]].

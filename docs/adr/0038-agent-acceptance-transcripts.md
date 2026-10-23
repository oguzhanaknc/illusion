# 0038. Agent acceptance tests from replayable transcripts

- Status: Accepted
- Date: 2026-10-23

## Context

The brief's acceptance criterion is that an AI agent builds a Tetris-like game through the editor without
human help. A live agent is slow, costs money and is not deterministic, so it cannot gate pull requests. CI
still has to prove on every build that the whole workflow an agent depends on works: creating content,
saving, restarting, and later building modules, playing and hot reloading. The proof must not depend on an
LLM, must exercise the real protocol (ADR 0037) rather than C++ calls, and must explain a failure well
enough to fix it without a debugger.

## Decision

- **Transcripts are JSON Lines.** Each line is a step: `{call, params, capture, expect, requires,
  otherwise}`, `{restart: {arguments}}`, `{frames: n}` (which calls `play.step`; never a wall-clock wait) or
  `{comment}`. `capture` stores values from a reply by JSON Pointer; `${name}` in later params, expectations
  and restart arguments is replaced by them (a string that is exactly `${name}` takes the captured JSON
  value), so recorded ids never appear literally.
- **Matchers.** Expectations are `{result: m}` or `{error: m}` (default: any success). An object matches an
  object with at least its members, an array matches element by element, scalars match by value; one-member
  `$` objects are operators: `$approx`, `$regex`, `$length`, `$exists`, `$oneOf`, `$contains`. A mismatch
  reports the JSON Pointer, the expected and the actual value.
- **Nothing is skipped.** A step marked `requires: "gpu"` runs without a GPU too and must then match
  `otherwise` (by default error 1400). A replay stops at the first diverging step, because later steps
  depend on it; a checks file groups steps by `check` id, and a failed check moves on to the next check.
- **The contract and the files.** `tests/acceptance/tetris/SPEC.md` is the contract (and, in M7x, the live
  agent's prompt). `scripted.jsonl` is written by us and grows with each milestone that adds the commands it
  needs; `checks.jsonl` holds the checks C1–C8, the same for replayed and live runs. In M7x a normalised
  `recorded.jsonl` of a real agent session joins it.
- **Runner.** `illusion::editor_client` provides `RpcClient` (socket, endpoint file, or a launched editor over
  stdio) and `TranscriptRunner`; `illusion_ctl replay <transcript> --checks <checks> --temp-workspace`
  launches `illusion_editor --headless --rpc-stdio --deterministic --seed 1 --no-gpu --workspace <temp>`,
  replays, runs the checks, prints the first failure with the reply, and keeps the workspace when something
  failed. The CTest `acceptance.tetris.board` (label `acceptance`, Release and RelWithDebInfo) runs on every
  build job, including the sanitizer jobs.
- **Live agent.** A nightly workflow on `main` (never on pull requests or forks) runs a pinned Claude Code
  CLI with only the MCP tools of the editor against `SPEC.md` and the same checks (M7x; needs an API key
  from the product owner).

## Consequences

- Every PR proves the agent workflow end to end through the protocol, deterministically and without network
  access; protocol changes that break agents break the transcript and must come with a version note.
- The transcript is only as broad as the editor: in M7j it covers the static board (C1, C3, C4). Module
  builds, input assets, play mode, rendering and hot reload join in the milestones that add their commands.
- Captures and matchers keep transcripts stable across id changes and harmless result additions; exact
  results (hashes, byte-identical saves) are asserted where they matter.
- Replaying through `illusion_mcp` (`--via-mcp`, both MCP eras) comes with the MCP bridge (M7k).

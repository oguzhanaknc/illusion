# 0001. Record architecture decisions

- Status: Accepted
- Date: 2026-10-10

## Context

illusion is developed by humans and AI agents across many sessions. Agents start without memory of
earlier discussions, so the reasoning behind a design must be written down where it can be found, or it
will be relitigated or accidentally undone.

## Decision

Significant decisions are recorded as Architecture Decision Records in `docs/adr/`, one file per
decision, numbered sequentially, using `0000-template.md`. A decision is significant when it is hard to
reverse or affects several modules: dependencies, data formats, threading, memory policy, public API
conventions, platform support.

ADRs are immutable once accepted, except for their status line. Changing a decision means writing a new
ADR that supersedes the old one.

## Consequences

- Reviews can point to a specific ADR instead of re-arguing a design.
- Every ADR must also be listed in `docs/content/{en,tr}/architecture/decisions.md`.
- Writing ADRs costs time; trivial or local decisions belong in code comments instead.

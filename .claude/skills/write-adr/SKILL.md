---
name: write-adr
description: Record an Architecture Decision Record for illusion. Use when choosing or changing a dependency, data format, threading or memory policy, public API convention, platform support, or anything hard to reverse.
---

# Writing an ADR

1. Check `docs/adr/` for an existing ADR on the topic. If your change contradicts an accepted ADR, you
   must write a new ADR that supersedes it (and set the old one's status to
   `Superseded by [NNNN](NNNN-title.md)`), never edit the old decision text.
2. Copy `docs/adr/0000-template.md` to `docs/adr/NNNN-imperative-title.md` using the next free number.
3. Fill in:
   - **Context:** the problem, constraints (performance, platforms, licences, existing code) and the
     options you considered with their trade-offs.
   - **Decision:** concrete and checkable ("Components are identified by fnv1a64 of the type name"),
     not aspirational.
   - **Consequences:** costs, risks, follow-up work (add it to ROADMAP.md) and what would make us revisit.
4. Status `Proposed` while the change is in review, `Accepted` in the commit that implements it. Date in
   ISO format.
5. Add a row to the table in `docs/content/en/architecture/decisions.md` **and**
   `docs/content/tr/architecture/decisions.md` (Turkish title, same link).
6. Reference the ADR from code comments where the decision is implemented, e.g.
   `// See docs/adr/0003-type-identity-across-modules.md`.

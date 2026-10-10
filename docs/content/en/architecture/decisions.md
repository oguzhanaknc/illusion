# Decisions

Significant design decisions are recorded as Architecture Decision Records (ADRs) in
[`docs/adr`](https://github.com/oguzhanaknc/illusion/tree/main/docs/adr). Each record describes the
context, the decision and its consequences. ADRs are written in English only; they are engineering
records rather than user documentation.

| ADR | Title |
| --- | --- |
| [0001](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0001-record-architecture-decisions.md) | Record architecture decisions |
| [0002](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0002-dependency-management.md) | Pinned dependencies with FetchContent |
| [0003](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0003-type-identity-across-modules.md) | Type identity and services across modules |
| [0004](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0004-archetype-ecs.md) | Archetype-based ECS with chunked SoA storage |
| [0005](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0005-job-system.md) | Job System with a bounded lock-free queue |
| [0006](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0006-documentation-languages.md) | Bilingual documentation |

To propose a new decision, copy `docs/adr/0000-template.md` and follow the
[contributing guide](../contributing.md#architecture-decision-records).

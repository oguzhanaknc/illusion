# 0007. Single-threaded event bus with immediate and queued delivery

- Status: Accepted
- Date: 2026-10-11

## Context

Windows, input, gameplay, audio and the editor need to notify each other without compile-time
dependencies. Requirements: no per-frame allocations, deterministic order, safe re-entrancy (handlers
that publish, subscribe or unsubscribe), and a way to process OS events at a defined point of the frame.

Options considered: virtual listener interfaces per event (boilerplate, tight coupling), signals/slots
per object (fine-grained but every sender must expose a signal), a global typed bus with an optional
thread-safe queue.

## Decision

- One `EventBus` class; events are plain structs identified by `typeHash<E>()` (ADR 0003).
- `publish()` delivers synchronously in subscription order. `enqueue()` + `dispatchQueued()` delivers
  later, in enqueue order, with double buffering so events enqueued during dispatch wait for the next
  call. Queued events must be trivially copyable and are stored in a reusable byte buffer.
- Handlers are `InplaceFunction<void(const void*), 56>`; registration returns an RAII `Subscription`.
- Re-entrancy: while a channel dispatches, new subscriptions go to a pending list and unsubscriptions
  only deactivate entries; both are applied when the outermost dispatch of that channel ends.
- The bus is single-threaded (asserted in Debug). Cross-thread communication goes through Jobs or
  explicit queues owned by the subsystem.
- Windows enqueue their events; the main loop calls `dispatchQueued()` once per frame.

## Consequences

- Systems communicate without knowing each other; event structs are the only shared contract.
- Publishing costs one hash-map lookup plus the handler calls; benchmarks will cover it once a hot path
  uses events.
- A thread-safe variant, if needed (for example for asset streaming notifications), will be a separate
  type rather than adding locks to every publish.
- Subscriptions must not outlive the bus (asserted in Debug).

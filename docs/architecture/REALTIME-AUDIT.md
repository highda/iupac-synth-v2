# Release realtime call-path audit

This is the source-level companion to V7's production allocation and concurrency tests. Behavioral proof remains `RealtimeAllocationTests`, processor tests, transition stress, ASan/UBSan and the separate TSan run orchestrated by `scripts/verify-release.sh`.

## Audio-owned path

`PluginProcessor::processBlock` reads the eight cached APVTS atomics, converts host MIDI into fixed-capacity event storage, drains the coordinator publication channel, and calls `Engine::render`. `Engine::render` chunks oversized blocks into storage allocated by `prepare`, applies sample-offset events, processes the two preallocated transition banks and writes the caller's buffers. Module processing uses fixed arrays and JUCE DSP objects prepared off the audio thread.

The reachable path contains no JSON, string construction, file/process/network I/O, logging, mutex acquisition, helper call or ownership reclamation. Patch decode, validation, graph compilation, state serialization and chemistry/discovery work remain coordinator-thread operations. Publication copies complete bounded values through fixed `AbstractFifo` slots; audio never deletes a published object. Allocation instrumentation covers render and plugin `processBlock` after prepare, including MIDI and patch-transition stress.

## Bounded resources and failure behavior

- At most 4,096 MIDI events are consumed per host block; overflow requests bounded all-notes-off and increments a counter.
- At most two prepared banks are audible. A transition retains one newest pending target rather than creating a third bank.
- Sixteen voices, eight graph nodes and sixteen matrix rows are fixed compatibility bounds (graph/row caps raised to 11 nodes/32 edges/24 rows by D6 on 2026-09-18; the audit's realtime conclusions are unaffected).
- Zero-sized blocks return without mutation; oversized blocks are chunked without allocating.
- Non-finite and over-limit samples are counted and clamped by the final safety guard.
- `releaseResources`, state callbacks, editor work and helper cancellation are outside `processBlock`; concurrency ownership is exercised separately under TSan.

The V8 report samples RSS after warmup, at the midpoint, and after the stress.
The no-growth assertion compares midpoint with completion so demand-paged
preallocated banks and libraries are resident before the measurement interval;
the final working-set ceiling remains 128 MiB.

Any future call reachable from `processBlock` must preserve these properties and extend the production-path allocation test when it introduces a new branch.

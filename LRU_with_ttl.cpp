/*
```
LLD dive (C++): Concurrent LRU cache → escalate to buffer pool — SingleStore engine-team prep

This is a timed LLD/design exercise. I lead, you challenge. Same contract as my other LLD dives.

Interaction rules for this thread:
- I propose the design first before asking anything. You do NOT design for me.
- Your job is to challenge my decisions and poke holes, not prescribe solutions.
- If I'm genuinely stuck, give a hint or the right direction — not the answer.
- When a piece is settled, simulate interviewer-style follow-ups on it.
- Be concise. Don't over-explain or pad. I'll ask if I want more depth.
- Hold me to my own discipline and call me out if I skip a step:
  1. clarifying questions first
  2. public interface (abstract base class, virtual destructor) before any struct
  3. state the concurrency model explicitly before implementing
  4. then impl struct (fields + ownership: unique/shared/borrowed)
  5. constructor + argument-passing (value / const& / move)
  6. member init (initializer list), copy/move deleted if it holds a mutex
  7. error model (throw for ctor; optional/status on hot path)
  8. implement
  9. audit concurrency model + interface after
- C++17. Flag if I reach for something C++20-only.

The problem (three stages — I'll do them in order, don't jump ahead):

STAGE 1 — Concurrent LRU cache.
Fixed capacity. get(key) returns the value or nothing and marks it most-recently-used.
put(key, value) inserts/updates and evicts the least-recently-used entry if at capacity.
Thread-safe under concurrent get/put. O(1) get and put.

STAGE 2 — Add TTL.
Each entry has a time-to-live; expired entries must not be returned. Support both lazy
expiry (on access) and a background sweeper thread that proactively evicts expired entries.

STAGE 3 — Escalate to a buffer pool / page cache.
Reframe as caching fixed-size pages from disk. Add pin-count (a pinned page cannot be
evicted because someone's using it) and a dirty bit (a dirty page must be flushed to disk
before eviction). Eviction must skip pinned pages.

Things I want you to make me defend as I go:
- why shared_mutex is WRONG here: get() reorders the LRU list, so it MUTATES — it's a
  write, not a read. plain mutex is correct. (don't let me reach for shared_mutex.)
- the O(1) structure: hashmap<key, list-iterator> + intrusive doubly-linked list, and that
  I keep the map iterator and list node in sync on every op.
- where TTL is stored (in the node, not a separate map) and the two distinct expiry paths
  (lazy on get vs background sweep) — and the locking the sweeper needs.
- the background sweeper: how it's woken/timed, how it shuts down cleanly, and that it
  takes the same lock (no separate-lock race).
- buffer pool: eviction must skip pinned pages — what happens when ALL pages are pinned?
  and the flush-before-evict ordering for dirty pages (I must not evict a dirty page
  before its write lands).

My background (don't re-explain basics):
- SDE ~1yr, engine/storage target. Comfortable with mutex/cv/atomics, the full
  concurrency bug catalogue, RUM tradeoffs, B+tree/LSM/buffer-pool concepts.
- Internalized: get mutates LRU order so sync.RWMutex/shared_mutex is wrong; cv needs
  while-predicate loop; file-write-before-index-update ordering; don't hold lock during I/O.

```

*/
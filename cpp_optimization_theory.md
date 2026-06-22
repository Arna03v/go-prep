# C++ Optimization Round — Theory Reference

---

## TIER 1 — Algorithmic / Query-Specific
> Apply first. These give the biggest speedup (10x–100x). No micro-opt will compensate for a missed algorithmic fix.

---

### A. Join Algorithms

The most important concept for a query-shaped problem. A naive simulation almost certainly has a nested loop join. That is your 20-second culprit.

| Algorithm | Complexity | When it appears |
|---|---|---|
| Nested Loop Join | O(N × M) | Default naive implementation — double `for` loop |
| Hash Join | O(N + M) | The primary fix — build hash table on smaller table, probe with larger |
| Sort-Merge Join | O(N log N + M log M) | When data is already sorted, or sort cost is amortized across multiple queries |

**Hash Join — the canonical implementation:**
```cpp
// Build phase — always on the SMALLER table
std::unordered_map<int, std::vector<RowB>> ht;
ht.reserve(table_b.size());
for (const auto& row : table_b) {
    ht[row.key].emplace_back(row);
}

// Probe phase — linear scan over the larger table
for (const auto& row_a : table_a) {
    auto it = ht.find(row_a.key);
    if (it != ht.end()) {
        for (const auto& row_b : it->second) {
            // emit output row
        }
    }
}
```

**Key rules:**
- Always build on the smaller table — less memory, better cache behavior
- Hash join is the default correct answer when data is unsorted and fits in memory
- Duplicate keys on the build side: `unordered_map<key, vector<Row>>` handles this

---

### B. Join Ordering (multi-way joins)

When 3+ tables are joined, the order is independent of the join algorithm and matters significantly.

**Rule:** Sort join pairs by estimated output cardinality, ascending. Join the pair that produces the fewest output rows first. Each subsequent join receives a smaller left-hand input.

**Why:** Intermediate result size compounds. Joining A × B first when A × B is large poisons every downstream join.

**Signal in code:** Joins chained in arbitrary order with no regard for table sizes.

---

### C. Pre-Aggregation

**Late aggregation (slow):**
```
JOIN fully → materialize all output rows → GROUP BY
```
Intermediate result can be orders of magnitude larger than the final aggregate.

**Pre-aggregation (fast):**
```
GROUP BY on one table first → reduces cardinality → JOIN the smaller result
```
Only valid when join semantics allow it (non-explosive joins, aggregation keys preserved through the join).

**Hash aggregation — the canonical pattern:**
```cpp
struct Acc { int64_t sum = 0; int count = 0; };
std::unordered_map<int, Acc> acc;
acc.reserve(estimated_groups);
for (const auto& row : table) {
    auto& a = acc[row.key];
    a.sum += row.value;
    a.count++;
}
```

---

### D. Filter / Predicate Pushdown

Apply `WHERE` predicates before joins, not after. Fewer rows entering the join = fewer hash table probes.

**Rule:** If you see a filter applied to a post-join result when it references only one input table (not necessarily), that is the bug. Push it before the join.
If raw data is being loaded into memory, sent over a network, or joined before it is filtered, predicate pushdown is not happening.
The Bad Pattern (Filtering in Memory)

#### Bad: Bypassing the database's filtering engine
The database has to scan and transmit millions of users over the network
`all_users = db.query(User).all()  # Pulls everything!`

The "predicate" is evaluated inside the application CPU
`active_users = [u for u in all_users if u.status == "ACTIVE"]`


The Good Pattern (Pushed Down to DB)

#### Good: The filter is evaluated directly at the storage level
Only the matching rows are sent over the network
`active_users = db.query(User).filter(User.status == "ACTIVE").all()`

How to spot it: Look for code that fetches a list (.all(), .toList(), .collect()) and immediately runs a .filter(), .where(), or a for/if loop over it.

**Predicate ordering within a filter:** Put the most selective predicate (eliminates most rows) first, cheapest to evaluate first.

---

### E. Late Materialization / Projection Pushdown

Don't copy entire row structs through the join pipeline.

**Symptom:** Full struct copied in the build phase, but only 2 out of 10 fields are used downstream.

**Fix:** Only carry the columns needed at each stage. Fetch remaining columns after filtering is complete. This directly reduces memory bandwidth and cache pressure in the hot path.

---

### F. Bloom Filter as Pre-Filter *(bonus — signals engine-level thinking)*

Build a bloom filter on the build side before the hash join. Use it to cheaply reject probe-side rows that definitely won't match before doing the more expensive hash table lookup.

**When it matters:** High-selectivity joins where most probe rows don't match. The bloom filter's false-positive rate means a small number of rows still go to the hash table unnecessarily — acceptable.

**Tradeoff to state:** Extra memory for the bloom filter bitmap; build cost amortized over probe phase.

---

## TIER 2 — C++ Performance Patterns
> Apply after Tier 1 is exhausted, or as the primary focus if the problem is not query-shaped.

---

### G. Memory Allocations & Copy Overheads

**Hunt for:**
- `new` / `malloc` inside tight loops
- `std::string` copies in hot paths
- `std::vector` push_back without reserve (resizing = O(N) copy events)

**Fixes:**
```cpp
// reserve before build loops
vec.reserve(expected_size);

// emplace_back constructs in-place, avoids a copy
vec.emplace_back(args...);

// string_view avoids copy — read-only reference into existing string
void process(std::string_view key);

// move semantics when transferring ownership into output
output.push_back(std::move(row));

// const& to avoid accidental copies in range loops
for (const auto& row : table) { ... }
```

Summary table:
- auto row : copies the data and the data is mutable
- auto& row : data is not copied (holds a reference to the data) and the data is mutable
- const auto& row : data is not copied (holds a reference to the data) and the data is immutable


---

### H. Data Structure Selection

| Wrong | Right | Why |
|---|---|---|
| `std::map` | `std::unordered_map` | O(log N) vs O(1) average lookup |
| `std::set` | `std::unordered_set` | Same — membership testing |
| `std::list` | `std::vector` | Cache-line hostile vs contiguous memory |
| `unordered_map` without reserve | `unordered_map` with `reserve(n)` | Prevents O(N) rehashing events during build |
| `std::map<std::tuple<...>>` for GROUP BY | Struct with custom hash | O(log N) vs O(1) aggregation |

---

### I. String Key → Integer Key Interning

`std::string` keys in joins or aggregations are expensive: slow hash, slow comparison, heap allocation.

**The real fix (not just string_view):**
1. Pre-scan all keys in a single pass, assign integer IDs via a dictionary `unordered_map<string, int>`
2. Replace all string keys in the working tables with their integer IDs
3. Run the entire join and aggregation on integers

String operations happen once at ingestion; all hot-path work operates on integers.

---

### J. Composite Key Hashing

GROUP BY on multiple columns needs a composite key. Naive approaches have silent performance traps.

**Option 1 — Custom hash struct:**
```cpp
struct Key { int a; int b; };
struct KeyHash {
    size_t operator()(const Key& k) const {
        size_t h = std::hash<int>{}(k.a);
        h ^= std::hash<int>{}(k.b) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
std::unordered_map<Key, Acc, KeyHash> acc;
```

**Option 2 — Pack into uint64_t** (only when both fields are ≤32-bit integers):
```cpp
uint64_t key = ((uint64_t)a << 32) | (uint32_t)b;
std::unordered_map<uint64_t, Acc> acc;
```

**Never use:** `std::map<std::tuple<int,int>, Acc>` — O(log N) per lookup, tuple comparison overhead.

---

### K. CPU Cache Locality & Memory Layout

**Pointer chasing (bad):**
- `std::list`, raw pointer arrays, trees in inner loops
- Each node access = potential cache miss = ~100 cycle stall

**Sequential access (good):**
- `std::vector` — contiguous memory, hardware prefetcher can predict access pattern
- Front-to-back iteration saturates cache lines

**2D array access:**
```cpp
// Cache-hostile (column-major on a row-major layout)
for (int j = 0; j < cols; j++)
    for (int i = 0; i < rows; i++)
        process(arr[i][j]);

// Cache-friendly (row-major)
for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++)
        process(arr[i][j]);
```

**Struct field access in hot loops:** If a struct has 10 fields but the inner loop only accesses 2, you're loading 10 fields worth of cache lines to use 2. Note this to the interviewer even if full refactor is out of scope.

---

### L. Virtual Function Overhead in Hot Paths

**Symptom:** Abstract base class with virtual `process()`, `evaluate()`, or `emit()` methods called once per row in the inner loop.

**Cost:** vtable pointer dereference + potential instruction cache miss, every call.

**Fix:** Templates with static dispatch (CRTP or direct template parameter), or restructure so the virtual call happens outside the loop, not inside it.

**Signal in code:**
```cpp
// Red flag — virtual call inside row-level loop
for (const auto& row : table) {
    operator->evaluate(row);   // virtual dispatch per row
}
```

---

### M. Branch Misprediction *(secondary — rarely gives >15% improvement alone)*

**Symptom:** Complex `if-else` logic inside inner loop, operating on unsorted, random data.

**Fixes:**
- Sort data before the loop so the branch predictor sees a consistent pattern
- `[[likely]]` / `[[unlikely]]` for statically known probabilities
- Branchless arithmetic where applicable: `val = cond ? a : b;` → sometimes compiles to `cmov`

**Do not lead with this.** Fix algorithmic and data structure problems first.

---

### N. Concurrency & Lock Contention *(conditional — only if codebase already has threads)*

**If single-threaded:** Do not introduce threads under time pressure. Risk of bugs outweighs potential gain in a 60-minute window.

**If threads already exist:**
- Coarse-grained mutex held across computation: shrink critical section to the minimum state mutation
- `std::atomic<int>` for counters/flags
- False sharing: two threads writing to independent variables on the same 64-byte cache line — pad structs to `alignas(64)`

---

## NON-QUERY C++ OPTIMIZATION THEORY
> For problems that are not query-shaped. The approach is identical — only the patterns differ.

---

### O. The Universal Mental Model

> **Bottleneck = operation_complexity × call_frequency**

Before touching any code, find the expression with the worst product of these two. That is your primary fix regardless of domain.

Examples:
- O(N²) operation called once > O(log N) operation called O(N) times
- O(1) operation called 10⁸ times with a cache miss per call > O(log N) called 10⁴ times

---

### P. Common Non-Query Patterns

**Redundant computation:**
```cpp
// Before: expensive function called every iteration despite stable input
for (int i = 0; i < n; i++) {
    result[i] = compute_heavy(config) * data[i];  // compute_heavy doesn't change
}

// After: hoist invariant out of loop
auto factor = compute_heavy(config);
for (int i = 0; i < n; i++) {
    result[i] = factor * data[i];
}
```

**Repeated sort inside a loop:**
```cpp
// Before: O(N log N) inside an O(M) loop = O(M × N log N)
for (auto& query : queries) {
    std::sort(data.begin(), data.end());
    answer(query, data);
}

// After: sort once if data doesn't change between queries
std::sort(data.begin(), data.end());
for (auto& query : queries) {
    answer(query, data);
}
```

**Set/map for membership testing:**
```cpp
// Before: O(log N) per lookup
std::set<int> seen;
if (seen.count(x)) { ... }

// After: O(1) average
std::unordered_set<int> seen;
seen.reserve(expected_size);
if (seen.count(x)) { ... }
```

**Batching I/O:**
- Small repeated `read()`/`write()` calls inside a loop → buffer into a larger chunk, single call

---

## TIER 3 — APPROACH
> What you do in the room. This is as important as the theory.

---

### Q. The Framework (execute in sequence, every time)

```
1. READ     (5 min)   What are the data structures? (usually vector<SomeStruct>)
                      What are the keys? (join columns, group-by columns)
                      What is the shape? (join → filter → aggregate?)
                      What are the table sizes?

2. NAME     (2 min)   State the algorithm out loud to the interviewer.
                      "This is a nested loop join over N × M rows."
                      "Aggregation is happening after full join materialization."
                      Do NOT skip this. Naming it correctly signals you know what you're looking at.

3. MEASURE  (3 min)   Add std::chrono timing around the top-level function
                      AND the suspected hot section — BEFORE changing anything.
                      Get the baseline on record.

4. FIX               Algorithmic first (join type, pre-aggregation, filter pushdown)
                      Data structure second (unordered_map, reserve, vector)
                      Micro-opt third (copies, move, string_view)

5. VERIFY            Re-run. State new timing. State improvement factor.
                      "Was 22s, now 3.8s — roughly 5.8x improvement."

6. SECONDARY         If target is met and time remains: cache layout,
                      branch patterns, further profiling.
                      If target is NOT met: go back to step 2 for the next bottleneck.
```

---

### R. Timing Idiom (memorize this)

```cpp
#include <chrono>
#include <iostream>

auto t0 = std::chrono::steady_clock::now();

// ... section under measurement ...

auto t1 = std::chrono::stead_clock::now();
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
std::cerr << "Section took: " << ms << "ms\n";
```

Use `std::cerr` so output doesn't mix with program stdout. Add timestamps around the top-level query first, then narrow to the hot section.

---

### S. Questions to Ask the Interviewer Upfront

These narrow the hypothesis space immediately:

- *"What's the approximate size of each table — thousands of rows or millions?"*
  → If millions: algorithmic fix will dominate. If thousands: micro-opts matter more.
- *"Is the code currently single-threaded?"*
  → Determines whether concurrency is in scope.
- *"Are there any constraints on what I can change — data structures, algorithms, or only the implementation?"*
  → Opens the door to fundamental redesign vs. local fixes.
- *"Is there a profiler available, or should I instrument with timing?"*
  → Sets expectations; also signals measurement discipline.

---

### T. Narration Pattern

Do not go silent. Name the problem, state the fix, then implement.

> *"I can see this inner loop is iterating over all rows of table B for each row of table A — that's O(N×M). I'm going to replace this with a hash join: build a hash table on the smaller table B first, then do a single linear scan over A with O(1) lookups. Let me add timing around the current version before I change anything."*

State three things before touching code: what the current algorithm is, what the fix is, and what you're measuring.

---

### U. Tradeoff Communication

For every optimization applied, explicitly state what you are trading. This is what the engine team evaluates specifically.

| Optimization | Tradeoff to state |
|---|---|
| Hash Join | O(N) memory on build side. Fine for this size; if memory-constrained, partitioned hash join |
| Pre-aggregation | Only valid if join semantics preserve aggregation keys — verify before applying |
| String interning | One extra pass to build dictionary; amortized over the join |
| Adding threads | Synchronization overhead; adds correctness risk under time pressure |
| Bloom pre-filter | Extra memory for bitmap; false-positive rate means some unnecessary probes remain |

---

### V. Profiling Tools — Conceptual Knowledge

You may or may not have these available. Know what each one tells you.

| Tool | What it tells you | Primary use |
|---|---|---|
| `std::chrono` | Wall-clock time for any bracketed section | Always available; use first |
| `perf stat ./prog` | Cache-miss rate, branch misprediction rate | High misses → data layout; high mispredictions → branch pattern |
| `gprof` | Which functions consume the most CPU time | Flat profile of hotspots |
| `valgrind --tool=cachegrind` | Detailed L1/L2/L3 cache miss counts per line | Precise cache diagnosis |

**Without any tools:** Reason from complexity × frequency. Any O(N²) or worse loop over millions of rows is your bottleneck. You can name it and fix it without a profiler.

---

### W. Strategic Signals

- **Measure and justify:** State your hypothesis before touching the code. Never optimize silently.
- **Readable code:** No bit-hacks or obscure tricks unless the payoff explicitly demands it and you can justify it.
- **Scope awareness:** If an optimization is valid but out of scope for the time available, name it and defer it explicitly. *"I'd also look at late materialization here, but I'll defer that and focus on the join algorithm first."*
- **Correctness under optimization:** When changing a nested loop join to a hash join, verify that the join type (inner, left, etc.) is preserved. Hash joins handle duplicates via `vector` in the map values — state this explicitly.

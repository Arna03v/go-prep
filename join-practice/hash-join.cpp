#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
using namespace std;

/*
### Hash Join (classic/in-memory) — theory

**The idea:** build a hash table on the smaller relation, then stream the larger relation through, probing the hash table for each row.

```
operator HashJoin(R, S):
    // assume S is the smaller relation — build side
    // R is the larger relation — probe side
    buildHashTable(S, on S.joinKey)

    for each tuple r ∈ R:
        matches = hashTable.lookup(r.joinKey)
        for each tuple s ∈ matches:
            emit (r, s)
```

This is structurally identical to Index Nested Loop Join's code shape — build a hash structure, then probe it per outer row. The difference is *when* the index/hash table is built: INLJ assumes it already exists from prior schema design; hash join builds it fresh, as part of the join itself, every single time.

### I/O cost

```
Cost = M + N
```

Each table gets read from disk exactly once — S once to build the hash table, R once to stream through and probe. This is the cheapest of everything we've covered so far, **with one hard constraint**: the build side has to fit in available memory.

```
constraint: N ≤ B   (build side's pages must fit within the buffer budget)
```

If that constraint breaks — S doesn't fit — you can't use this version at all. That's exactly the gap Grace Hash Join exists to close, which is why you'll want it next.

**Why build on the smaller side specifically:** the constraint only cares about the build side's size, not the probe side's. R can be arbitrarily large — you stream it through one page at a time and never hold more than a page of it in memory. So you always want the *smaller* relation occupying the memory-constrained role.

*/
struct Row {
    int key;
    std::string payload;
};

struct JoinedRow {
    Row left;
    Row right;
};

// Classic Hash Join
// Caller is responsible for passing the SMALLER relation as `buildSide` —
// the function doesn't pick automatically, since "smaller" requires size info
// not present in the function signature.
std::vector<JoinedRow> hashJoin(
    const std::vector<Row>& probeSide,   // larger relation — streamed
    const std::vector<Row>& buildSide)   // smaller relation — hashed
{
    std::unordered_multimap<int, Row> hashTable;
    hashTable.reserve(buildSide.size());

    for (const auto& row : buildSide) {
        hashTable.emplace(row.key, row);   // duplicates land as multiple entries under the same key
    }

    std::vector<JoinedRow> result;
    for (const auto& probeRow : probeSide) {
        auto range = hashTable.equal_range(probeRow.key);
        for (auto it = range.first; it != range.second; ++it) {
            result.push_back({probeRow, it->second});
        }
    }

    return result;
}
/*

This is almost line-for-line the INLJ code from last message — same `equal_range` mechanism, same emit logic. The only real difference: INLJ's index pre-exists conceptually; here the hash table is built inline, right before probing, as part of this single call.

### Time and space complexity (`r = |probeSide|`, `s = |buildSide|`)

| Phase | Time | Space |
|---|---|---|
| Build hash table | O(s) average | O(s) — stores every row of build side |
| Probe for each row of probe side | O(1) average × r → **O(r)** | O(1) per probe |
| Emit matches | O(output size) | O(output size) |

**Combined: Time = O(r + s) average case, O(r·s) worst case under hash collisions. Space = O(s) for the hash table + O(output size) for the result.**

Same average/worst-case caveat as INLJ, since it's the same underlying `unordered_multimap` — worth stating once and reusing, since interviewers will sometimes ask "what's the actual worst case" specifically to check you're not just reciting average-case Big-O.

**The tradeoff worth stating explicitly** (this is the line from your tradeoff table): duplicates on the build side land as multiple separate entries under the same key bucket in the multimap — functionally a vector of matches per key, even though `unordered_multimap` doesn't expose it as literally `vector<Row>`. If asked to justify correctness with duplicate keys, point at this directly: "every build-side row with a matching key gets its own entry, so `equal_range` returns all of them, not just one."

Try it with the same duplicate-key test case from sort-merge join, and also try swapping which side you pass as `buildSide` vs `probeSide` to confirm the *output* doesn't change — only which side gets held in memory should differ.

*/


// ================== GRACE HASH JOINS ======================
/*
### Grace Hash Join (Partitioned Hash Join) — theory

**The idea:** classic hash join requires the entire build side to fit in memory (`N ≤ B`). Grace Hash Join removes that requirement by first splitting both relations into smaller partitions — small enough that each individual partition's build side *does* fit — then running classic hash join independently on each matching partition pair.

```
operator GraceHashJoin(R, S, k):       // k = number of partitions

    // Phase 1: Partition
    for each tuple s ∈ S:
        write s to partition_S[ h1(s.joinKey) % k ]
    for each tuple r ∈ R:
        write r to partition_R[ h1(r.joinKey) % k ]

    // Phase 2: Build + Probe, one partition pair at a time
    for i in 0..k:
        buildHashTable(partition_S[i], using h2)   // h2 ≠ h1 — different hash function
        for each tuple r ∈ partition_R[i]:
            matches = hashTable.lookup(r.joinKey)
            for each s ∈ matches: emit (r, s)
        discard hashTable                          // freed before next partition
```

**The key invariant:** if `r.joinKey == s.joinKey`, then `h1(r.joinKey) == h1(s.joinKey)` — same hash, same partition number. So two rows that actually join are *guaranteed* to land in matching partition indices, and rows in different partition indices can never join. That's what makes splitting the problem into k independent sub-joins safe.

**Why a second hash function `h2`, separate from `h1`:** if you reused the same hash function for partitioning and for the in-memory hash table, any pathological key distribution that caused bad partitioning would *also* cause bad in-memory bucketing — the two stages would correlate their worst cases together instead of independently smoothing each other out. Standard practice is two independent hash functions.

### I/O cost

```
Partition phase: read R, S once (M+N), write all partitions back out (M+N)  →  2(M+N)
Build/Probe phase: read all partitions back in once                        →  (M+N)

Total Cost = 3(M+N)
```

**Constraint on number of partitions `k`:** you need `k` output buffers open simultaneously during partitioning (one per partition being written) plus 1 input buffer, so `k ≤ B − 1`. And each build-side partition must fit in memory: `N/k ≤ B`. Combining both: `N ≤ B(B−1) ≈ B²` — single-level partitioning handles build sides up to roughly B² pages. Past that, you'd recursively partition a partition that's still too big — same escalation pattern as merge sort needing more passes when a single fan-in level isn't enough.

**Concrete numbers:** `B = 5` buffer pages, `N = 20` pages (S doesn't fit in B=5, so *classic* hash join is off the table entirely here). Partition into `k = B−1 = 4` partitions → each partition ≈ `20/4 = 5` pages, which now fits in B. With `M = 10`: `Cost = 3×(10+20) = 90`. The relevant comparison isn't against classic hash join's `M+N=30` — that number was never achievable here since the memory constraint broke. It's against the alternative of NOT being able to do a single-pass hash join at all.

---

### Code

*/

// Partition a relation into `numPartitions` buckets using hash(key) % numPartitions
std::vector<std::vector<Row>> partitionRows(const std::vector<Row>& rows, size_t numPartitions) {
    std::vector<std::vector<Row>> partitions(numPartitions);
    std::hash<int> h1;
    for (const auto& row : rows) {
        size_t p = h1(row.key) % numPartitions;
        partitions[p].push_back(row);
    }
    return partitions;
}

// Grace Hash Join
// buildSide convention same as classic hash join — pass the smaller relation here.
// Partitioning shrinks each INDIVIDUAL partition's build side to fit in memory,
// even when the whole buildSide doesn't.
std::vector<JoinedRow> graceHashJoin(
    const std::vector<Row>& probeSide,
    const std::vector<Row>& buildSide,
    size_t numPartitions)
{
    auto probePartitions = partitionRows(probeSide, numPartitions);
    auto buildPartitions = partitionRows(buildSide, numPartitions);

    std::vector<JoinedRow> result;

    for (size_t p = 0; p < numPartitions; ++p) {
        std::unordered_multimap<int, Row> hashTable;   // this plays the role of h2 —
        hashTable.reserve(buildPartitions[p].size());   // a SEPARATE hash, scoped to one partition

        for (const auto& row : buildPartitions[p]) {
            hashTable.emplace(row.key, row);
        }

        for (const auto& probeRow : probePartitions[p]) {
            auto range = hashTable.equal_range(probeRow.key);
            for (auto it = range.first; it != range.second; ++it) {
                result.push_back({probeRow, it->second});
            }
        }
        // hashTable destructs here — only ONE partition's hash table is ever
        // resident at a time, which is the actual point of this algorithm.
    }

    return result;
}
/*

**A modeling gap worth flagging upfront, same spirit as the BNLJ page-vs-tuple caveat earlier:** this code doesn't actually spill anything to disk — `probePartitions` and `buildPartitions` hold *every* partition simultaneously in memory the whole time, since they're just `std::vector`s. The only thing that's actually scoped to "one at a time" here is the `hashTable` itself. A real engine writes each partition to a separate file on disk during phase 1, then reads partitions back in one at a time during phase 2 — so the I/O-cost formula (`3(M+N)`) reflects real disk reads/writes that this in-memory demo doesn't reproduce. The code demonstrates the *mechanism* (partition, then build-one-discard-one), not the actual memory-pressure relief.

**A second simplification:** `partitionRows` uses `std::hash<int>` for partitioning, and `unordered_multimap`'s internal hashing (also `std::hash<int>` by default) for the in-memory build — that's the same hash function reused for both `h1` and `h2`, not two independent ones. Doesn't break correctness, but a poor key distribution could now produce correlated skew in both stages rather than one smoothing out the other. Worth naming if asked.

### Time and space complexity (`r = |probeSide|`, `s = |buildSide|`, `k` = numPartitions)

| Phase | Time | Space |
|---|---|---|
| Partition both relations | O(r + s) — one hash + pushback per row | O(r + s) — every row copied into some partition |
| Build + probe, summed across all k partitions | O(r + s) average — partition sizes sum back to r and s exactly | O(max partition size) *peak*, during any single iteration — only one hash table alive at a time |
| Emit matches | O(output size) | O(output size) |

**Combined: Time = O(r + s) average — same asymptotic class as classic hash join.** This is worth stating explicitly if asked "what do you gain algorithmically" — the answer isn't a better Big-O, it's a lower *peak memory ceiling*. You trade a `3×` I/O cost multiplier (`3(M+N)` vs classic's `M+N`) for the ability to run at all when the build side doesn't fit in memory. That's the real tradeoff to name here, in the same sense as the earlier memory-for-speed tradeoff — except this one is closer to "I/O-for-feasibility."

Try running this with a `numPartitions` value, then a duplicate-key test split deliberately so duplicates land in different partitions if you used different keys — confirm the partition invariant (`h1` consistency) is actually what's preventing cross-partition matches from being missed, not just luck from small test data.
*/
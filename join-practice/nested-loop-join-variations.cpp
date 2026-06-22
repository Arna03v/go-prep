#include <functional>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
using namespace std;

struct Row{
    int key;
    string payload;
};

struct JoinedRow{
    // need rows from both tables
    Row left;
    Row right;
};


// tuple comparing time complexity : O(number of rows in outer table) * O(number of rows in the inner table)
// tuple comparing space complexity : not including the required output; constant space
/*
The predicate is a free function, not baked-in equality. This is the one structural advantage NLJ has over hash join — it works for any condition: range joins (a.key < b.key), inequality joins, even multi-column conditions. Hash join only works for equality. If an interviewer asks "when would you ever pick NLJ over hash join," this is the answer.

If both the tables are in memory : Outer/inner choice doesn't change total comparisons (still N×M), but it changes cache behavior. The inner loop's table gets re-scanned once per outer-row iteration. If the inner table is small enough to stay resident in cache, each re-scan is cheap. So convention: put the smaller table as inner, larger table as outer (streamed through once). Worth stating explicitly as a tradeoff even though Big-O doesn't capture it.

If the tables are not in memory : 
m = tuples in R, M = pages R occupies
n = tuples in S, N = pages S occupies
Cost = M + (m·N) → read R once (M page I/Os), then for each of the m tuples in R, re-scan all of S (N page I/Os each time)

Lead with the I/O time is it is much bigger cost than CPU computations. 
No index, no pre-processing, no extra memory beyond the output. That's the whole tradeoff: simplest possible correctness, worst possible asymptotic cost.
*/
vector<JoinedRow> nestedLoopJoin(vector<Row>& outer, vector<Row>& inner, function<bool(const Row& a, const Row& b)> predicate){
    // takes a function called predicate
        // which takes 2 rows and returns a bool
    
    // any function of the given signature can be passed

    vector<JoinedRow> res;

    // for each row in the outer table
        // process the entire inner table

    for(auto& outerRow : outer){
        for(auto& innerRow : inner){
            if(predicate(innerRow, outerRow) == true){
                // res.emplace_back(innerRow, outerRow); // wont work undless there is a constructor which takes in2 random rows 
                res.push_back({innerRow, outerRow});
            }
        }
    }

    return res;
}


// ============= BLOCK NESTED LOOP JOIN ===================


/*
R = outer table, m tuples across M pages
S = inner table, n tuples across N pages
B = number of pages of R held in one in-memory block (not tuples — flagging this now, more on it below)
Cost = M + ⌈M/B⌉ · N

Buffer pool constraint (how a real engine actually picks B)
In practice B isn't chosen freely — it's constrained by available buffer pages. If total buffer budget is Buf pages, reserve 1 page to stream S in, 1 page for output, and the rest holds the R-block:
Cost = M + ⌈M / (Buf − 2)⌉ · N
This is the formula a DB course (or an engine-team interviewer) would expect if asked to derive BNLJ cost under a memory budget — worth having ready in this exact form.
*/
using Page  = std::vector<Row>;     // one page = some number of tuples
using Table = std::vector<Page>;    // a table = a sequence of pages
std::vector<JoinedRow> blockNestedLoopJoin(
    const Table& outer,
    const Table& inner,
    std::function<bool(const Row&, const Row&)> predicate,
    size_t blockSizePages,
    long long& pageIOCount)        // tracked explicitly, not just asymptotic
{
    std::vector<JoinedRow> result;
    size_t M = outer.size();

    pageIOCount += M;  // one full read of R, regardless of blocking

    for (size_t blockStart = 0; blockStart < M; blockStart += blockSizePages) {
        size_t blockEnd = std::min(blockStart + blockSizePages, M);

        for (const auto& page : inner) {
            pageIOCount += 1;  // one page of S read, shared across the whole block

            for (size_t p = blockStart; p < blockEnd; ++p) {
                for (const auto& outerRow : outer[p]) {
                    for (const auto& innerRow : page) {
                        if (predicate(outerRow, innerRow)) {
                            result.push_back({outerRow, innerRow});
                        }
                    }
                }
            }
        }
    }

    return result;
}

// ============= INDEX NESTED LOOP JOIN ===================
/*

### Index Nested Loop Join — theory

for each row in the outer table R, probe the index on the inner table S.

**The idea:** instead of rescanning all of S for every row of R (naive) or rescanning S once per block (BNLJ), use an index that already exists on S's join key to jump directly to matching rows.

```
operator IndexNestedLoopJoin(R, S):
    // assumes an index already exists on S.joinKey
    for each tuple r ∈ R:
        matches = index.lookup(S, r.joinKey)   // direct lookup, not a scan
        for each tuple s ∈ matches:
            emit (r, s)
```

### I/O cost — same notation as before (`M`/`N` = pages, `m`/`n` = tuples)

```
Cost = M + m × (cost_to_probe_index + cost_to_retrieve_matches)
```

Two things determine the probe cost:

**Index type** (cost to find the matching index entries):
- B+Tree index: cost ≈ index height `Hi` — typically 3–4 even for large tables, since B+Trees have very high fanout per node
- Hash index: cost ≈ 1–2 I/Os, effectively constant

**Index clustering** (cost to retrieve the actual matching tuples once found):
- Clustered (matching tuples stored physically together): `⌈matches / tuples_per_page⌉`
- Unclustered (matches scattered across arbitrary pages): `matches` — potentially one I/O per matching tuple, worst case

For the common case — a foreign-key-to-primary-key join, where each probe returns exactly 1 match:

```
Cost = M + m × (Hi + 1)
```

**Concrete numbers, same scale as the naive NLJ example:** `M = 10` pages (R, m=1000 tuples), `N = 20` pages (S), B+Tree height `Hi = 3`, FK→PK join (1 match per probe).

```
Naive:  M + m·N      = 10 + 1000×20 = 20,010
INLJ:   M + m·(Hi+1) = 10 + 1000×4  =  4,010
```

~5× fewer I/Os here, and the gap widens further as `N` grows — INLJ's cost barely moves as S gets bigger, since you never scan it.

**The important caveat — why this isn't "just always use this":** that formula assumes the index **already exists**, built ahead of time via `CREATE INDEX`, amortized across many queries. If you had to build an index from scratch just for one join, you'd be paying roughly the same build cost as hash join's build phase — at which point you'd just be doing a worse version of hash join. A query optimizer only picks INLJ when an index is already sitting there from prior schema design, not as something to construct on the fly. Worth stating explicitly if asked "why not always use index nested loop?"

*/

// Index Nested Loop Join (hash-index analog)
// NOTE: in a real engine the index on `inner` already exists before this join runs.
// Building it here is purely to demonstrate the lookup mechanism.
std::vector<JoinedRow> indexNestedLoopJoin(
    const std::vector<Row>& outer,   // R — scanned
    const std::vector<Row>& inner)   // S — indexed
{
    // build the index first
    std::unordered_multimap<int, Row> index;
    index.reserve(inner.size());
    for (const auto& row : inner) {
        index.emplace(row.key, row);
    }
    /*
    This is the actual index lookup — the index.lookup(S, r.joinKey) line from the pseudocode. Let's unpack what equal_range does and returns.
index is an unordered_multimap<int, Row>. Since it's a multimap, multiple Rows can share the same key (e.g. multiple S rows with key = 5). equal_range(k) finds all entries with key k and gives you back the boundaries of that group — not the entries themselves directly.
It returns a std::pair<iterator, iterator>:

range.first → an iterator pointing to the first entry with key == outerRow.key
range.second → an iterator pointing to one past the last entry with that key (same "one-past-the-end" convention as vector::end())

If outerRow.key doesn't exist in the index at all, range.first == range.second — an empty range, zero matches.
This single call is doing the same job that equal_range did in your lower_bound/upper_bound notes from earlier in this conversation — range.first is the multimap's version of lower_bound, range.second is the multimap's version of upper_bound, just both returned together in one call instead of two separate calls.
    */
    std::vector<JoinedRow> result;
    for (const auto& outerRow : outer) {
        auto range = index.equal_range(outerRow.key);
        for (auto it = range.first; it != range.second; ++it) {
            result.push_back({outerRow, it->second});
        }
    }

    return result;
}

/*

### Time and space complexity (not I/O — using `r = |R|`, `s = |S|` as before)

| Phase | Time | Space |
|---|---|---|
| Build index on S | O(s) average (amortized O(1) insert × s) | O(s) — stores a copy of every row of S |
| Probe for each row of R | O(1) average per lookup × r → **O(r)** | O(1) per probe |
| Emit matches | O(output size) | O(output size) |

**Combined: Time = O(r + s + output size) average case. Space = O(s) for the index + O(output size) for the result.**

The average-case caveat matters here specifically — `std::unordered_multimap` degrades to O(s) per lookup under adversarial hash collisions, which would make total time O(r·s), same worst case as naive NLJ. Worth knowing this is the same caveat that applies to hash join's build side, since they're using the same underlying data structure.

**One modeling note worth saying out loud:** this code uses a hash index (`unordered_multimap`) because it's the simplest analog in C++. Real engines more commonly use B+Tree indexes (ordered, O(log s) per lookup) precisely because they also support range queries and `ORDER BY` — a hash index can't. If you wanted to model that instead, swap to `std::multimap<int, Row>` and the lookup cost becomes O(log s) rather than O(1) average, at the benefit of ordered iteration.

Try this against the same duplicate-key test case you used for sort-merge join — `equal_range` should behave the same way `iEnd`/`jEnd` run-finding did, just via the hash multimap instead of manual pointer arithmetic.


*/
int main(){
    vector<Row> orders    = {{1, "order_A"}, {2, "order_B"}, {1, "order_C"}};
    vector<Row> customers = {{1, "Alice"}, {2, "Bob"}};

    auto result = nestedLoopJoin(orders, customers,
    [](const Row& a, const Row& b) { return a.key == b.key; });
    // . By taking the comparison as a parameter, the same loop structure works for any condition without touching the function body:
    /*
    // equality join
        nestedLoopJoin(orders, customers, [](const Row& a, const Row& b) { return a.key == b.key; });

        // range join — e.g. "outer.key falls within inner's range"
        nestedLoopJoin(events, ranges, [](const Row& a, const Row& b) { return a.key >= b.key && a.key < b.key + 10; });
    */

    for(const auto& el : result) {
        cout << "(" << el.left.key << "," << el.left.payload << ") joined with ("                                                                                                                
            << el.right.key << "," << el.right.payload << ")" << endl;                                                                                                                        
    }  
}
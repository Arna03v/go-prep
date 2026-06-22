#include <vector>
#include <string>
#include <algorithm>
#include <functional>
using namespace std;

/*
### Sort-Merge Join — theory, same I/O cost model

**Two phases: sort, then merge.**

```
operator SortMergeJoin(R, S):
    sort R by join key   (if not already sorted)
    sort S by join key   (if not already sorted)

    i = 0, j = 0
    while i < |R| and j < |S|:
        if R[i].key < S[j].key:  i++
        elif R[i].key > S[j].key: j++
        else:
            // keys match — but there may be DUPLICATES on either side
            find full run R[i..i'] where all keys == R[i].key
            find full run S[j..j'] where all keys == S[j].key
            emit cross product of these two runs
            advance i past i', j past j'
```

**Cost — sort phase:**

If either side isn't already sorted, you pay external merge sort cost. With `B` buffer pages available, standard formula:

```
passes = 1 + ⌈log_(B-1)⌈M/B⌉⌉
sort cost = 2M × passes
```

(Pass 0 produces `⌈M/B⌉` sorted runs of size B, costing 2M for read+write. Each subsequent pass merges B−1 runs together, again costing 2M, until one run remains.)

**Cost — merge phase:**

Once both sides are sorted, the merge itself is a single sequential pass over each:

```
merge cost = M + N
```

**Total cost = sort(R) + sort(S) + (M + N)**, where each sort term drops to 0 if that side is already ordered — e.g. coming straight off a sorted index scan, or the result of a prior sort-merge join feeding into another one. This is exactly why SMJ is attractive in a query plan: if the data is *already* sorted, you skip the expensive part entirely and pay only `M + N`, matching hash join's I/O cost without ever materializing a hash table.

**Where it beats hash join outright:** range and inequality joins (`a.key BETWEEN b.lo AND b.hi`, `a.key < b.key`). Hash join only works for equality — this connects back to the same point I made about naive NLJ's predicate flexibility, but SMJ gets that flexibility *and* near-linear cost, as long as the comparison respects sort order.

**The correctness trap, parallel to hash join's duplicate-handling note:** if you don't explicitly buffer the full matching run on both sides before advancing pointers, you'll only emit the first matching pair per key instead of the full cross product. This is the SMJ-specific version of "hash joins handle duplicates via vector in the map values — state this explicitly." Worth saying out loud in the interview: "I need to buffer the duplicate run on at least one side, not just advance past the first match."

* How does this IO cost come along?
Let's derive it from first principles — this is the part that trips people up because it's not obvious why sorting needs *multiple passes* at all.

### Why passes happen in the first place

You have a table of `M` pages, but only `B` buffer pages of memory. You can't load the whole table and call `std::sort` on it — it doesn't fit. So external sort works by: (1) make small sorted chunks that *do* fit in memory, write them to disk, then (2) repeatedly merge those chunks into bigger sorted chunks until one remains.

### Pass 0 — Run Creation

Read in `B` pages at a time (as much as fits), sort that chunk in memory with any standard in-memory sort, write it back out as a **sorted run**.

- Read all M pages once: **M** I/Os
- Write all M pages back out (now as sorted runs): **M** I/Os
- Pass 0 cost: **2M**

Each run is `B` pages long, so the number of runs produced is:

```
runs after pass 0 = ⌈M / B⌉
```

### Merge passes — reducing run count

Now merge these runs together. With `B` buffer pages, you can hold one input page from each of `B − 1` runs simultaneously, plus 1 page reserved for writing output. So each merge pass can combine **B − 1** runs into one larger run — this is the "fan-in."

Each merge pass still touches every page exactly once on the way in and once on the way out, **regardless of how many runs are being merged at once** — so every merge pass costs the same **2M**, just like pass 0.

What changes per pass is the run count, shrinking by a factor of `(B−1)`:

```
runs after pass 1 = ⌈runs_after_pass_0 / (B−1)⌉
runs after pass 2 = ⌈runs_after_pass_1 / (B−1)⌉
...
```

### How many merge passes until 1 run remains?

You need enough passes that `(B−1)^passes ≥ ⌈M/B⌉` (initial run count shrunk down to 1). Solving for passes:

```
merge passes = ⌈log_(B-1) (⌈M/B⌉)⌉
```

Add back the 1 pass spent on initial run creation:

```
total passes = 1 + ⌈log_(B-1) (⌈M/B⌉)⌉
total cost = 2M × total passes
```

— which is exactly the formula from before. The `2M` is constant per pass (you're always reading and writing the full table once); the log term is just "how many times do I need to multiply the fan-in `(B−1)` by itself to absorb all the initial runs."

### Concrete numbers, so this isn't just symbols

Say `M = 108` pages, `B = 5` buffer pages.

- **Pass 0:** run size = 5 pages → `⌈108/5⌉ = 22` runs. Cost: `2×108 = 216`.
- **Pass 1:** fan-in = B−1 = 4 → `⌈22/4⌉ = 6` runs. Cost: `216`.
- **Pass 2:** `⌈6/4⌉ = 2` runs. Cost: `216`.
- **Pass 3:** `⌈2/4⌉ = 1` run — done. Cost: `216`.

4 passes total → `4 × 216 = 864` page I/Os to fully sort a 108-page table with only 5 buffer pages.

Check against the formula: `passes = 1 + ⌈log_4(22)⌉ = 1 + ⌈2.23⌉ = 1 + 3 = 4`. ✓. `cost = 2×108×4 = 864`. ✓.

### Tying back to the full Sort-Merge Join cost

This `2M × passes` formula applies **separately to R and S** if either isn't already sorted — each has its own M (or N), and possibly different available buffer allocation. So the full picture:

```
SMJ total cost = sortCost(R) + sortCost(S) + (M + N)
```

where `sortCost(X) = 0` if X arrives pre-sorted (e.g. off an index), and the trailing `M + N` is the single sequential merge pass we coded last message. This is exactly why query planners care so much about whether a child operator already produces sorted output — it can zero out the most expensive term in this whole formula.
---
*/


struct Row {
    int key;
    std::string payload;
};

struct JoinedRow {
    Row left;
    Row right;
};

// Merge sort
// merges two already-sorted halves: rows[left, mid) and rows[mid, right)
void merge(std::vector<Row>& rows, size_t left, size_t mid, size_t right) {
    std::vector<Row> temp;
    temp.reserve(right - left);

    size_t i = left, j = mid;
    while (i < mid && j < right) {
        if (rows[i].key <= rows[j].key) {
            temp.push_back(rows[i++]);
        } else {
            temp.push_back(rows[j++]);
        }
    }
    while (i < mid)   temp.push_back(rows[i++]);
    while (j < right) temp.push_back(rows[j++]);

    std::copy(temp.begin(), temp.end(), rows.begin() + left); // copies values from temp to the rows position + left
}

// sorts rows[left, right) by key, recursively
void mergeSort(std::vector<Row>& rows, size_t left, size_t right) {
    if (right - left <= 1) return;   // 0 or 1 elements — already sorted

    size_t mid = left + (right - left) / 2;
    mergeSort(rows, left, mid);
    mergeSort(rows, mid, right);
    merge(rows, left, mid, right);
}

// convenience overload — sorts the whole vector
void mergeSort(std::vector<Row>& rows) {
    mergeSort(rows, 0, rows.size());
}



vector<JoinedRow> sortMergeJoin(vector<Row>& R, vector<Row>& S){
    mergeSort(R);
    mergeSort(S);

    vector<JoinedRow> result;

    // we also need pointers
    size_t i = 0; // for R
    size_t j = 0; // for S

    while(i < R.size() && j < S.size()){
        if(R[i].key < S[j].key){
            i++;
        }else if(R[i].key > S[j].key){
            j++;
        }else{
            // theya re equal
            // need to do the full looping
            int key = R[i].key;

            // find end of R till the keys are the same
            size_t iEnd = i;
            while(iEnd < R.size() && R[i].key == R[iEnd].key) iEnd++;

            // same for j
            size_t jEnd = j;
            while (jEnd < S.size() && S[jEnd].key == key) jEnd++;

            // now emit the full cross produt in this range
            for (size_t a = i; a < iEnd; ++a) {
                for (size_t b = j; b < jEnd; ++b) {
                    result.push_back({R[a], S[b]});
                }
            }

            i = iEnd;
            j = jEnd;
        }
    }

    return result;
}

/*
full time and space complexity : not IO cost
Switching out of the I/O model to standard time/space complexity — using `r = |R|`, `s = |S|` as tuple counts to avoid clashing with the earlier page/tuple notation (`M`, `N`, `m`, `n`).

### `merge(rows, left, mid, right)`

- **Time:** O(k), where k = `right - left` — single linear pass through both halves via the `i`, `j` pointers, plus the copy-back.
- **Space:** O(k) — the `temp` vector holds every element in the range being merged.

### `mergeSort(rows, left, right)`

- **Time:** classic recurrence `T(n) = 2T(n/2) + O(n)` → **O(n log n)**, where n = `right - left`.
- **Space:** **O(n)**, not O(n log n) — this is the part worth being precise about if asked.

Here's why it's O(n) and not O(n log n): you might expect each of the `log n` recursion levels to add another O(n) of temp allocation, stacking up. But recursion here is depth-first — `mergeSort(left, mid)` fully completes (and its temp vectors get freed) *before* `mergeSort(mid, right)` even starts. So at any given instant, only **one** `temp` vector is alive at a time, not one per level. Its size varies by which call is currently merging, and the largest one occurs at the very top call, where `temp` holds all n elements. Add O(log n) for the recursion call stack itself (a handful of `size_t`s per frame) — dominated by the O(n) term. So: **O(n) auxiliary space total**, the standard result for merge sort.

### `sortMergeJoin(R, S)`

| Step | Time | Space |
|---|---|---|
| `mergeSort(R)` | O(r log r) | O(r) |
| `mergeSort(S)` | O(s log s) | O(s) |
| pointer traversal (`i`, `j` advances + run-finding loops) | O(r + s) | O(1) |
| cross-product emission | O(output size) | O(output size) |

**Combined: Time = O(r log r + s log s + output size). Space = O(r + s) auxiliary + O(output size) for the result.**

The reason `output size` gets called out as its own term rather than folded into "O(r+s)": the pointer traversal itself only ever moves forward — that part is genuinely linear. But the nested loop emitting the cross product is bounded by how many *matching* pairs exist, which is independent of traversal cost. If every row in R and every row in S shares the same key, output size hits **O(r·s)** — degenerating to the same worst case as naive nested loop join. This is the same distinction I flagged when we walked through the duplicate-run bug: traversal cost and output cost are two separate things, and a join algorithm's "efficiency" claim only ever covers the traversal/matching part — it can never beat the size of the actual answer.

One more thing worth having ready: if `r` and `s` are wildly different sizes, `r log r + s log s` is dominated by whichever table is larger — same intuition as "put the smaller table on the build side" for hash join, just showing up here as "the bigger table's sort cost dominates the whole pipeline."
*/
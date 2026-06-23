QUERY optimisation prep

**Tier 1 — Query Algorithmic**
- Nested loop join, hash join, sort-merge join — complexities and tradeoffs
- Join ordering for multi-way joins
- Pre-aggregation vs late aggregation
- Filter / predicate pushdown
- Late materialization / projection pushdown
- Bloom filter as join pre-filter
- Volcano execution vs vectorised model
    The Gap: Traditional query engines use the Volcano model (next() returns one row at a time via a virtual function call). SingleStore relies heavily on Vectorization (next() returns a vector/batch of 1024 values at once).

    Why it matters for June 22: Batching elements hides the virtual function call overhead you listed, keeps the CPU pipeline full, and allows the compiler to use SIMD (Single Instruction, Multiple Data) instructions. If you are given a query loop to optimize, look to see if you can change it from processing one element to processing a contiguous block of elements.

    Branches inside the loop (an if per element) are the killer — they prevent auto-vectorization because the CPU can't apply one instruction uniformly

    auto vectorisation `-O3 -march=native`, keep loops simple and branch-free so it auto-vectorizes

- Custom Memory Allocation (Arenas / Pool Allocators)
    You noted reserve() and emplace_back(), which eliminate internal reallocations, but you missed the cost of the allocator itself.

    The Gap: Calling new, malloc, or resizing a standard std::vector inside a query hot path triggers OS-level synchronization or heap management overhead.

    The Engine Fix: Databases use Arena Allocators (or std::pmr::monotonic_buffer_resource in modern C++). They allocate a massive chunk of memory upfront for the entire lifetime of the query execution. Individual operations just bump a pointer to claim memory, and the entire arena is wiped cleanly at the end. Mentioning this trade-off shows you understand real-world database memory mechanics.

    The first is why bump-pointer allocation is fast beyond just avoiding malloc: no per-allocation metadata/bookkeeping, no free-list traversal, no fragmentation, and crucially no per-object destruction — you reset one pointer to reclaim everything (the "wiped cleanly at the end" you mentioned, but name the consequence: deallocation is O(1) for the whole arena regardless of object count). The tradeoff to state out loud: you can't free individual objects, which is exactly why it fits query execution — everything has the same lifetime (the query), so individual frees are unnecessary.
    The second is the thread-local / per-operator arena angle, which connects to the concurrency theme running through your whole prep. A global arena shared across threads reintroduces the lock contention you're trying to escape. The engine answer is per-thread (or per-operator) arenas so allocation stays lock-free. Mentioning that pre-empts the "what if multiple threads allocate?" follow-up.

- predicate evaluation order and short-circuiting on batches. When you have WHERE a > 5 AND b < 10, the vectorized engine evaluates the most selective predicate first to shrink the batch before evaluating the next one — so the second predicate runs on fewer rows. This is a real, nameable optimization (selectivity-based predicate reordering) that sits right at the intersection of your Tier 1 query-algorithmic list and this Tier 2 performance list. It's a strong thing to volunteer because it shows you think about data volume flowing through stages, not just per-line code speed.


**Tier 2 — C++ Performance Patterns**
- `reserve()`, `emplace_back`, `std::move`, `string_view`
- `unordered_map` vs `map`, `unordered_set` vs `set`
- String key → integer key interning
- Composite key hashing (hash-combine, packing into uint64_t)
- Cache locality — vector vs list, sequential vs pointer-chasing access, row-major vs column-major iteration
- Virtual function overhead in hot paths
- Branch misprediction and sort-before-process
- False sharing and lock contention (only if threads exist)

**Non-Query C++ Optimization**
- Complexity × frequency mental model
- Redundant computation / loop-invariant hoisting
- Repeated sort inside a loop
- Batching I/O

**Tier 3 — Approach**
- Read → Name → Measure → Fix → Verify → Secondary framework
- `std::chrono` timing idiom
- Profiling tools conceptually: `perf stat`, `gprof`, `cachegrind`
- Tradeoff communication for each optimization
- Questions to ask the interviewer upfront


DSA prep

## 3. DSA: The 14-Question Blueprint

Since they skew heavily toward tracking spatial states, pointer manipulation, and level traversal, prioritize these specific categories. Focus on executing them with optimized space metrics.

### Tree Level-Order & Pointer Stitching (Highest Priority)

1. **LC 116:** Populating Next Right Pointers in Each Node
2. **LC 117:** Populating Next Right Pointers in Each Node II *(The classic SingleStore loop problem)*
3. **LC 103:** Binary Tree Zigzag Level Order Traversal *(Try to implement without duplicating layout space)*
4. **LC 958:** Check Completeness of a Binary Tree

[https://gemini.google.com/app/80f6ad18cfff502b] : list of problems (the ones below)

### Advanced List & Graph Structure Operations

5. **LC 430:** Flatten a Multilevel Doubly Linked List
6. **LC 138:** Copy List with Random Pointer
7. **LC 25:** Reverse Nodes in k-Group *(Tests pure, defensive pointer tracking)*
8. **LC 863:** All Nodes Distance K in Binary Tree

### System Data Structures & Stream Processing

9. **LC 146:** LRU Cache *(Standard, but ensure your implementation is flawless)*
10. **LC 460:** LFU Cache *(Forces complex tracking across multiple structures)*
11. **LC 208:** Implement Trie (Prefix Tree)
12. **LC 23:** Merge k Sorted Lists

### Tree Path Dynamics

13. **LC 236:** Lowest Common Ancestor of a Binary Tree
14. **LC 124:** Binary Tree Maximum Path Sum
--------------
## DSA Questions to be practised



Trees specific DSA
```plaintext

0. Hash aggregator
    - https://p.ip.fi/sNt6

0. iterator and index for a table
    - https://p.ip.fi/9u09

0. range scan in a n-ary search tree
    - The whole idea is to descende until we find the start of the range and theb move horizonatally
    - https://p.ip.fi/S7Jt

0. 

1.⁠ ⁠perfect binary tree, every node's next pointer should pint to its right node or NULL
	- basic BFS : https://p.ip.fi/rC3p
	- O(1) space : modify children when traversing parents: https://p.ip.fi/QPRB

2.⁠ ⁠imperfect binary tree, every node's next pointer should point to its right node or NULL
	- basic BFS : https://p.ip.fi/TBg6
	- o(1) space : treat each level as a linked list : https://p.ip.fi/QBfC

3.⁠ ⁠imperfect binary tree; each leaf node's next should point to the next leaf node in LEVEL ORDER TRAVERSAL
		 1
       /   \
      2     3
     / \     \
    4   5     7
   /           \
  8             9

  gives : 5 -> 8 -> 9 -> NULL
  	- basic BFS : https://p.ip.fi/gxsL
  	- o(1) space : 2 phase approach : https://p.ip.fi/hWCC

4.⁠ ⁠imperfect binary tree; connect all leaf nodes in a linkedlist from LEFT to RIGHT
		 1
       /   \
      2     3
     / \     \
    4   5     7
   /           \
  8             9
  gives : 8 -> 5 -> 9 -> NULL
  	- basic DFS : not BFS since it is not level order : https://p.ip.fi/lMuh
  	- O(1) : some morris trick question : https://p.ip.fi/mZWQ


5.⁠ ⁠reverse a linked list;
	- https://p.ip.fi/I8Iw

6.⁠ ⁠reverse linked list k at a time
	- https://p.ip.fi/gKJq

7. Implement a trie
    - https://p.ip.fi/_-LW
        - lots of random IO, can we optimise?
    - https://p.ip.fi/fC_G
        - DONT KNOW CLEARLY
        - store nodes of a child in a vector sorted by the character


8. binry tree zig-zag order traversal (left to right for l1, right to left for l2 and so on)
    - basic bfs : https://p.ip.fi/fCf9

9. flatten a multi level doubly linked list
    - https://p.ip.fi/7nEx

10. copy list with a random pointer
    - https://p.ip.fi/fhdl

11. all nodes at distance k in a binary tree
    - https://p.ip.fi/744z

12. lru cache
    - https://p.ip.fi/ddmj

13. LFU cache
    - https://p.ip.fi/hYd-

14. merge k sorted lists at a time
    - traverse everything -> everythign in pq -> pop : https://p.ip.fi/ArrV
    - merge sort : nlogk : https://p.ip.fi/BEWm

15. lowest common ancestor in a BST
    - if both nodes are less then go left; else if both nodes are greater go right; else current node is the res : https://p.ip.fi/KAno

16. lowest common ancestor in BT
    - store all parents; then move up from p and store its parents; then traverse q, if there is a match then it is the LCA
    - https://p.ip.fi/KGbo

17. binary tree maximum path sum
    - https://p.ip.fi/IuGx

18. binary tree inorder traversal
    - https://p.ip.fi/7pLu

19. maximum depth of a binary tree
    - DFS : https://p.ip.fi/mN2P

20. diameter of a binary tree
    - https://p.ip.fi/gBCz

21. toposort
    - https://p.ip.fi/3dw6

```

---

## 2. Graph-Based Engine Problems

### Problem A: Distributed Deadlock Detection (Wait-For Graphs)

* **The Ask:** In a database, Transaction A might hold a lock on Row 1 and wait for Row 2. Transaction B holds Row 2 and is waiting for Row 1. This forms a cycle. You are asked to implement a background worker that tracks these dependencies across a distributed system and detects deadlocks.
* **What they are testing:** Cycle detection algorithms like Tarjan's or a modified Depth-First Search (DFS). The twist is handling this asynchronously—if the graph is constantly changing as new transactions start and finish, how do you prevent false positives?


---

## Hash aggregagtion

## Gap B — GROUP BY … COUNT/SUM via a hash table

Since you've done the join, here's the spec for the aggregation exercise. I'm defining it, not solving it — go build it, then I'll poke holes the way we do.

**What to build:** a hash aggregation operator. Input is a stream of rows `(key, value)`; output is one row per distinct key with its aggregate(s). Build it interface-first in C++ (your usual discipline applies — define the public surface before any struct).

**The core that must come out right:**
- One hash table, `key → accumulator`, where the accumulator is a small struct, not a single number.
- **Multiple aggregates in one pass.** If they ask for COUNT *and* SUM, you scan once and update both — don't re-scan per aggregate. Your accumulator holds `{count, sum, ...}`.
- **AVG is the trap.** You cannot store a running average; you keep `sum` and `count` and divide at finalize. This generalizes to the real principle: aggregates must be *composable from partial state*. Be ready to say that.

**The curveballs you'll get pushed on — be ready for all four:**
1. **Composite key (`GROUP BY a, b`).** How do you hash and compare a tuple key? Key design, not an afterthought.
2. **Doesn't fit in memory.** The money question for an engine team, and it's right in your wheelhouse from the spill-to-disk work: hash-partition the input by key, then aggregate partition-by-partition so each fits. This is the alternate algorithm to volunteer before they ask.
3. **Distributed partial aggregation.** SingleStore is distributed — each node pre-aggregates locally, then partials get merged. The sharp point: COUNT becomes *SUM of the partial counts* at the merge step, while SUM stays SUM. Same composability principle, and it's exactly why you keep sum+count for AVG.
4. **Sort-based aggregation as the alternate solution.** Hash agg gives unordered output and needs memory; sort-then-group is the alternative with different tradeoffs (ordered output, O(n log n), streams within bounded memory). Having both ready is the "multiple solutions" the rubric grades.

---

## Some other DSA questions
some candidadate had hashing + two pointer
- LC 1 : Two sum
- LC 167 : two sum with sorted input
- LC 15 : 3sum
- LC 128 : longest consecutive sequence : O(n) via hashset
- LC 3 : longest susbtring without repeating characters
- LC 88 : merge sorted array : In-place two-pointer merge filling from the back
- LC 56 Merge Intervals : Sort then single-pass merge. Standard, and the merge-while-scanning pattern recurs everywhere in engines.
- LC 49 Group anagrams 


---

### Tips for the June 23 Round with Yamil

Because the description explicitly mentions looking for **alternate solutions** and **asymptotic complexity**, use this strategy:

1. **Don't jump straight to the code:** Spend the first 5 minutes sketching out the data layout.
2. **Talk about the cache:** In database engines, memory latency is the enemy. If you suggest a node structure, mention its memory footprint. Say things like, *"Using a flat array here keeps our data contiguous in memory, which reduces CPU cache misses compared to a scattered linked list."*
3. **Acknowledge Concurrency:** Even if they don't explicitly ask for locks right away, mentioning how your tree or graph would behave if multiple threads tried to write to it at the same time will instantly make you sound like an systems engineer.
# Types of join algorithms
We normalise data to ensure that there is no duplication of data being stored, by normalisation we mean splitting the data into multiple table.

as per DDIA; normalisation is using IDs to point to the information instead of storing the information directly. This way all updates happen to the table that maps the information to the ID and not the actual table. If we directly store the information then it is de-normalised data. 

- denormalised data is good for reads (everything is present in one place) but bad for writes (updating one value means we need to update all its occurences, and uses more disk space)
    - better for OLAP systems
- normalised data is good for writes (all updates happen in one place, uses lesser disk space) but bad for reads (need to perform joins to get the relationship between columns back)
    - better for OLTP systems

[NOTE] : IO cost is not the same as the computation cost; these are 2 completely different things

## Nested join algorithms
For the tables
- R (id, name)
- S (id, value, cdate)

where 
    • R: M pages, m tuples
    • S: N pages, n tuples

For each tuple in the outer table, we must do a sequential scan to check for a match in the inner table
### Naive
``` 
operator NestedLoopJoin(R, S):
    for each tuple r ∈ R: // Outer Table
        for each tuple s ∈ S: // Inner Table
            emit, if r and s match
```

For every tuple in R, S is scanned once. 
Cost : M + (m*N)
- The `M` (Outer Table Cost): You must scan the outer table (`R`) to iterate through its tuples. Since table `R` occupies `M` pages, you perform exactly `M` I/O operations to read it from disk once.
    
- The `m*N` (Inner Table Cost): For each of the `m` tuples in the outer table, you must scan the entire inner table (`S`). Since table `S` occupies `N` pages, each of those `m` tuples requires `N` I/O operations to read the inner table.

Try to keep the smaller table on the outer (that is, the first table that is scanned)

---

### Block
```
operator BlockNestedLoopJoin(R, S):
    for each block bR ∈ R: // Outer Table
        for each block bS ∈ S: // Inner Table
            for each tuple r ∈ bR:
                for each tuple s ∈ bS:
                emit, if r and s match
```
For every block/page in R, it scans S once. Fewer disk IOs
Cost : M + ⌈M/B⌉ · N
Smaller table (in terms of pages) should be the outer table

### Buffered block
If we have buffers (blocks of fixed sizes in memory); say B buffers. Use b-2 to store the outer table; 1 to store the inner table and one to store the output
```
operator ExternalBlockNestedLoopJoin(R, S):
    for each B-2 block bR ∈ R: // Outer Table
        for each block bS ∈ S: // Inner Table
            for each tuple r ∈ bR:
                for each tuple s ∈ bS:
                    emit, if r and s match
```
Cost : M + (⌈ M / (B-2) ⌉ x N)
    
- if M < B-2 (completely fits in memory) then cost = M+N!
---

### Index
We can avoid sequential scans by using an index to find inner table matches
- Use the existing index
- OR, build one on the fly (hashtables)

for each row in the outer table R, probe the index on the inner table S.

```
operator IndexNestedLoopJoin(R, S):
    for each tuple r ∈ R: // Outer Table
        for each tuple s ∈ Index(ri = si): // Index on Inner Table
            emit, if r and s match

```
If the cost to check the index per tuple = C;
Cost : M + (C*m)
    scan each page in the outer table and for each page in the outer table do the index probe.
    cant be n*c; as we cant probe on something we have not scanned yet


Here is exactly what happens when you "probe the index":

1. **Index Probe:** For each tuple `r` in the outer table, you take the join key value ($r_i$) and use it to search the index built on the inner table $S$.

2. **Retrieve Tuple Location:** The index is a data structure (like a B+Tree or a Hash Table) that stores pointers. When you probe the index with the key $r_i$, the index immediately returns the location (e.g., a Record ID or pointer) of the matching tuple(s) in the actual inner table $S$.

3. **Fetch Tuple:** Because the index gives you the exact location, the DBMS can perform a direct "jump" to the page where that specific tuple in table $S$ resides on disk. This is significantly faster than scanning the entire inner table page-by-page as you would in a Naïve Nested Loop Join.

4. **Concatenate:** Once the tuple from $S$ is fetched, the DBMS concatenates it with your current tuple from the outer table $R$ and emits the result.

**Why this changes the cost formula:**
Because you are using the index to jump straight to the data, you aren't paying the cost of scanning all $N$ pages of table $S$ for every tuple in $R$. Instead, you pay a constant cost $C$ (which represents the cost of navigating the index and doing the direct fetch) for each of the $m$ tuples in the outer table.

That is why the total cost is **$M + (m \times C)$**, rather than including the cost of scanning $N$.

---

To optimise I/O cost of nested joins
- Smaller table as the outer table
- Buffer as much of the outer table in memory as possible
- Use an index if available



## Hash join
Helps in reducing comparisions by removing all the pairs that we dont need to check. 

• If tuple r ∈ R and a tuple s ∈ S satisfy the join condition, then they have the same value for the join attributes.
• If that value is hashed to some partition i, the R tuple must be in ri and the S tuple in si
• Therefore, R tuples in ri need only to be compared with S tuples in si

Build phase:
    scan the outer table and populate a hash table using the function `h1` on the join attribute

Probe phase:
    Scan the inner table and use `h1` on each tuple to jump to a location in the hash table; now only compare with the rows in the current bucket of the hash table!

```
operator BasicHashJoin(R, S):
    build hash table HTR for R
    for each tuple s ∈ S
        emit, if h1 (s) in HTR
```
- can match and return either a the full row (less disk access but uses up memory faster) or an identifier to the row (better if the join selectivity is low)

## Grace hash join
When the tables dont fit into memory.
- Break the table down in partitions on the disk (using the hash function)
    - R will be broken down into R1, R2, R3 .. where 1,2,3 are the hash results
    - same for S
- Now during probe phase we only have to pull R1 and S1 to perform the join

Cost :
    - build phase : read and write both tables on the disk : 2*(M+N)
    - probe phase : read both tables                       : (M+N)

    total cost : 3*(M+N)

## Hash joins vs index nested loop join
Prefer INLJ when: M + mC

- The index already exists (no build cost)
- The outer table is very small — a few hundred or thousand rows. m × (Hi+1) stays tiny even for large S
- The join is on a range or inequality condition — hash join can't do this
- Memory is constrained — INLJ's O(1) extra space wins here

Prefer Hash Join when: M + N

- No index exists and building one just for this query would cost as much as just hashing (which is always, since building an index is strictly more expensive than building a hash table)
- Both tables are large — INLJ's m × (Hi+1) term gets expensive when m is millions of rows, even with a small per-probe cost
- The join is an equality join — hash join's average O(1) probe beats B+Tree's O(log s) when you're doing it millions of times


## Sort-merge join
1. Sort both tables on the join key
- Better if the data is already sorted

2. Merge

```
operator SortMergeJoin(R, S):
    sort R, S on join keys
    cursorR ← R_sorted, cursorS ← S_sorted
    while cursorR and cursorS:
        if cursorR > cursorS:
            increment cursorS
        else if cursorR < cursorS:
            increment cursorR
        else if cursorR and cursorS match:
            emit
            increment cursorS      // move the pointer forward on the inner table
```
• Sort Cost (R): 2M x (1 + ⌈ logB−1 ⌈M / B⌉ ⌉)
• Sort Cost (S): 2N x (1 + ⌈ logB−1 ⌈N / B⌉ ⌉)
• Merge Cost: (M + N)
• Total Cost: Sort + Merge

```markdown
COST ANALYSIS
Before you can merge anything, you must create the initial sorted runs of data.

* **The Process:** The DBMS reads the entire table $R$ (which has $M$ pages) into memory, sorts chunks of size $B$ (the number of available buffers), and writes them back to disk.


* **The Cost:** This requires reading all $M$ pages and writing all $M$ pages, which equals **$2M$** I/O operations.
* **Result:** You now have $\lceil M / B \rceil$ sorted sub-files (runs) on disk.

### 2. The Merge Passes (The "$\log$" part of the formula)

Once you have these initial sorted runs, you must combine (merge) them until you have one single sorted file.

* **The Fan-In Factor ($B-1$):** Because you have $B$ buffers available, you must reserve one buffer for the output (to write the merged data), meaning you can merge **$(B-1)$** runs at a single time.


* **The Logarithmic Cost:** The number of times you need to perform a merge pass is determined by the number of initial runs ($\lceil M / B \rceil$) and the fan-in factor $(B-1)$. The number of merge passes required is $\lceil \log_{B-1} \lceil M / B \rceil \rceil$.
* **The Cost per Pass:** Each merge pass requires reading the entire dataset ($M$ pages) and writing the entire dataset back to disk ($M$ pages), costing **$2M$** I/Os per pass.

### Summary of the Total Cost

The formula simply multiplies the cost of one full read/write pass ($2M$) by the total number of passes performed:

* **Pass 0 (Initial Sort):** Costs $2M$.


* **Passes 1 through $N$ (Merge Passes):** Each pass costs $2M$. Since there are $\lceil \log_{B-1} \lceil M / B \rceil \rceil$ merge passes, the total cost for merging is $2M \times (\lceil \log_{B-1} \lceil M / B \rceil \rceil)$.


Adding these together gives you the final formula: **$2M \times (1 + \lceil \log_{B-1} \lceil M / B \rceil \rceil)$**.
```
### Worst case performance
NOTE : the algorithm tries to be linear; but when the keys are repeating it; the cursor needs to backtrack to get all the combinations of tuples. If all the rows have the same value for the join key it essentially becomes M*N  + sort-cost

```markdown

* **Scenario:** Occurs when the join attribute of *all* tuples in both relations ($R$ and $S$) contains the same value.

* **Deviation from Standard Behavior:** While the standard algorithm typically performs a linear scan using two cursors to find matches ($O(M+N)$), identical join keys force the join to produce a Cartesian product of all matches.

* **The "Backtracking" Mechanism:** If the block of matching tuples in the inner table ($S$) exceeds the available memory buffers, the algorithm cannot maintain a linear scan. Instead, it must "backtrack," resetting the inner table cursor to the start of the matching block for *each* tuple in the outer table's matching block.

* **Impact on Cost:** This backtracking effectively degrades the merging phase into a **Nested Loop Join** pattern.

* **Worst-Case Cost Formula:** $(M \times N) + \text{sort cost}$.


You asked: If I have two rows in $R$ with the same key, and a whole block of matches in $S$, do I have to scan $S$ twice?

    Yes, in the worst case, you do.The "Sort-Merge" nature: You are still benefiting from the fact that both tables are sorted. Because they are sorted, you know exactly where the group of matching keys begins and ends in both tables.The Backtracking: Once you find the first matching row in $R$, you scan the matching block in $S$. When you move to the second matching row in $R$, you must reset your $S$ cursor back to the start of that matching block in $S$ to ensure you emit all combinations.Memory constraints: If that block of matching keys in $S$ is larger than your available memory buffers, you cannot just "keep it in memory." You are forced to re-read those pages from disk.
```

## Algorithm comparisions
Sort merge is useful when
- one or both tables are already sorted on the join key
- output must be sorted on the join key
- sorting also works better on non-uniform data
    - when the data is skewed (more in one bucket as compared to another)

Hash joins are considered faster and less computationally expensive than sort merge joins. 
Nested loop joins are the most expensive; only used when both tables are very small or when an index is available to avoid a full scan

# Join ordering for multi way joins
When we are joining multiple tables, the ordering (which tables to join first) matter independtly from the join algorithm (only talks about how to join the two tables). 
- the intermediate tables formed need to be either stored in memory or spilled to disk
    - larger the intermediate table; costlier this operation
- number of probes and hash table sizes increase/decrease based on the ordering

Join the pair that produces the fewest output rows first. To spot the issue in code, look for joins chained in arbitrary order with no regard for table sizes.

example
```
The setup
Three tables:

events — 50M rows (every user action)
users — 2M rows (all registered users)
premium_users — 10K rows (paid subscribers only)

Query: get all events from premium users.


SELECT e.*, u.name
FROM events e
JOIN users u       ON e.user_id = u.id
JOIN premium_users p ON u.id = p.user_id

Ordering A : left to right
Step 1: events JOIN users → every event has a matching user, so output ≈ 50M rows
Step 2: 50M rows JOIN premium_users (10K) → only premium events survive, output ≈ 250K rows

The intermediate you carry into step 2 is 50M rows.
Step 1: hash table on users (2M), probe with events (50M) → 50M output
Step 2: hash table on premium_users (10K), probe with 50M intermediate → 250K output

Ordering B : smallest estimated output first
Step 1: users JOIN premium_users → only premium users survive, output ≈ 10K rows
Step 2: 10K rows JOIN events (50M) → same final result, ≈ 250K rows

The intermediate you carry into step 2 is 10K rows.
Step 1: hash table on premium_users (10K), probe with users (2M) → 10K output
Step 2: hash table on 10K intermediate, probe with events (50M) → 250K output


Along with the intermediate table space; we can also calculate the hash table space. 
Total probe work is 100Mil vs 52Mil
```
Hence the ordering importance
---

# order of execution of queries

It isn't strictly back-to-front, but you are entirely right that **the written order of a SQL query is completely different from how the database actually executes it.** When you write a query, you write it in an order that makes sense for human readability (`SELECT` first). But the database engine cares about **data dependencies**—it can't select columns until it knows what table they are coming from.

Here is the actual, standard logical order of execution for a SQL query:

1. **`FROM` / `JOIN**` – The database first goes to the disk/memory to grab the tables and combine them. (If it doesn't do this first, it doesn't know what data exists).
2. **`WHERE`** – It filters out individual raw rows that don't match your criteria.
3. **`GROUP BY`** – It gathers the remaining rows and sorts them into the "buckets" we talked about.
4. **`HAVING`** – It filters out entire *buckets* (e.g., "only keep buckets where the `SUM(Price) > 500`").
5. **`SELECT`** – **Only now does it look at the `SELECT` clause.** It extracts the specific columns you asked for and computes the aggregate math for each bucket.
6. **`DISTINCT`** – It removes any duplicate rows from the final result.
7. **`ORDER BY`** – It sorts the final output rows.
8. **`LIMIT` / `OFFSET**` – It chops off the top X rows to return to you.

### **Why this matters for SDE Interviews:**

Notice that **`FROM` and `GROUP BY` happen BEFORE `SELECT**`.
This is why you cannot use a column alias created in the `SELECT` clause (like `SELECT Price AS order_cost`) inside a `WHERE` or `GROUP BY` clause in standard SQL. The database hasn't even reached the `SELECT` step when it is filtering and bucketing!

---

# Pre aggregation vs late aggregation
## Aggregations
an aggregation is an operation that takes a collection of rows (an input set) and condenses them into a single summary value.
- count()
- min/max()
- avg()

These are almost always paired with the `group by` clause.
## Group by clause
To understand **`GROUP BY`**, forget about code for a second. Think of it as a **"bucketing" mechanism**.

If you have a giant pile of receipts and someone asks you to calculate your total spending, you just add them all up. That is a simple aggregation (`SUM`).

But if they ask, *"How much did you spend **per category** (Food, Rent, Utilities)?"*, you have to do two things:

1. Sort the receipts into separate physical piles (buckets) based on the category.
2. Sum up the receipts *inside each pile* individually.

In SQL, the **`GROUP BY`** clause is step 1 (making the piles). The **aggregate function** is step 2 (calculating the metric inside each pile).

---

### **A Concrete Example**

Imagine we have an `Orders` table with raw data:

| Order_ID | Category | Price |
| --- | --- | --- |
| 1 | Electronics | 800 |
| 2 | Books | 20 |
| 3 | Electronics | 150 |
| 4 | Books | 15 |
| 5 | Clothing | 50 |

If you write this query:

```sql
SELECT Category, SUM(Price)
FROM Orders
GROUP BY Category;

```

Here is exactly what the database engine does behind the scenes:

#### **Step 1: The `GROUP BY` Phase (Bucketing)**

The database scans the `Category` column and creates a temporary bucket for every unique value it finds. It throws the corresponding rows into those buckets:

* **Electronics Bucket:** Contains rows with prices `[800, 150]`
* **Books Bucket:** Contains rows with prices `[20, 15]`
* **Clothing Bucket:** Contains rows with prices `[50]`

#### **Step 2: The Aggregation Phase (Collapsing)**

Now that the data is isolated into buckets, the database applies your aggregate function (`SUM(Price)`) to each bucket *independently*. This collapses the multiple rows in each bucket down to a single summary row:

* **Electronics:** $800 + 150 = \mathbf{950}$
* **Books:** $20 + 15 = \mathbf{35}$
* **Clothing:** $50 = \mathbf{50}$

#### **The Final Output:**

The final result set only has one row per unique group:

| Category | SUM(Price) |
| --- | --- |
| Electronics | 950 |
| Books | 35 |
| Clothing | 50 |

---

### **The Golden Rule of `GROUP BY**`

> **Every column you select must either be inside the `GROUP BY` clause, or wrapped in an aggregate function.**
```sql
SELECT Category, Order_ID, SUM(Price)
FROM Orders
GROUP BY Category;
```
is illegal because how will we map multiple order Ids inside the group to the output we want?

#### **Option A: Wrap it in an aggregate function**

If you don't care about the exact `Order_ID`, but you just want *a* value, wrap it in something like `MAX()` or `MIN()` to collapse it down to a single value.

```sql
SELECT Category, MAX(Order_ID), SUM(Price) 
FROM Orders 
GROUP BY Category;
-- This is legal! The database will confidently output '3'.

```

#### **Option B: Add it to the `GROUP BY` clause**

If you actually want to see the breakdown by both category *and* order ID, you add it to the grouping. This tells the database to make smaller, more specific buckets.

```sql
SELECT Category, Order_ID, SUM(Price) 
FROM Orders 
GROUP BY Category, Order_ID; 
-- This is legal! It creates a unique bucket for "Electronics + Order 1" 
-- and a separate bucket for "Electronics + Order 3".

```

---
Back to the aggregations.
## How are the aggreagtions computed
1. Hash Aggregation
    How it works: The database reads the input rows one by one. It hashes the GROUP BY column(s) to find a specific bucket in an in-memory hash table. It then updates the running aggregate state for that bucket (e.g., increments a counter for COUNT, or adds to a running total for SUM).

        We only store the state for each bucket of the hash table.
    
    Time Complexity: Generally $O(N)$ to scan the table.
    
    Space/Memory Constraints: The hash table must fit in memory. If there are millions of unique groups (high cardinality), the hash table gets too large and the database has to "spill" to disk, which severely degrades performance.

2. Stream (Sort) Aggregation
    How it works: This algorithm requires the input data to be sorted by the GROUP BY column(s) first. The database then reads the rows sequentially. Because the data is sorted, all rows for a specific group arrive consecutively. As soon as the database detects a new GROUP BY key in the stream, it knows the previous group is finished. It emits the final aggregated value for that previous group, clears its memory, and starts over for the new key.
    
    Time Complexity: Scanning is $O(N)$, but if the data isn't already sorted, the preliminary sort step takes $O(N \log N)$.
    
    Space/Memory Constraints: Extremely memory efficient. It operates in $O(1)$ memory because it only needs to keep track of the running aggregate for the current group it is iterating over.

## Pre vs late aggreation
Two competing strategies for deciding when to execute a GROUP BY operator relative to a JOIN operator in a query plan. When a query requires both joining tables and aggregating data, the database optimizer must choose whether to collapse the rows before combining the tables, or combine the tables first and collapse them at the very end.

---

### **The Scenario**

Imagine we want to generate a report showing the **Total Amount Spent** by **Every User**.

* **`Users` Table:** 10,000 rows (contains `User_ID`, `Name`)
* **`Orders` Table:** 1,000,000 rows (contains `Order_ID`, `User_ID`, `Amount`)

Here is how the two strategies execute this query.

### **1. Pre-Aggregation (Early Aggregation)**

With this strategy, the database **aggregates first, then joins**. It pushes the `GROUP BY` operation down directly onto the large table before any join happens.

* **Step 1:** The database scans the 1,000,000 rows of the `Orders` table and groups them by `User_ID`. It computes the `SUM(Amount)` right here.
* *Intermediate Result:* A temporary table of exactly **10,000 rows** (one row per user).


* **Step 2:** The database joins this tiny 10,000-row intermediate table with the 10,000-row `Users` table to fetch the users' names.
* **Total Join Overhead:** The join only has to process $10,000 \times 10,000$ relationships. The massive 1-million-row table never enters the join phase.

### **2. Late Aggregation (Post-Aggregation)**

With this strategy, the database **joins first, then aggregates**. This follows the literal, default order of SQL execution we discussed earlier.

* **Step 1:** The database joins the raw `Users` table (10,000 rows) with the raw `Orders` table (1,000,000 rows).
* *Intermediate Result:* A massive intermediate table of **1,000,000 rows**. Every row now contains the order amount *plus* duplicate user data (like the user's name, email, etc.) copied over for every single order they made.


* **Step 2:** The database performs a Hash or Stream aggregation on this 1,000,000-row intermediate table to finally compress it down to the final 10,000 user rows.
* **Total Join Overhead:** The join engine had to process all 1,000,000 raw rows, creating a massive memory footprint.

---

### **Which one is better? (The Interview Trade-off)**

You might look at the example above and think, *"Pre-aggregation is obviously always better."* But in database systems, the right answer is always "it depends."

A smart Cost-Based Optimizer (CBO) will choose between them based on **selectivity** and **cardinality**:

| Factor | Win Goes To | Why? |
| --- | --- | --- |
| **No Filters / High Duplication** | **Pre-Aggregation** | If you are processing the entire dataset, shrinking 1,000,000 rows to 10,000 rows *before* the join saves massive amounts of CPU and RAM during the join phase. |
| **Heavy Filtering (`WHERE` clause)** | **Late Aggregation** | If you add a clause like `WHERE Users.Country = 'Iceland'`, and only 5 users live there, a Late Aggregation join will immediately discard 99.9% of the orders via index lookups. Pre-aggregating all 1,000,000 orders beforehand would be a massive waste of work for data that gets thrown away. |
| **High Key Cardinality** | **Not much difference** | If the `GROUP BY` key is almost unique (e.g., grouping orders by a timestamp down to the millisecond), pre-aggregation won't actually reduce the row count at all. You waste time trying to aggregate, only to pass the same 1,000,000 rows to the join anyway. |

# Filter/Predicate pushdown
A "predicate" is simply a condition in your WHERE or JOIN ON clause that evaluates to true or false (e.g., Age > 21 or Status = 'Active').

Pushdown means moving the evaluation of that condition as close to the physical storage layer (the disk or memory scan) as humanly possible. Instead of reading all data and filtering it later in the execution tree, the database filters the data while reading it.

The Query Tree Comparison
Let’s look at how a database optimizer rearranges a query plan for a database containing a Users table (1,000,000 rows) and an Orders table (10,000,000 rows), where we only want data for User_ID = 42.

```SQL
SELECT * FROM Users U
JOIN Orders O ON U.User_ID = O.User_ID
WHERE U.User_ID = 42;
```

## Scenario A: No Predicate Pushdown (The Default Order)
Scan: The database scans all 1,000,000 users and all 10,000,000 orders from disk.

Join: The join engine processes millions of rows to match users to their orders.

Filter: At the very top of the execution tree, the WHERE User_ID = 42 filter is applied, throwing away 99.99% of the joined data.

Why it’s bad: You wasted massive disk I/O, network bandwidth, and CPU cache space joining data you were immediately going to delete.

## Scenario B: With Predicate Pushdown (Optimized)
The optimizer looks at the WHERE clause and pushes the User_ID = 42 condition down past the join, directly into the table scan step.

Filter Early (Pushed Down): The database applies the filter during the scan of the Users table. It pulls exactly 1 row (User 42).

Filter Early Part 2: Because of the join condition (U.User_ID = O.User_ID), the optimizer realizes it can also push the predicate down to the Orders table scan. It only pulls the orders for User_ID = 42 (say, 5 rows).

Join: The join engine only has to join 1 row with 5 rows.

Why it’s great: You read next to nothing from disk, and the join happens instantly in the CPU cache.

Bad: Bypassing the database's filtering engine
The database has to scan and transmit millions of users over the network all_users = db.query(User).all() # Pulls everything!

The "predicate" is evaluated inside the application CPU active_users = [u for u in all_users if u.status == "ACTIVE"]

The Good Pattern (Pushed Down to DB)

Good: The filter is evaluated directly at the storage level
Only the matching rows are sent over the network active_users = db.query(User).filter(User.status == "ACTIVE").all()

How to spot it: Look for code that fetches a list (.all(), .toList(), .collect()) and immediately runs a .filter(), .where(), or a for/if loop over it.

Predicate ordering within a filter: Put the most selective predicate (eliminates most rows) first, cheapest to evaluate first.


# Late Materialization / Projection Pushdown
- Filtering (Selection): Slices the grid horizontally (keeping only certain rows).
- Projection: Slices the grid vertically (keeping only certain columns).

It is defined entierly by our `SELECT` clause

Projection pushdown
    Projection Pushdown is an optimization where the database engine tells the physical storage layer to read and return only the specific columns requested by the query, discarding the unused columns at the absolute earliest point possible—right at the disk, SSD, or memory scan level.

    Without projection pushdown, the database engine might read entire records from disk and only strip away the unwanted columns at the very end of execution. With pushdown, the vertical slicing happens at the very bottom of the execution tree.

    1. Row-Oriented Databases (e.g., PostgreSQL, MySQL)

        In a row-oriented database, data is physically stored on disk row-by-row inside pages.

        The Reality: Because a row is stored contiguously, the physical disk read arm must read the entire page containing the row. The database cannot avoid physically reading the unused columns off the disk.

        How Pushdown Still Helps: Even though the bytes are read from disk into the buffer cache, projection pushdown ensures that the database engine does not allocate memory for the unused columns in the execution tuples (the memory structures used during joins, sorts, and filters). It prevents CPU cache pollution by not copying the unwanted columns up the execution plan.

    2. Column-Oriented Databases (e.g., Parquet files, ClickHouse, Snowflake)

        In a columnar database, every column is stored in its own separate file or contiguous set of disk blocks.

        The Reality: This is where projection pushdown shines. If a table has 100 columns and you only select 2, the storage engine completely ignores the other 98 columns on disk. It never reads their files or memory blocks.

        The Saving: Your physical disk I/O scales down proportionally. If you only query $2\%$ of the columns, you only perform roughly $2\%$ of the physical disk reads.


Materialization is the process of physically constructing a row (or a set of rows) in memory so that the database engine can process it. When a database reads data from a disk or SSD, the data is stored in bytes. To do anything useful with this data (like checking a filter condition, performing a join, or returning it to the client), the database must unpack those bytes and construct a structured object in RAM—like an array of values representing a row. This step of building the physical representation of the data is called materializing the tuple.

Early Materialisation
    The database reads a row from disk and immediately materializes the entire row with all its columns. This full, bulky row is then passed up through every single step of the query plan (joins, filters, sorting).

    The Problem: If a table has 50 columns, but you only need 2, early materialization forces the database to carry the memory overhead of all 50 columns through every join and filter step, wasting CPU cache and RAM.

Late Materialisation
    The database does not construct the full row immediately. Instead, it reads and processes only the bare minimum data needed for the current step (e.g., just the ID and the column being filtered). It carries lightweight pointers or Tuple IDs ($TIDs$) through the query plan.

    Only at the very last step, right before sending the results to the user, does it go back to the storage layer to fetch the remaining columns and assemble ("materialize") the final rows.

    Used columns are kept as lightweight identifiers ($TIDs$) to avoid carrying heavy payloads through joins or filters.

# Bloom filter as join pre-filter
If all of your data fits in the RAM of a single machine, you generally do not need a Bloom filter for a standard in-memory Hash Join. You can just probe the hash table directly. the Bloom filter becomes a game-changer because of two major real-world constraints: physical boundaries (networks/disks) and CPU cache limits.

You check the hash table to get the exact match and retrieve the joined columns. You check the Bloom filter to avoid the expensive physical costs (Network transfer, Disk I/O, and CPU RAM cache misses) of looking at rows that have no chance of matching.

1. The Distributed Scenario: 
    The Network BoundaryIn modern analytical databases (like Snowflake, BigQuery, or Spark), data is spread across a cluster of different physical machines.Imagine Node A has the small table and builds the Hash Table. Node B has the massive table.
    
    Without a Bloom Filter: Node B has no idea which of its rows will match. It must pack up all of its raw rows and send them over the network to Node A so Node A can probe the Hash Table. Shuffling millions of non-matching rows over the network is incredibly slow.
    
    With a Bloom Filter: Node A sends a tiny Bloom filter (a few megabytes of bits) over the network to Node B. Node B tests its rows locally. It discards $99\%$ of them immediately. Only the remaining $1\%$ of rows are sent over the network to Node A to be joined.
    
    The takeaway: You cannot easily "check the hash table directly" if the hash table is on a different machine across the data center. Sending a tiny Bloom filter is much cheaper than moving the hash table or the raw data.
    
2. The Single-Machine Scenario: CPU Cache vs. RAM
    Even on a single machine, if the small table has millions of rows, its Hash Table might be several gigabytes in size.
    
    RAM is slow: A gigabyte-sized hash table cannot fit into the CPU's fast L1/L2/L3 cache. Every time you probe the hash table, the CPU has to fetch data from physical RAM. This causes a CPU cache miss, which takes about 100ns (an eternity for a CPU processing billions of rows).
    
    Cache is fast: A Bloom filter representing those same millions of rows might only be a few megabytes. This easily fits entirely inside the CPU's L3 cache. Probing the Bloom filter happens in near-instant CPU cache speeds ($\sim 1\text{ns}$).
    
    The workflow:
        The CPU probes the ultra-fast, in-cache Bloom filter first.
        
        If the Bloom filter says "No" (which it does for most rows in a selective join), the database immediately skips the row. You completely avoid a slow RAM lookup. 
        
        Only if the Bloom filter says "Yes" (or "Maybe") does the database pay the high cost to do a random memory lookup in the actual giant Hash Table.

3. The Storage Engine (Disk I/O)
    If the massive table is stored on disk (like a columnar Parquet file), we want to avoid even reading the row into memory.

    The database can push the Bloom filter down to the storage reader. If a block of data on disk only contains keys that the Bloom filter says "No" to, the database engine skips reading that entire block of disk storage into memory.

# Volcano vs vectorised model of processing data
## SIMD brief
Instead of performing a calculation on a single pair of numbers at a time, a SIMD instruction allows the CPU to perform the exact same operation on an entire array (vector) of data points simultaneously in a single CPU clock cycle.

Traditional Loop (4 Cycles):
Cycle 1: Add A[0] + B[0] -> C[0]
Cycle 2: Add A[1] + B[1] -> C[1]
Cycle 3: Add A[2] + B[2] -> C[2]
Cycle 4: Add A[3] + B[3] -> C[3]

SIMD Execution (1 Cycle):
Vector Register 1: [ A[0], A[1], A[2], A[3] ]
                           +   (Single ADD instruction)
Vector Register 2: [ B[0], B[1], B[2], B[3] ]
                           =
Vector Register 3: [ C[0], C[1], C[2], C[3] ]


When you write clean, branch-free loops over flat arrays and compile with optimization flags (-O3 -march=native), the compiler uses auto-vectorization to automatically translate your standard loops into these high-performance SIMD instructions.

---

## Volcano Execution vs. Vectorized Model

In database engine design, query execution models dictate how data flows from the storage layer up through physical operators (filters, joins, aggregations) to produce the final output. The transition from the classic Volcano Iterator Model to Vectorized Execution is one of the most critical shifts in modern high-performance databases (e.g., SingleStore, ClickHouse, DuckDB, Snowflake).

1. The Volcano Iterator Model (Row-at-a-Time)

Developed by Goetz Graefe in 1994, the Volcano Model is the traditional standard for query execution.

How it works:

Every database operator (Scan, Filter, Project, Join) is implemented as an Iterator. They all expose a simple, uniform interface containing three main functions:

open(): Set up resources.

next(): Return the next single row (tuple) in the stream, or null if the stream is exhausted.

close(): Clean up resources.

Operators are chained together in an execution tree. When the client requests data, the top operator calls next() on its child, which calls next() on its child, cascading down to the table scan.

// --- What Volcano looks like in C++ code ---

```cpp
struct Tuple {
    int id;
    int value;
};

class VolcanoOperator {
public:
    virtual ~VolcanoOperator() = default;
    virtual void open() = 0;
    virtual Tuple* next() = 0; // Returns ONE row
    virtual void close() = 0;
};

// Example implementation of a Filter operator: WHERE value > 100
class VolcanoFilter : public VolcanoOperator {
private:
    VolcanoOperator* child;
    Tuple* current_tuple;

public:
    VolcanoFilter(VolcanoOperator* childOperator) : child(childOperator), current_tuple(nullptr) {}

    void open() override { child->open(); }
    void close() override { child->close(); }

    Tuple* next() override {
        // We pull rows one-by-one from the child operator
        while ((current_tuple = child->next()) != nullptr) {
            // Virtual function call overhead + pointer indirection on every single row!
            if (current_tuple->value > 100) {
                return current_tuple; 
            }
        }
        return nullptr;
    }
};
```

The Problem with Volcano on Modern Hardware (The Bottleneck)

While Volcano is incredibly elegant, simple to implement, and memory-efficient (it only keeps one row in memory at a time), it performs poorly on modern CPUs because of two primary issues:

Massive Virtual Function Call Overhead:
Every call to next() is a virtual function call. Virtual calls require a vtable lookup, which introduces a tiny CPU overhead. While negligible for a few rows, if you run a query processing 100,000,000 rows, you pay the vtable lookup penalty hundreds of millions of times.

Poor Instruction and Data Cache Locality:
Because rows are represented as pointers to objects scattered across memory (especially in row-oriented databases), jumping from row pointer to row pointer causes constant L1/L2 data cache misses.
Furthermore, the CPU instruction cache (I-cache) is constantly swapped out as execution jumps back and forth between different virtual operator methods. The CPU pipeline cannot be kept full, leading to pipeline stalls.

2. The Vectorized Model (Batch-at-a-Time)

The Vectorized Model (first popularized by the MonetDB/X100 project) retains the iterator tree structure but changes the fundamental data flow. Instead of passing a single row up the tree, next() returns a vector (or batch) of rows (typically 1024 values) at once.

How it works:

Instead of dealing with pointers to individual objects, data inside a batch is stored in contiguous, flat arrays (one array per column).

// --- What Vectorization looks like in C++ code ---
```cpp

const int BATCH_SIZE = 1024;

struct VectorBatch {
    int* id_col;
    int* value_col;
    int size; // Number of valid elements currently in this batch
};

class VectorOperator {
public:
    virtual ~VectorOperator() = default;
    virtual void open() = 0;
    virtual int next(VectorBatch& batch) = 0; // Populates batch, returns count of active rows
    virtual void close() = 0;
};

// Example implementation of a Vectorized Filter operator: WHERE value > 100
class VectorFilter : public VectorOperator {
private:
    VectorOperator* child;

public:
    VectorFilter(VectorOperator* childOperator) : child(childOperator) {}

    void open() override { child->open(); }
    void close() override { child->close(); }

    int next(VectorBatch& batch) override {
        // Amortized virtual call: One call fetches 1024 rows!
        int count = child->next(batch);
        if (count == 0) return 0;

        int write_idx = 0;
        // The loop operates on flat, contiguous memory arrays
        for (int i = 0; i < count; i++) {
            if (batch.value_col[i] > 100) {
                // Keep the row by compacting it in the batch
                batch.id_col[write_idx] = batch.id_col[i];
                batch.value_col[write_idx] = batch.value_col[i];
                write_idx++;
            }
        }
        
        // Return the number of elements that survived the filter
        return write_idx; 
    }
};
```

3. Why Vectorization Matters: The Hardware Perspective

In system design interviews, the most high-signal answers explain performance through hardware limitations. Vectorization addresses hardware execution constraints in three key ways:

1. Amortizing Virtual Calls

If a query processes 1,024,000 rows:

Volcano: Executes $1,024,000$ virtual function calls.

Vectorized: Executes only $1,000$ virtual function calls (since each call processes 1,024 rows).
The overhead of the virtual call abstraction is reduced by a factor of 1000x, practically making it free.

2. Exploiting CPU L1 Cache

By storing data in flat primitive arrays (e.g., int* value_col), elements are packed tightly in adjacent memory addresses.

When the CPU fetches batch.value_col[0], the hardware prefetcher automatically pulls the subsequent values (batch.value_col[1..15]) into the ultra-fast L1 cache. Cache misses drop to near zero.

3. Enabling SIMD (Single Instruction, Multiple Data)

Modern CPUs have wide vector registers (AVX-2, AVX-512) capable of performing operations on multiple data points simultaneously in a single CPU cycle.
If your query loops over a contiguous array of numbers, compiling the code with optimizations (-O3 -march=native) allows the compiler to generate SIMD instructions, checking or updating 8 to 16 rows at the exact same time.

4. The SIMD Killer: Branches inside the Loop

If you are asked to write or optimize a query engine loop in an interview, you must keep in mind that conditional branches (if statements) kill auto-vectorization.

Why branches prevent auto-vectorization:

If a loop contains a branch (if (values[i] > 100)), the CPU's branch predictor must guess which branch the data will take. If the branch behavior is highly unpredictable (e.g., exactly 50% of the values are $> 100$), the CPU experiences massive branch mispredictions, which flushes the instruction pipeline.

Furthermore, SIMD instructions rely on applying the exact same instruction uniformly to a register of values. If different elements in the array take different code paths, SIMD cannot be easily used.

The Optimization: Writing Branchless Loops

We can restructure branching logic into arithmetic or bitwise expressions to help the compiler auto-vectorize the code.
```cpp
// --- Branching Loop (Kills SIMD Auto-vectorization) ---
int count = 0;
for (int i = 0; i < batch_size; i++) {
    if (prices[i] > threshold) {  // Conditional Branch
        matching_indices[count++] = i;
    }
}

// --- Branchless Loop (Enables SIMD Auto-vectorization) ---
int count = 0;
for (int i = 0; i < batch_size; i++) {
    // Boolean condition evaluates to 1 (true) or 0 (false)
    bool is_match = (prices[i] > threshold);
    
    // Store index unconditionally. If it was not a match, 
    // the next matching index will overwrite it.
    matching_indices[count] = i;
    
    // Increment count by the value of the boolean (0 or 1)
    count += is_match; 
}
```

By removing the if statement, we remove the branch instruction (je / jne in assembly). The loop executes the same sequence of instructions for every single element, allowing the compiler to confidently pack the operations into SIMD registers.

# Custom memory allocator
This is a fundamental concept for low-level systems engineering and a major differentiator in backend or database engine interviews. High-performance software rarely relies blindly on general-purpose memory allocation.

---

## The Gap: Why General Purpose Allocators (`malloc` / `new`) Fail

By default, when you call `malloc` in C or `new` in C++, you are using a general-purpose allocator (like `ptmalloc`, `jemalloc`, or `tcmalloc`). These allocators are marvels of software engineering, but they are designed to handle *any* possible scenario: allocations ranging from 8 bytes to 8 gigabytes, varying object lifetimes, and requests coming from hundreds of concurrent threads.

To support this flexibility, general-purpose allocators suffer from three major performance killers:

1. **Thread Contention:** Multiple threads allocating memory simultaneously have to acquire global or arena locks. Even with thread-local caches, highly concurrent systems experience locking overhead.
2. **Memory Fragmentation:** Allocating and freeing objects of random sizes leaves unpredictable "holes" in your memory space. Over time, you might have plenty of total free memory, but not enough *contiguous* space to fulfill a large allocation request.
3. **Bookkeeping Overhead:** Every time you call `malloc(size)`, the allocator has to store metadata (usually a hidden header right before the returned pointer) to know exactly how many bytes to release when you eventually call `free()`.

In an SDE interview, if you are optimizing a high-throughput loop that creates and destroys millions of objects (like database rows, query operators, or network packets), your target answer should be a **Custom Allocator**. The two most common types are **Arenas** and **Pools**.

---

## 1. The Arena Allocator (Bump Allocator)

An **Arena Allocator** pre-allocates a massive, contiguous block of memory upfront from the OS heap. When the application requests memory, the arena simply hands back a pointer to the current position and "bumps" the pointer forward by the requested size.

### The Rules:

* **Allocation is $O(1)$:** It is just an addition operation (moving a pointer).
* **Individual Deallocation is Forbidden:** You cannot free a single object inside an arena.
* **Bulk Freeing is $O(1)$:** You clear the entire arena all at once by resetting the pointer back to the beginning of the block.

### Code Representation (C++)

```cpp
#include <cstddef>
#include <cstdint>
#include <new>

class ArenaAllocator {
private:
    uint8_t* buffer;      // The large pre-allocated memory block
    size_t capacity;      // Total size of the arena
    size_t offset;        // Current bump pointer position

public:
    ArenaAllocator(size_t size) : capacity(size), offset(0) {
        buffer = new uint8_t[capacity];
    }

    ~ArenaAllocator() {
        delete[] buffer;
    }

    // Allocation is an ultra-fast O(1) pointer bump
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        // Handle alignment adjustments
        size_t current_address = reinterpret_cast<size_t>(buffer + offset);
        size_t padding = (alignment - (current_address % alignment)) % alignment;
        
        if (offset + padding + size > capacity) {
            throw std::bad_alloc(); // Out of arena memory
        }

        offset += padding;
        void* ptr = buffer + offset;
        offset += size;
        return ptr;
    }

    // Individual freeing is a no-op!
    void deallocate(void* ptr) {
        // Do nothing. Individual objects cannot be reclaimed.
    }

    // Bulk reset reclaiming all memory instantly
    void reset() {
        offset = 0;
    }
};

```

### When to use it in code:

Look for operations with a **clear, bounded lifecycle**.

* **The Classic Database Use Case:** Every time an incoming SQL query executes, the engine creates a "Query Arena". All intermediate join rows, filter expressions, and string operations allocate memory inside this query arena. Once the query finishes and returns the final result to the client, the engine throws away the entire arena with a single `reset()` call.

---

## 2. The Pool Allocator (Fixed-Size Object Allocator)

A **Pool Allocator** is designed for scenarios where you need to create and destroy millions of objects of the **exact same size** (e.g., all objects are exactly 64 bytes).

The pool pre-allocates a large chunk of memory and divides it into an array of equal-sized slots (slots/chunks). It keeps track of which slots are free using a lightweight structure called a **Free List**.

### The Rules:

* **No Alignment Issues or Metadata:** Every slot is identical.
* **True Individual Deallocation is $O(1)$:** You can free any slot at any time. The allocator just pushes that slot back onto the Free List.
* **Zero Fragmentation:** Because every slot fits exactly one object, you can never end up with awkward, unusable gaps of memory.

### Code Representation (C++)

```cpp
class PoolAllocator {
private:
    struct Node {
        Node* next; // Embedded linked list pointer inside the free slots
    };

    uint8_t* raw_buffer;
    size_t object_size;
    size_t object_capacity;
    Node* free_list_head;

public:
    PoolAllocator(size_t obj_size, size_t capacity) 
        : object_size(obj_size >= sizeof(Node) ? obj_size : sizeof(Node)), 
          object_capacity(capacity), 
          free_list_head(nullptr) 
    {
        raw_buffer = new uint8_t[object_size * object_capacity];
        
        // Thread all slots into the free list initially
        for (size_t i = 0; i < object_capacity; ++i) {
            Node* current_node = reinterpret_cast<Node*>(raw_buffer + (i * object_size));
            current_node->next = free_list_head;
            free_list_head = current_node;
        }
    }

    ~PoolAllocator() {
        delete[] raw_buffer;
    }

    // O(1) Pop from the free list
    void* allocate() {
        if (free_list_head == nullptr) {
            throw std::bad_alloc(); // Pool is full
        }
        
        Node* slot = free_list_head;
        free_list_head = free_list_head->next;
        return reinterpret_cast<void*>(slot);
    }

    // O(1) Push back to the free list
    void deallocate(void* ptr) {
        if (ptr == nullptr) return;
        
        Node* slot = reinterpret_cast<Node*>(ptr);
        slot->next = free_list_head;
        free_list_head = slot;
    }
};

```

### When to use it in code:

Look for long-lived systems managing uniform data structures.

* **Examples:** State matrices in game engines (e.g., millions of uniform Bullet particles), network packet buffers in a connection multiplexer, or traditional index nodes (B+ Tree nodes) inside a storage engine layer.

---

## Summary for the Interview

When asked how to optimize memory allocations inside a hot loop, your mental model should map to this summary matrix:

| Metric / Feature | General Purpose (`malloc`) | Arena (Bump) Allocator | Pool Allocator |
| --- | --- | --- | --- |
| **Allocation Cost** | Variable (Slow due to searches/locks) | $O(1)$ (Incredibly fast addition) | $O(1)$ (Pop from linked list) |
| **Deallocation Cost** | Variable | Forbidden individually ($O(1)$ for bulk) | $O(1)$ (Push to linked list) |
| **Memory Fragmentation** | High risk over long run | None (Packed sequentially) | None (Slots perfectly matched) |
| **Best Used For** | General application logic | Bounded-lifetime tasks (Queries, Requests) | Uniform, identical objects (Nodes, Packets) |
| **Cache Performance** | Poor (Scattered heap nodes) | Excellent (Contiguous data streaming) | Excellent (Predictable dense layout) |

# Predicate order evaluation
predicate evaluation order and short-circuiting on batches. When you have WHERE a > 5 AND b < 10, the vectorized engine evaluates the most selective predicate first to shrink the batch before evaluating the next one — so the second predicate runs on fewer rows. This is a real, nameable optimization (selectivity-based predicate reordering) that sits right at the intersection of your Tier 1 query-algorithmic list and this Tier 2 performance list. It's a strong thing to volunteer because it shows you think about data volume flowing through stages, not just per-line code speed.


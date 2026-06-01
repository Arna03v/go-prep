## The Problem

You are building an indexing library for a database engine. The table has the following schema:

```
Id   uint64
Text string  // averages 5KB per record
```

The total table size is 100GB. Your library must build an in-memory index over this table and expose an iterator interface for querying it.

**Constraints:**
- The index must not exceed 50GB in memory
- The index must support concurrent reads from multiple goroutines simultaneously
- The index must support writes — new records can be inserted while reads are in progress
- Query: `SELECT * FROM table WHERE Id > X LIMIT Y`

**Initial APIs given to you:**

```go
// Index manages the in-memory index over the table.
type Index interface {
    // Insert adds a new record to the index.
    // The index stores only the Id and a pointer/offset to
    // where the Text lives in main storage — not the Text itself.
    // Must be safe to call concurrently with reads.
    Insert(id uint64, text string) error

    // Query returns an iterator over all records where Id > minId.
    // Results are limited to `limit` records.
    // Multiple goroutines can call Query simultaneously.
    Query(minId uint64, limit int) Iterator

    // Close shuts down the index and releases all resources.
    Close() error
}

// Iterator is a cursor over a query result set.
// Each goroutine owns its own iterator instance — not goroutine-safe.
// Fetches one record at a time from main storage using offsets from the index.
type Iterator interface {
    // HasNext returns true if there are more records to fetch.
    HasNext() bool

    // Next returns the next matching record and advances the cursor.
    // Fetches the full Text from main storage using the stored offset.
    // Returns an error if the record cannot be fetched.
    Next() (Record, error)

    // Close releases the iterator's resources.
    // Must be called when the caller is done iterating.
    Close() error
}

// Record is a single row returned by the iterator.
type Record struct {
    Id   uint64
    Text string
}
```

---

Does this feel complete? If yes I will write the handoff prompt.
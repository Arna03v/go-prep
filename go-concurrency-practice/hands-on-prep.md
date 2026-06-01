# Practicing Go concurrency primtives
---

## Program 1: Fixed-Size Bounded Blocking Queue (Ring Buffer)

This problem tests your mastery of condition variables, precise locking boundaries, and memory protection. You cannot resize the underlying storage.

### The Objective

Implement a thread-safe, fixed-size queue where writing to a full queue blocks until space is available, and reading from an empty queue blocks until data is available.

### API Contract

```go
type BoundedQueue struct {
    // Define structural elements here
}

func NewBoundedQueue(capacity int) *BoundedQueue {
    // Initialize elements
}

func (q *BoundedQueue) Enqueue(item interface{}) {
    // Blocks if full
}

func (q *BoundedQueue) Dequeue() interface{} {
    // Blocks if empty
}

```

### Primitives to Use

* `sync.Mutex`
* `sync.Cond` (Hint: You will need two conditions or a shared broadcast)
* `defer`

### Critical Edge Cases to Handle

* **Spurious Wakeups:** Ensure your condition checks inside `Enqueue` and `Dequeue` loop continually (`for` loop condition check) rather than checking via an `if` statement.
* **Deadlock Prevention:** Ensure you do not hold locks while calling methods that could permanently block the caller outside the data structure's control.

---

## Program 2: Concurrent Multi-Read Cache with Lazy Singleton Initialization

### The Objective
Design an in-memory string-to-string cache backed by a map. The cache lazily initializes its underlying heavy backup storage connection upon the first `Get` hit. It must support high-concurrency reading, safe map insertion, and a graceful exit mechanism that ensures no operations are dropped midway during shutdown.

### Clarifications
- **"Heavy backup storage"** is a stub. Simulate it with a function `initBackupStorage()` that prints "initializing" or sleeps briefly. The point is it must run exactly once no matter how many goroutines call `Get` simultaneously. `sync.Once` is how you guarantee that.
- **"Background routines"** in `Close()` should be simulated — for example a goroutine that periodically cleans up expired keys or just runs in a loop. The point is practicing the `wg.Wait()` + `ctx.Done()` select pattern, not building a real background job.
- **Hard constraint:** Never call `Lock()` while holding `RLock()` in the same goroutine. If you need to upgrade from a read to a write inside `Get()`, release the `RLock()` first, then acquire `Lock()`.

### API Contract

```go
type SecureCache struct {
    // Define structural elements here
}

func NewSecureCache() *SecureCache {
    // Initialize elements
}

func (c *SecureCache) Get(key string) (string, bool) {
    // Must handle safe parallel reads
    // Must initialize heavy storage EXACTLY ONCE on the first call
}

func (c *SecureCache) Set(key string, value string) {
    // Safe parallel write protection
}

func (c *SecureCache) Close(ctx context.Context) error {
    // Signals all pending background routines to stop and waits for them
    // Returns error if context expires (times out) before routines clean up
}
```

### Primitives to Use
- `sync.RWMutex`
- `sync.Once`
- `sync.WaitGroup`
- `context.Context`
- `defer`

### Critical Edge Cases to Handle
- **Read-to-Write Upgrade Trap:** Never attempt to acquire a `Lock()` while holding a `RLock()` within the same goroutine — this results in a permanent deadlock.
- **Context Expiry during Wait:** Implement a pattern where `wg.Wait()` is intercepted by a `select` block listening to `ctx.Done()` to prevent infinite blocking on shutdown if a background job hangs.

---

## Program 3: Token-Bucket Rate Limited Worker Pool

### The Objective
Create a task execution pool containing a fixed number of worker goroutines. The pool receives jobs, disperses them among internal workers (fan-out), collects output into a singular outbound pipeline (fan-in), and applies a rate limiter using a tick-producing channel to guarantee processing thresholds are met.

### Clarifications
- **"Rate limiter"** means use `time.NewTicker(time.Second / rateLimitPerSec)` to produce a tick channel. A worker can only execute a job when it receives a tick. This is the token — the ticker is the token source. You are not using a mutex for rate limiting, only channels and `select`.
- **"Fan-out"** means jobs flow from one `Submit` call into a shared job channel that all workers read from — one source, many consumers.
- **"Fan-in"** means all workers write their results into one shared results channel — many sources, one destination.
- **Hard constraints:**
    - `Submit` must not panic if `Stop()` has already been called and closed the job channel. Use a `select` with `ctx.Done()` and a done channel to guard against writing to a closed channel.
    - After `Stop()` returns, zero goroutines should still be running. Verify with `runtime.NumGoroutine()` if you want to test it.

### API Contract

```go
type Job func() string

type Dispatcher struct {
    // Define structural elements here
}

func NewDispatcher(workerCount int, rateLimitPerSec int) *Dispatcher {
    // Spin up background worker goroutines
}

func (d *Dispatcher) Submit(ctx context.Context, job Job) error {
    // Push a job to the workers. Return error if context times out or cancels.
}

func (d *Dispatcher) Results() <-chan string {
    // Expose read-only stream of completed job outputs
}

func (d *Dispatcher) Stop() {
    // Tear down workers and close channels cleanly without panicking
}
```

### Primitives to Use
- Buffered channels (for the token bucket and job pipeline)
- Unbuffered channels (for structural synchronization)
- `context.Context` (for request timeout thresholds)
- `select` loops

### Critical Edge Cases to Handle
- **Panics from Closed Channels:** Ensure `Submit` does not attempt to write to a channel that has been closed by `Stop`.
- **Leaking Goroutines:** Ensure workers stop executing and exit completely if the job channel is closed or the context is cancelled. Verify no goroutines remain active after `Stop()` completes.

---

## Track 1: LLD Design Pattern Exercises

### What These Exercises Are

These are **skeleton exercises**. The goal is NOT to implement working logic. The goal is to practice the architecture of a library.

For each exercise, produce a single Go file containing:

#### 1. The public interface with all method signatures
**The Goal**

Separate the contract of your library from its implementation. Callers should depend on what your library does, not how it does it.

**Wrong approach — methods directly on struct:**
```go
type Pool struct { ... }
func (p *Pool) Acquire(ctx context.Context) (net.Conn, error) { ... }
```
Caller depends on the concrete type. Contract and implementation are merged. Cannot evaluate API design separately from implementation choices.

- expsong the close function is important to handle the end of lifeycle

**Correct approach — interface first, struct hidden:**
```go
// Public contract — callers only see this
type Pool interface {
    Acquire(ctx context.Context) (net.Conn, error)
    Release(conn net.Conn)
    Close() error
}

// Hidden implementation — callers never touch this directly
type poolImpl struct { ... }
func (p *poolImpl) Acquire(ctx context.Context) (net.Conn, error) { ... }
```

**Why it matters**

The interface is the first thing you put on the Google Doc. It is your answer to "what is the public API." The interviewer evaluates your API design by reading the interface alone — clean method names, right parameters, right return types. They do not need to see `poolImpl` to judge whether your API is good. Callers cannot construct the struct directly — they must go through your constructor, which means you control instantiation completely.

---

#### 2. The unexported implementation struct with all fields — no method bodies
**What it means:** The concrete struct that implements your interface. Unexported means it starts with a lowercase letter — callers cannot construct it directly. All fields are declared, no method bodies.

**How it looks:**
```go
type poolImpl struct {
    mu          sync.Mutex
    connections chan net.Conn
    factory     func() (net.Conn, error)
    maxSize     int
    idleTimeout time.Duration
    closed      bool
    once        sync.Once
}
```

**Purpose:** This is where system design is evaluated. The interviewer looks at your fields and asks: have you identified the right internal state? Do you have the right synchronisation primitives? Is anything missing or unnecessary? Your fields tell the full story of how your library works internally.

**What to ask yourself for each field:** Why does this need to exist? What breaks if I remove it?

---

#### 3. Option functions or config struct as appropriate
**The Goal**

When a caller creates your library, they need to configure it. You need a clean way to accept that configuration without a constructor that takes 10 parameters.

**Config Struct**

Bundle all configuration into a struct. Caller fills in what they need, rest is zero values, constructor applies defaults for anything missing.

```go
type Config struct {
    MaxSize     int
    IdleTimeout time.Duration
}
func NewPool(factory func() (net.Conn, error), cfg Config) *Pool
```

**Functional Options**

Define a function type that mutates the struct. Each option is one such function. Constructor applies defaults first, then runs each option over them.

```go
type Option func(*poolImpl)
func WithMaxSize(n int) Option { return func(p *poolImpl) { p.maxSize = n } }
func NewPool(factory func() (net.Conn, error), opts ...Option) *Pool
```

**Genuine Differences**

- Zero value ambiguity: with config struct, `MaxSize: 0` is ambiguous — did the caller mean zero or did they forget? With functional options, if `WithMaxSize` was never called, the default applies unambiguously.
- Option functions can carry behaviour — one option can set multiple related fields simultaneously. Config struct fields can only hold values.
- Config struct is more readable in one glance — all configuration visible in one place. Functional options require knowing which `WithXxx` functions exist.

**Conclusion**

The differences are real but marginal in practice. Use config struct by default — it is simpler to write under time pressure and more readable in a Google Doc. If the interviewer asks about functional options, you know the trade-off and made a conscious choice. One rule of thumb: if there are only one or two parameters, skip both and just put them in the constructor directly.

---

#### 4. Constructor function with validation logic only — no business logic
**What it means:** The `NewXxx` function that creates and returns your struct. It sets defaults, validates inputs, initialises internal state. No business logic — no goroutines started, no connections made.

**How it looks:**
```go
func NewPool(factory func() (net.Conn, error), opts ...Option) (*poolImpl, error) {
    if factory == nil {
        return nil, errors.New("factory function required")
    }
    p := &poolImpl{
        factory:     factory,
        maxSize:     10,              // default
        idleTimeout: 30 * time.Second, // default
        connections: make(chan net.Conn, 10),
    }
    for _, opt := range opts {
        opt(p)
    }
    return p, nil
}
```

**Purpose:** Two signals here. First — you never leave a zero-value struct usable, which is a correctness principle. Second — validation in the constructor means invalid configuration fails immediately at creation time, not silently later during operation. The interviewer evaluates whether you caught the obvious invalid inputs.

---

#### 5. Sentinel error variables or custom error types
**What it means:** Define the error conditions your library can produce, upfront, as package-level variables or structs.

- generally include; if a struct is closed
- for all paramaters; what happens when the paramater is invalid
- and then something else, but go through the above 2 before reching this

**How it looks:**
```go
// Sentinel error — simple, caller checks identity
var ErrClosed = errors.New("pool is closed")
var ErrPoolExhausted = errors.New("pool exhausted")

// Custom error type — when caller needs to inspect the error
type CircuitOpenError struct {
    RetryAfter time.Time
}
func (e *CircuitOpenError) Error() string {
    return fmt.Sprintf("circuit open, retry after %s", e.RetryAfter)
}
```

**Purpose:** Forces you to think about your library's failure modes before implementation. In the interview this signals API maturity — you've thought about what can go wrong and given callers a clean way to handle it.

**When to use which:** Sentinel for simple yes/no failures (`ErrClosed`, `ErrNotFound`). Custom type when the caller needs data attached to the error (`RetryAfter`, `Attempts`).

---

#### 6. Compile-time interface verification: `var _ InterfaceName = (*implStruct)(nil)`
**What it means:** A single line that tells the Go compiler to verify your implementation struct actually satisfies your interface. Causes a compile error if it doesn't.

```go
// 1. Public interface
type Pool interface {
    Acquire(ctx context.Context) (net.Conn, error)
    Release(conn net.Conn)
    Close() error
}

// 2. Internal implementation struct
type poolImpl struct {
    mu      sync.Mutex
    connections chan net.Conn
}

// 3. Methods on the implementation struct
func (p *poolImpl) Acquire(ctx context.Context) (net.Conn, error) { ... }
func (p *poolImpl) Release(conn net.Conn) { ... }
func (p *poolImpl) Close() error { ... }

// 4. Compile-time verification
var _ Pool = (*poolImpl)(nil)
```

Line 4 is saying: "a nil pointer of type `*poolImpl` must satisfy the `Pool` interface." If you forgot to implement `Release` on `poolImpl`, this line produces a compile error immediately.

**One naming note:** `poolImpl` is the Go convention — unexported, lowercase. Your `PoolImplementation` would be exported because it starts with uppercase, which defeats the purpose of hiding the implementation. Keep the implementation struct unexported always.

**Purpose:** In a Google Doc you can't run code. This line tells the interviewer you understand that Go interface satisfaction is implicit — nothing enforces it unless you do this explicitly. It's a one-liner that signals depth of Go knowledge.

**How to read it:** "Assign a nil pointer of type `*poolImpl` to a variable of type `Pool`, discarding the result. If `*poolImpl` doesn't implement `Pool`, this line won't compile."

---

#### 7. Empty `Close() error` block using `sync.Once` — structure only
**What it means:** The shutdown method. `sync.Once` guarantees the teardown logic runs exactly once even if multiple goroutines call `Close()` simultaneously.

Different desgin patterns: 
- can use a bool closed
- can also use a context -> WithCancel
    - make the closing of done channel idempotent
- can also just use a done channel
    but have to use sync.Once to ensure it is only closed once
- if the stop() needs to wait for background processes to finish, the struct will also have a sync.WaitGroup

```Go

done := make(chan struct{})
	go func() {
		b.wg.Wait()
		close(done)
	}()

	select {
	case <-done:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}

````

**How it looks:**
```go
func (p *poolImpl) Close() error {
    var err error
    p.once.Do(func() {
        p.mu.Lock()
        defer p.mu.Unlock()
        p.closed = true
        // teardown logic would go here
        // close channels, drain connections, etc.
    })
    return err
}
```

**Purpose:** Two things. First — idempotency. Calling `Close()` twice must not panic. Without `sync.Once`, closing a channel twice panics. Second — every public method must check `closed` before operating and return `ErrClosed` if true. The `Close()` skeleton anchors that entire lifecycle pattern. The interviewer checks whether you've thought about what happens after shutdown.

---

## Exercise 1: Connection Pool

A concurrent connection pool library that manages a set of reusable network connections. Callers acquire a connection, use it, and return it. If a connection encounters a network failure, the caller marks it as destroyed rather than returning it.

### Clarifications
- **"Factory function"** is a caller-provided function of signature `func() (net.Conn, error)` that the pool calls internally to create new connections. It is passed in as a functional option.
- **"Mark as destroyed"** means a separate method `Destroy(conn net.Conn)` that discards the connection instead of returning it to the pool.
- **"Idle timeout"** is a `time.Duration` config value. You do not need to implement the eviction logic — just make sure the field exists on the struct and is set via a functional option.
- `Close()` prevents new `Acquire()` calls and is idempotent.

### Patterns to Hit
- Functional options: max size, idle timeout, factory function
- Interface-first
- Lifecycle with `Close()` using `sync.Once`
- Sentinel errors: `ErrClosed`, `ErrPoolExhausted`
- Struct fields: buffered channel, mutex, config values, `sync.Once`, `closed bool`

---

## Exercise 2: Event Bus

An in-memory pub/sub event bus. Callers publish events by topic and subscribe handlers to topics. A slow handler on one topic must not block publishers or handlers on a different topic. `Close()` blocks until all enqueued events finish processing, up to a context deadline.

### Clarifications
- **"Separated publisher and subscriber concerns"** means your interface should have clearly named methods: `Publish(topic string, payload any) error` and `Subscribe(topic string, handler func(payload any)) error`. Both on the same interface is fine.
- **"Slow consumer must not block other topics"** means each topic has its own buffered channel internally. You do not need to implement the dispatch logic — just reflect this in your struct fields.
- **Compile-time verification** looks like this: `var _ EventBus = (*eventBusImpl)(nil)` — place this at the top of the file after the interface declaration.
- `Close(ctx context.Context) error` returns an error if the context expires before all handlers finish.

### Patterns to Hit
- Config struct pattern: `Config{ BufferPerTopic int, MaxSubscribersPerTopic int }`
- Interface with compile-time verification
- Struct fields: `sync.RWMutex`, `map[string][]chan any`, `sync.WaitGroup`
- Lifecycle: `Close(ctx context.Context) error`
- Sentinel errors: `ErrClosed`, `ErrTopicNotFound`, `ErrSubscriberLimitReached`

---

## Exercise 3: Rate Limiter

A sliding-window rate limiter that tracks request volumes on a per-key basis (e.g. IP address or API key) over a configurable window duration. Keys are strings — callers pass their own identifier.

### Clarifications
- **"Per-key state"** means the struct holds a map where each key maps to its own request count and window tracking. You do not need to implement the window logic — just model the map and what each entry contains as a separate struct.
- **"Cleanup ticker"** is a `*time.Ticker` field on the struct. It is used to periodically remove stale keys. You do not need to implement the cleanup goroutine.
- **Minimalist interface** means only one method: `Allow(ctx context.Context, key string) (bool, error)`. Nothing else is exposed.
- Constructor must return an error if `MaxRequests <= 0` or `WindowDuration <= 0`.

### Patterns to Hit
- Config struct: `Config{ WindowDuration time.Duration, MaxRequests int, CleanupInterval time.Duration }`
- Single-method interface strictly enforced
- Internal/exported boundary: caller only sees the interface
- Struct fields: `sync.RWMutex`, `map[string]*keyState`, `*time.Ticker`, `sync.Once`
- Sentinel errors: `ErrClosed`, `ErrRateLimitExceeded`

---

## Exercise 4: Retry Library

A library that executes a given function and retries it on failure according to a configurable policy. Supports max attempts, delay between retries, exponential backoff, and context cancellation.

### Clarifications
- **"Interface for retry policy"** means you define a `RetryPolicy` interface with a method like `ShouldRetry(attempt int, err error) (bool, time.Duration)`. A concrete `ExponentialBackoff` struct implements it. The executor takes a `RetryPolicy` as input.
- **"Custom error type"** means a struct, not a variable: `type RetryError struct { LastErr error; Attempts int }` with an `Error() string` method.
- **No `Close()` needed** — this library has no background goroutines or resources to clean up. The exercise is partly about recognising when lifecycle management is unnecessary.
- Core execution method: `Do(ctx context.Context, fn func() error) error`

### Patterns to Hit
- Functional options: max attempts, initial delay, backoff multiplier, retry condition function
- Separate `RetryPolicy` interface with one concrete implementation
- Custom error type with `Unwrap() error` for `errors.Is` compatibility
- No lifecycle — no `Close()`, no `sync.Once`

---

## Exercise 5: Circuit Breaker

A circuit breaker that wraps calls to an external dependency. Tracks failures and trips to open state after a threshold. Transitions to half-open after a cooldown to probe recovery.

### Clarifications
- **"Unexported enum"** means define a type and constants inside the package: `type circuitState int` with `const ( stateClosed circuitState = iota; stateOpen; stateHalfOpen )`. These are not exported.
- **"Custom error type for `ErrCircuitOpen`"** means a struct: `type CircuitOpenError struct { RetryAfter time.Time }` with an `Error() string` method. Callers can extract `RetryAfter` to know when to try again.
- **Half-open logic** does not need to be implemented — just make sure the state field and cooldown duration exist on the struct.
- `Close()` is trivial here — just a `sync.Once` that sets a `closed` flag. Practice the pattern even when there is nothing meaningful to tear down.

### Patterns to Hit
- Unexported state enum
- Functional options: failure threshold, cooldown duration, success threshold to reclose
- Interface: `Execute(ctx context.Context, fn func() error) error`
- Custom error type: `CircuitOpenError` with `RetryAfter time.Time`
- Struct fields: `circuitState`, failure count, last failure time, `sync.Mutex`, `sync.Once`

---

## Exercise 6: Prioritized Task Queue

A task execution library where callers submit jobs with an explicit priority. Internal worker goroutines pull and execute the highest priority jobs first. Supports both graceful and abrupt shutdown.

### Clarifications
- **"Priority"** is an unexported enum: `type Priority int` with `const ( Low Priority = iota; Medium; High )`. Export these constants — callers need to pass them.
- **"Separate channel per priority"** means the struct holds three channels: `highJobs`, `medJobs`, `lowJobs`. Workers check high first, then medium, then low using a `select` with ordering logic. You do not need to implement the worker logic — just model the fields.
- **`Close(graceful bool)`** — if `true`, close the job channels and let workers drain them before exiting. If `false`, cancel a context to force workers to stop immediately. Model both a `cancel context.CancelFunc` and a `sync.WaitGroup` on the struct to support this.
- **Custom error type** for task execution failure: `type TaskError struct { Cause error; Priority Priority }`.

### Patterns to Hit
- Functional options: worker count, buffer depth per priority level
- Exported priority enum, unexported implementation struct
- `Close(graceful bool)` — two distinct shutdown paths
- Custom error type: `TaskError`
- Struct fields: three buffered channels, `sync.WaitGroup`, `sync.Once`, `context.CancelFunc`
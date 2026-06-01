## Layer 3 — Leading the Design Discussion

**1. Clarifying questions before touching the keyboard**
- Bounded or unbounded?
- Blocking or non-blocking on contention?
- Thread-safe or single-threaded?
- What does the error model look like?
- What are the performance constraints?

**2. Write the interface/API first, implement second**
- Put the interface on the doc, explain it, get buy-in, then go into the struct and methods
- The interviewer is evaluating your API *before* your implementation

**3. Name trade-offs out loud as you make decisions**
- "I'm using `sync.Cond` here rather than a channel because..." 
- "I'm using `RWMutex` because reads will dominate..."
- "I could make this non-blocking and return an error immediately, but I'll go with blocking + context for now because it's more useful to callers — we can discuss"

**4. Scope explicitly and proactively**
- Say what you're building now and what you're deferring
- "I'll implement the core acquire/release cycle first. Health checks and eviction policy I'll mark as TODOs and we can go there if time allows"

**5. Think out loud about thread safety at every step**
- As you write each method, state whether it's safe to call concurrently and why
- If it's not, say so — that's better than silently leaving a race condition

---

Also need to revise previous notes that were made.
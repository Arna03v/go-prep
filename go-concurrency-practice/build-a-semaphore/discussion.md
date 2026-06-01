## The Interview Challenge: Bounded Concurrent Web Crawler

### The Context

You are building a high-performance web crawler in Go. Your crawler needs to fetch pages from thousands of URLs concurrently using goroutines. However, if you fire off thousands of network requests at the exact same time, you will either exhaust local network sockets, get rate-limited (blocked) by the target servers, or run out of memory.

To prevent this, you must build a concurrency throttler—a **Counting Semaphore**—using Go channels.

### The Specifications

Your task is to implement a reusable semaphore structure with the following requirements:

1. **Initialization:** It must accept a capacity parameter $N$ defining the maximum number of concurrent operations allowed.
2. **Acquire:** A method that blocks if $N$ operations are already running, and unblocks once a slot becomes available.
3. **Release:** A method that frees up a slot, allowing other waiting operations to proceed.
4. **Idiomatic Go:** The underlying mechanism must leverage Go channels and their blocking/non-blocking behaviors natively, without using the `sync` package's primitives (like `sync.Mutex` or `sync.Cond`).

---

### Interviewer Questions for You to Answer:

Along with your first iteration of the code, please answer these two design questions:

1. **Buffered vs. Unbuffered:** Will your underlying channel be buffered or unbuffered, and what exactly does the channel's capacity represent?
2. **The Token Model:** In your design, does calling `Acquire` *send* a token into the channel or *receive* a token from it? Why did you choose that direction?

Whenever you are ready, lay down your first iteration of the code and your answers!
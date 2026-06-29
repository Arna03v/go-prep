
# [Concurrency series by Mike shah](https://www.youtube.com/watch?v=Fn0xBsmact4&list=PLvv0ScY6vfd_ocTP2ZLicgqKnvq50OCXM)
## Introduction to concurrency
Multiple tasks can happen at once, the order matters and sometimes have to wait on shared resources. It is different from parallelism (where everything hapens at once)

## Unique pointers
A std::unique_ptr is a smart pointer that owns and manages another object through a pointer, automatically destroying that object when the unique pointer goes out of scope. [1, 2, 3, 4] 
Here is how it fundamentally differs from a raw pointer (*ptr):
### 1. Memory Management

* Raw Pointer (*ptr): Requires manual memory management. You must explicitly call delete to free the allocated memory. Forgetting to do so causes memory leaks. [5, 6, 7] 
* Unique Pointer (unique_ptr): Automates memory management. It uses the RAII (Resource Acquisition Is Initialization) pattern. It safely deletes the memory automatically as soon as the pointer variable goes out of scope. [8, 9, 10, 11, 12] 

### 2. Ownership Rules

* Raw Pointer (*ptr): Has no inherent concept of ownership. Multiple raw pointers can point to the exact same memory address. This can easily lead to dangling pointers if one pointer deletes the memory while others are still using it. [13, 14, 15, 16] 
* Unique Pointer (unique_ptr): Enforces strict, exclusive ownership. Only one unique_ptr can own a resource at any given time. It cannot be copied to another pointer, only moved using std::move(). [17, 18, 19, 20, 21] 

### 3. Quick Comparison Table

| Feature [22, 23, 24, 25, 26] | Raw Pointer (T* ptr) | Unique Pointer (std::unique_ptr<T>) |
|---|---|---|
| Memory Cleanup | Manual (delete ptr) | Automatic (On scope exit) |
| Ownership | Ambiguous / Shared | Exclusive (Strictly one owner) |
| Copyable | Yes | No (Compiler error) |
| Movable | Yes | Yes (Using std::move) |
| Runtime Overhead | Zero | Zero (Matches raw pointer performance) |

### 4. Code Comparison

#include <memory>
void rawPointerExample() {
    int* raw = new int(5);
    // ... if code crashes or returns here, memory leaks!
    delete raw; // Manual cleanup required
}
void uniquePointerExample() {
    std::unique_ptr<int> smart = std::make_unique<int>(5);
    // ... do work
} // 'smart' automatically deletes the integer here. No leak possible.



## Thread based concurrency
- light weight process
- Threads share the same global memory, heap memory, file descriptors
- Threads have different stack memory(thread local variables, data registers, stack pointer and program counter)
- threads switch via context switching
- Use threads for 
    - Heavy computations (related or unrelated)
    - performing different tasks

We have two types of threads
- threads 
- jthreads

```cpp
void test (int x){std::cout <<"hello from thread" << endl; std::cout <<"arguments are: " << x << endl;}

int main(){
    std::thread myThread(&test, 100); // myThread will execute the test function

    // mets make the main thread wait until myThread is dons
    myThread.join(); // blocks the execution of current thread (main) until myThread finishes execution
    std::cout << "Hello from main" << endl;

    // // mets make the main thread wait until myThread is done
    // myThread.join();
    return 0;
}
```

Launching multiple threads
```cpp
void test (int x){
    std::cout <<"hello from thread: " << this_thread::get_id() << endl; 
    std::cout <<"arguments are: " << x << endl;
}

int main(){
    vector<thread> threads;
    for(int i=0; i<10; i++){
        threads.push_back(thread(&test, i));
    }
    for(auto& t : threads) t.join();

    std::cout << "Hello from main" << endl;

    // // mets make the main thread wait until myThread is done
    // myThread.join();
    return 0;
}
```

we need to take care to join the threads. We can use `jthreads` instead. automatically joins all unjoined threads at the end of main's execution
```cpp

#include <iostream>
#include <thread>
#include <vector>
using namespace std;

void test (int x){
    std::cout <<"hello from thread: " << this_thread::get_id() << endl; 
    std::cout <<"arguments are: " << x << endl;
}

int main(){
    vector<jthread> jthreads;
    for(int i=0; i<10; i++){
        jthreads.emplace_back(&test, i);
    }
    // for(auto& t : threads) t.join();

    std::cout << "Hello from main" << endl;

    // // mets make the main thread wait until myThread is done
    // myThread.join();
    return 0;
}
```

If we dont want the threads to depend on main (say completing some background operation). we can use thread.detach()

```cpp
void runLogging() {
   // some background event
}

int main() {
    std::thread t(runLogging);

    t.detach(); // t runs independently now. Main does NOT wait.
    std::cout << "Main thread: Moving on immediately.\n";

    // Small delay to allow the detached thread to print before main exits
    // std::this_thread::sleep_for(std::chrono::seconds(2));  // if this is commented and the main finishes; then runLogging does not print anything
    return 0;
}

```

## mutexes and preventing data races

updating a shared value can cause the data to be overwritten. This is because writing can be broken into 3 atomic operations 
- read
- update
- write

to prevent execution from being switched out at any point, we use mutexes. Sections within the mutex locks and unlocks becomes atomic. 
We can also use binary semaphores instead of mutexes! (semaphores later)
```cpp
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
using namespace std;

mutex shared_value_lock;
static int shared_value = 0; // cant be read outside this cpp file
// int shated_value = 0; can be read outside this file by using `extern` keyword

// may cause issues, while one thread is reading the valye another thread might update it
// read x -> x++ -> write x; execution can be switched out at any point!
// to stop this -> we use a mutex (mutual exclusion) on this shared_value
void shared_value_increment(){
    shared_value_lock.lock(); // now all threads will wait until they get to lock
        shared_value = shared_value + 1;    
    shared_value_lock.unlock();
}

void test (int x){
    std::cout <<"hello from thread: " << this_thread::get_id() << endl; 
    std::cout <<"arguments are: " << x << endl;
}

int main(){
    vector<thread> threads;
    for(int i=0; i<100; i++){
        threads.push_back(thread(&shared_value_increment));
    }
    for(auto& t : threads) t.join();

    std::cout << "shared value is: " << shared_value << endl;

    // // mets make the main thread wait until myThread is done
    // myThread.join();
    return 0;
}
```
## Using lock_guards/scoped_lock/unique_lock to prevent deadlocks
### lock_guards
we can forget to unlock, causing deadlocks. Here are some examples

```cpp
void unsafe_shared_value_increment(){
    shared_value_lock.lock(); // now all threads will wait until they get to lock
        shared_value = shared_value + 1;    
    // shared_value_lock.unlock(); mutex is not being unlocked -> nothing else can lock the mutex -> nothing else can proceed with updating the value
}

void unsafe_shared_value_increment(){
    shared_value_lock.lock(); // now all threads will wait until they get to lock
        try{
            shared_value = shared_value + 1; 
            throw "dangerous ... abort";
        } catch (...){
            cout << "exception occured" << endl;
        }
   
    shared_value_lock.unlock(); // this is skipped when the exception is thrown
}
```
ofcourse we can manually make sure that every exit has the unlock. We can also use a `lock_guard` which is a mutex wrapper. Once its scope is over, the object it is holding (the mutex) is unlocked

```cpp

void safe_shared_value_increment_mutex(){  
    lock_guard<mutex> lock(shared_value_lock); // now all threads will wait until they get to lock
        
        try{
            shared_value = shared_value + 1; 
            throw "dangerous ... abort";
        } catch (...){
            cout << "exception occured" << endl;
        }
}

void safe_shared_value_increment_mutex_2(){
    lock_guard<mutex> lock(shared_value_lock);
    shared_value = shared_value + 1;    

}
```

### unique_lock

`lock_guard` locks on construction and unlocks on scope exit — that's all it does. `unique_lock` does the same RAII unlock, but adds *control*: you can unlock/relock mid-scope, construct it without locking yet (`defer_lock`), or time out. The cost is it's slightly heavier (it tracks an ownership flag). Use `lock_guard` by default; reach for `unique_lock` only when you need that flexibility — most importantly, `condition_variable::wait` *requires* it, because the cv has to unlock while sleeping and relock on wake.

```cpp
void unique_lock_basic(){
    unique_lock<mutex> lock(shared_value_lock); // locks now, unlocks at scope exit (like lock_guard)
    shared_value = shared_value + 1;
}

void unique_lock_manual_release(){
    unique_lock<mutex> lock(shared_value_lock);
        shared_value = shared_value + 1;
    lock.unlock();              // release early — do the expensive non-shared work outside the lock

    do_slow_unrelated_work();   // other threads can touch shared_value while we're here
    // no double-unlock at scope exit: unique_lock tracks that it no longer owns the mutex
}

void unique_lock_deferred(){
    unique_lock<mutex> lock(shared_value_lock, defer_lock); // construct WITHOUT locking
    // ... set things up that don't need the lock ...
    lock.lock();                // lock only when we actually reach the critical section
    shared_value = shared_value + 1;
}

// the reason it exists: condition_variable needs to unlock-while-waiting, relock-on-wake
void unique_lock_with_cv(){
    unique_lock<mutex> lock(shared_value_lock);
    cv.wait(lock, []{ return ready; }); // wait() unlocks `lock`, sleeps, relocks on wake
    shared_value = shared_value + 1;
}
```

### scoped_lock

`scoped_lock` (C++17) is `lock_guard` for *multiple* mutexes at once, and it acquires them with built-in deadlock avoidance. This is the fix for the AB–BA deadlock: two threads grabbing two locks in opposite order. `scoped_lock` locks all of them atomically (internally a try-and-back-off algorithm), so order doesn't matter.

```cpp
mutex mutex_a, mutex_b;

// DANGER: classic AB-BA deadlock
void transfer_bad(){
    lock_guard<mutex> la(mutex_a);   // thread 1 grabs A, waits for B
    lock_guard<mutex> lb(mutex_b);   // thread 2 grabbed B first, waits for A -> both stuck
    // ...
}

// SAFE: scoped_lock takes both at once, no consistent ordering needed
void transfer_safe(){
    scoped_lock lock(mutex_a, mutex_b); // both acquired deadlock-free (CTAD: no <...> needed)
    // ... touch state guarded by A and B ...
}                                       // both released at scope exit
```
### shared_mutex + shared_lock

A regular `mutex` is exclusive: one holder, always. A `shared_mutex` has two modes — *shared* (read) and *exclusive* (write). Many threads can hold it in shared mode at once, but an exclusive lock waits for all readers to leave and blocks everyone while held. Use it only when reads outnumber writes *and* the reads are truly read-only. The lock type you use picks the mode: `shared_lock` = read, `unique_lock`/`lock_guard` = write.

```cpp
shared_mutex config_mutex;
string shared_config = "initial";

// READERS: shared_lock -> many can run concurrently
string read_config(){
    shared_lock<shared_mutex> lock(config_mutex); // shared/read mode
    return shared_config;                          // pure read, no mutation
}

// WRITER: unique_lock (or lock_guard) -> exclusive, blocks all readers + writers
void write_config(const string& value){
    unique_lock<shared_mutex> lock(config_mutex);  // exclusive/write mode
    shared_config = value;
}
```

The trap — and the thing to say out loud in an interview — is that `shared_lock` is only correct if the operation *doesn't mutate state*. The classic mistake is an LRU cache: `Get` looks read-only, but it moves the touched node to the front, so it's a write in disguise. Guarding it with `shared_lock` lets two "readers" reorder the list concurrently → data race.

```cpp
// WRONG: Get mutates LRU order, so this is not a read
string lru_get_bad(const Key& k){
    shared_lock<shared_mutex> lock(lru_mutex); // two threads here both reorder -> race
    move_to_front(k);                          // <-- mutation under a *shared* lock
    return value_of(k);
}

// RIGHT: it's a write. Plain mutex is simpler and correct.
string lru_get_ok(const Key& k){
    lock_guard<mutex> lock(lru_mutex);         // exclusive; shared_mutex buys you nothing here
    move_to_front(k);
    return value_of(k);
}
```

## std::atomic
Updating shared values in safe ways without using locks. Especially when we wnt nly one/two values to be updated

Generally an update is actually 3 operations. x++ is beoken into
- read x into a register
- increment the value in the register
- write the value back into the memory location

where execution can be switched after any step. using `atomic` makes sure all 3 happen together without any interleaving
```cpp
static atomic<int> shared_value_2 = 0;

void increment_atomic_shared_value(){
    // shared_value_2 = shared_value_2 + 1; this is not atomic
    // shared_value_2++; this is atomic
    shared_value_2.fetch_add(2); // this is atomic (adding 2)
}
```
- also exists for bool, char (basically all primitive types)
- can work with user created types as well if the type is collection of bits

## compare_exchange (CAS)

`compare_exchange` is the atomic read-modify-write that all lock-free code is built on. `cas(expected, desired)` does, atomically: "if my current value equals `expected`, set me to `desired` and return true; otherwise load my actual current value back into `expected` and return false." That last part is the trick — on failure it hands you the fresh value, so you can retry without a separate reload.

```cpp
atomic<int> value{0};

// lock-free increment: the canonical CAS-retry loop
void cas_increment(){
    int expected = value.load();                              // snapshot
    while(!value.compare_exchange_weak(expected, expected + 1)){
        // on failure, `expected` was auto-updated to the current value -> just retry
        // someone else incremented between our load and our CAS
    }
}
```

### weak vs strong

`strong` returns false only when the value genuinely didn't match. `weak` is allowed to return false *spuriously* — fail even when the value DID match — because on some CPUs (ARM/Power use load-linked/store-conditional) the store can fail for reasons unrelated to the value, like a context switch touching the cache line. So:

- **`weak`** → use when you're already in a retry loop (the common case). A spurious failure just costs one more loop iteration, and `weak` is cheaper there because it skips the logic to mask those failures.
- **`strong`** → use for a one-shot check where looping would be awkward, or where each retry is expensive (you don't want a *spurious* failure forcing a costly recompute).

```cpp
// weak: fine, we loop anyway -> spurious failure just retries
while(!value.compare_exchange_weak(expected, desired)){ /* retry */ }

// strong: single attempt, no loop -> a spurious failure would be a real bug here
if(flag.compare_exchange_strong(expected, desired)){
    // took the slot exactly once
}
```

### the ABA problem

CAS only compares the **value**, not whether it changed. If a value goes A → B → A between your read and your CAS, the CAS still succeeds — but the world may have shifted underneath you. Classic case: a lock-free stack where a popped node is freed and a *new* node is later allocated at the same address.

```cpp
// thread 1 wants to pop: reads head -> node A, plans to set head = A->next
Node* expected = head.load();          // (1) head == A, A->next == B
Node* next     = expected->next;       // (2) plans head = B

// thread 2 runs in between: pop A, pop B, then push a NEW node reusing A's address
//   head: A -> B -> C   becomes   A(new) -> C     (A's old memory got reused)

// thread 1 resumes:
head.compare_exchange_weak(expected, next); // (3) head still "== A" by pointer value -> SUCCEEDS
// but `next` (B) is now freed/garbage. head now points into reclaimed memory. corruption.
```

The value matched (`A == A`) so CAS is happy, but the linked structure is no longer what thread 1 assumed. Standard fixes: **tagged pointers** (pack a version counter next to the pointer so `A#1 != A#3`), **hazard pointers**, or **epoch-based reclamation** — all ways of making "it's still A" actually mean "nothing changed."

## std::call_once / std::once_flag

Runs an initializer exactly once across all threads, even if many threads hit it simultaneously. Everyone else blocks until the winner finishes, then they all proceed seeing the completed state. It's the C++ spelling of `sync.Once` — same problem (lazy one-time init), same internals (fast atomic check, slow locked init).

```cpp
once_flag init_flag;
Connection* conn = nullptr;

void ensure_initialized(){
    call_once(init_flag, []{
        conn = open_connection(); // runs in exactly ONE thread, others wait here
    });
    // past this point, every thread sees conn fully constructed
}
```

That's the whole API. It also handles the case your `defer atomic.StoreUint32` was guarding: if the initializer **throws**, the flag is NOT consumed — the next thread retries, so you never get stuck with a "done" flag over half-built state.

### why naive double-checked locking is broken

The tempting hand-rolled version — check a bool, lock, check again, init — looks right but has a data race:

```cpp
// BROKEN: do not write this
bool ready = false;            // plain bool
Connection* conn = nullptr;

void init_bad(){
    if(!ready){                // (1) read ready  -- UNSYNCHRONIZED
        lock_guard<mutex> lk(m);
        if(!ready){            // (2) double-check under lock
            conn = open_connection(); // (3) construct
            ready = true;             // (4) publish
        }
    }
}
```

Two independent failures:

1. **Data race on `ready`.** Line (1) reads `ready` with no synchronization while another thread writes it at (4). In C++ that's undefined behavior, full stop — the compiler is allowed to assume it can't happen and optimize accordingly.

2. **Reordering between (3) and (4).** The compiler/CPU can make the write `ready = true` *visible before* the write to `conn`. So a second thread sees `ready == true` on the fast path, skips the lock, and uses `conn` — which is still null/half-constructed. The store of the flag raced ahead of the data it was supposed to guard.

This is exactly why your Go version used `atomic.LoadUint32`/`atomic.StoreUint32` instead of a plain `uint32`, and set `done` only *after* `f()` — the atomics provide the acquire/release ordering that forces "conn is fully built" to be visible before "ready is true." The hand-rolled C++ above drops that ordering, so it breaks.

## condition variables
generally threads which are not in the critical section, they are in a `spin lock`; spinning and checking the lock again and again, wasting CPU cycles. Or we can put the thread to sleep. Pick a thread of the queue when the current thread in the critical section finishes execution
- Need multiple threads
- a shared variable (generally a boolean)
- a lock that is also shared (unique lock fits perfectly)
- a condition variable

Thread in the queue keeps sleeping until the current thread notifies. 
- while waiting for the condition the lock is released
- after waking up the lock is reacquired
```cpp
mutex gLock;
condition_variable gConditionVariable;



int main(){
    int result = 0;
    bool notify = false; // communictes between threads if the current thread has finished the work

    // reporting thread (sleeping thread)
    thread reporter([&]{
        unique_lock<mutex> lock(gLock);

        while(!notify){
            gConditionVariable.wait(lock);
        }

        cout << "[reporter] result is : " << result << endl;
    });
    // working thread
    thread worker([&]{
        unique_lock<mutex> lock(gLock);

        // do the work
        result += 1;

        // work is done
        // notify others
        notify = true;

        this_thread::sleep_for(chrono::seconds(5));
        cout << "worker thread complete" << endl;

        // we want to notify one of the threads (that is next in the queue)
        gConditionVariable.notify_one();
    });

    return 0;
}
```
Protecting shared data → mutex (or spinlock). Always. This is non-negotiable whenever two threads touch the same state.
Waiting for another thread to make a condition true (queue non-empty, work available, shutdown requested) → condition variable, on top of that mutex. Don't spin-poll.
- `notify_one` (one waiter, e.g. one queue slot freed) vs `notify_all` (state change all waiters must re-evaluate, e.g. shutdown).

## std::async
- without synchronisation
```cpp
#include <future>

int sq(int x) return x*x;

int main(){
    future<int> asyncFunction = async(&sq, 10); // same way to launch as  threads

    for(int i=0; i<10; i++){
        sq(i);
    }

    // result of the operation
    // we are blocked at the get() opration until the result is computed
    int result = asyncFunction.get();
    cout << "result is : " << result << endl;

    return;
}
```
We can start the computation eagerly on a separate thread, then keep working in the main thread while it runs concurrently. We only block when we actually need the result (`get()`). So the work and our main-thread work overlap — making use of multiple cores — instead of running one after the other.

## mutexes vs semaphores
- mutexes need to be locked and unlocked from the same thread
- semaphores have no such restrictions (any thread can acquire and release)
    - have a fixed number of threads that can acquire the resource at any point of time
    - binary semaphores are basically mutexes

a mutex protects (you lock the data, do the thing, unlock — same thread does both), a counting semaphore counts (you acquire a unit of a resource and release it, possibly from a different thread)

C++17 gives you std::counting_semaphore<Max>; binary_semaphore is just counting_semaphore<1>.
### counting_semaphore — limit N concurrent users of a resource

```cpp
#include <semaphore>

counting_semaphore<10> db_slots{10}; // 10 tokens: at most 10 concurrent DB connections
// <10> is the initial value

void query_db(){
    db_slots.acquire();      // take a token; BLOCKS if all 10 are out
    do_query();              // at most 10 threads are ever in here at once
    db_slots.release();      // hand the token back -> wakes a waiter
}
```

This is your buffered-channel-of-size-N semaphore, ported. `acquire()` = receive a token, blocks when empty; `release()` = send one back. The count *is* "how many slots are free right now."

### the key asymmetry vs a mutex: release can cross threads

```cpp
binary_semaphore signal{0}; // start at 0 = "nothing ready yet"

void producer(){
    prepare_data();
    signal.release();        // thread A signals "go"
}

void consumer(){
    signal.acquire();        // thread B waits here until A releases
    use_data();
}
```

A mutex **cannot** do this — `lock()` and `unlock()` must be the same thread; unlocking a mutex you don't own is undefined behavior. A semaphore has no notion of ownership, so one thread acquiring and a *different* thread releasing is the normal, intended use. That's the whole reason it's a distinct primitive.

## futures and promises

**Go → C++ mapping:**

Think of `promise<T>` + `future<T>` as a **one-shot, one-element channel**.

- `promise<T>` is the **write end**. The worker calls `promise.set_value(result)` exactly once.
- `future<T>` is the **read end**. The caller calls `future.get()`, which blocks until the value is set.
- You link them: `promise.get_future()` gives you the paired future.

**The flow:**

```cpp
// submit side:
promise<Result> p;
future<Result> f = p.get_future();  // hand this back to caller
// enqueue {task, std::move(p)} onto the queue

// worker side:
// dequeue {task, promise}
Result r = /* compute */;
p.set_value(r);  // caller's future.get() now unblocks
```

**Key detail:** `promise` is move-only (like a channel you hand off). You can't copy it — you `std::move` it into the queue entry.

# Types of bugs/patterns to identify
## Data race
Starting with #1. I'll keep to your format and stop after.

## 1. Data race

**Theory.** A data race is two or more threads accessing the *same* memory location concurrently, with at least one access being a write, and no happens-before relationship (lock, atomic, fork/join) ordering them. The mechanism: operations that *look* atomic in source aren't — `count++` is a load, an add, and a store, and another thread can slip between them. In C++ a data race is undefined behavior, not merely a wrong number: the compiler is allowed to assume races never happen, so it may cache the value in a register, reorder, or tear a multi-word write. So the failure isn't just "lost update," it's "anything."

**Broken — lost update on a counter:**

```cpp
int count = 0;                      // plain int, shared, no synchronization

void worker() {
    for (int i = 0; i < 100000; ++i)
        count++;                    // RACE: this is load r=count; r=r+1; store count=r
}

// Interleaving that loses an increment:
//   count == 5
//   thread A: load r=5
//   thread B: load r=5          <- B preempts after A's load, before A's store
//   thread A: r=6; store count=6
//   thread B: r=6; store count=6  <- B's store clobbers A's; one increment vanished
// Two ++ ran, count went up by 1. With two threads x100000 you reliably end up < 200000.
```

**Fixed — two valid options, pick by contention shape:**

```cpp
std::atomic<int> count{0};          // option A: atomic RMW
void worker() {
    for (int i = 0; i < 100000; ++i)
        count.fetch_add(1, std::memory_order_relaxed);  // single indivisible RMW; relaxed
}                                   // is fine here — we only need atomicity of the counter,
                                    // not ordering against other data
```

```cpp
int count = 0;                      // option B: mutex
std::mutex m;
void worker() {
    for (int i = 0; i < 100000; ++i) {
        std::lock_guard<std::mutex> g(m);  // establishes happens-before between iterations
        count++;                            // now a protected, well-ordered RMW
    }
}                                   // correct but heavier; per-iteration lock is overkill
                                    // for a pure counter — atomic wins here. Mutex earns
                                    // its place when count++ is part of a larger invariant.
```

The map case is nastier than a counter, because the corruption is structural, not just a stale value:

```cpp
std::unordered_map<int,int> cache;  // shared, no lock
void put(int k, int v) {
    cache[k] = v;                   // RACE: operator[] may insert a node and, on load-factor
}                                   // breach, rehash the whole table. Two concurrent inserts
                                    // can interleave mid-rehash -> corrupted buckets, lost
                                    // nodes, or a crash. NOT just a lost write.
// Fix: a std::mutex around every access (reads included), or a concurrent map.
// std::atomic does NOT help here — you can't atomic<> a whole container.
```

**Where it shows up / the tells.** Anywhere shared mutable state is touched without a guard: hit/miss counters and metrics on a cache, a `size_` field on a queue updated outside the lock, a `bool ready_` flag poked from one thread and polled from another, free-list head pointers in a buffer pool. The textual tells when scanning: a plain (non-`atomic`, non-`const`) variable or container that's a class member or global, mutated with `++`, `+=`, `=`, `.insert`, `.push_back`, `operator[]`, while clearly reachable from more than one thread — and no lock in sight at the access site. A classic giveaway is a "stats" or "counter" field that someone left unguarded because it "doesn't matter if it's a little off" — it's still UB. Another: a flag written under a lock in one place but read *without* the lock elsewhere (the read side races even though the write side looks safe).

One distinction worth keeping sharp for the interview: making the counter `atomic` fixes the *counter*, but if that counter is supposed to stay consistent with *other* state (e.g. `size_` must match the actual number of nodes in the list), then atomic-per-field isn't enough — you need a lock spanning the whole invariant. That's the bridge to pattern #2.

## Under-scoped critical section
## 2. Under-scoped critical section

**Theory.** The lock is held, but not over the *whole* span that needs to be atomic. Two flavors: 
(a) you release the lock too early, so an action that depends on state checked under the lock runs after the lock is gone; action which depends on the state must be inside the critical section
    - the entire check and act needs to be the critical section
(b) a logical operation is split across *two separate* lock acquisitions, and another thread mutates state in the gap between them. 

The mechanism is the same as a data race in spirit, but the individual accesses *are* locked — the bug is that the *critical section boundary* is drawn in the wrong place. The invariant spans more than the lock does.

**Broken — lock released before the action it guards:**

```cpp
std::mutex m;
std::queue<Task> q;

Task pop() {
    Task t;
    {
        std::lock_guard<std::mutex> g(m);
        if (q.empty()) return {};      // checked under lock — good so far
    }                                  // BUG: lock released HERE, at end of scope
    t = q.front();                     // RACE: another thread popped between the
    q.pop();                           // empty-check and now; q.front() on empty
    return t;                          // queue is UB. The check and the act that
}                                      // relies on it are in different critical sections.
// Interleaving:
//   q has 1 element.
//   thread A: locks, sees !empty, UNLOCKS
//   thread B: locks, sees !empty, front()+pop()  -> q now empty, B unlocks
//   thread A: front() on empty queue -> UB / crash
```

**Fixed — one critical section covering check *and* act:**

```cpp
Task pop() {
    std::lock_guard<std::mutex> g(m);  // lock spans the whole decision+mutation
    if (q.empty()) return {};
    Task t = q.front();
    q.pop();
    return t;
}                                      // check and act are now indivisible
```

The split-acquisition flavor is sneakier because every individual line *is* locked:

```cpp
std::mutex m;
std::unordered_map<int, Account> accts;

void transfer(int from, int to, int amt) {
    int bal;
    {
        std::lock_guard<std::mutex> g(m);
        bal = accts[from].balance;     // acquisition #1: read balance under lock
    }                                  // lock dropped
    if (bal < amt) return;             // decision made on a now-STALE snapshot
    {
        std::lock_guard<std::mutex> g(m);   // acquisition #2: separate lock
        accts[from].balance -= amt;    // BUG: between #1 and #2 another transfer
        accts[to].balance   += amt;    // drained `from`. We re-read nothing; we just
    }                                  // subtract. Balance can go negative -> overdraft.
}
// someone can change the balance after the scope 1 but before the scope 2

// The check (bal < amt) and the act (-= amt) are guarded individually but the
// invariant "don't spend money you no longer have" spans both. Two locks, one
// broken invariant.
// Fix: single lock_guard around read-check-write, OR re-check the balance inside
// the second critical section before mutating.
```

**Where it shows up / the tells.** Buffer pools and free lists where you check "is a buffer available?" under the lock, release it, then go grab one. Caches with check-then-fill that we'll formalize as TOCTOU in #3 (that's the read-side cousin of this). Connection pools: "count < max" checked, lock dropped, then a slot claimed. Reference-counted handles where you read the refcount, decide to free, but the decrement-and-decide isn't one critical section. Anywhere a `size_`/`count_`/`available_` is consulted to make a branch decision and the branch body mutates the same structure.

The textual tells: a `{ ... }` scope block that closes *right after* a check (`if (q.empty())`, `if (count_ < max)`, `if (it != map.end())`) while the code that *uses* that result sits outside the block. Two separate `lock_guard`/`unique_lock` scopes inside one function operating on the same shared data — the gap between them is the bug. A local variable that caches shared state (`bal`, `n`, `head`) read under one lock and acted on under another, or none. Also watch for an early `return`/`break`/`continue` *inside* a lock scope that's fine, versus a lock scope that ends and then more shared-state work continues — the latter is the smell.

The fix discipline for the interview: state the invariant out loud ("the queue must be non-empty from the check through the pop"), then make the critical section exactly cover that invariant — no shorter (this bug), no longer (that's #13, over-serialization). 

If holding the lock across the whole span is too expensive because part of it is slow, the answer isn't to shrink the lock and race — it's to re-validate inside the second critical section, or restructure so the slow part works on a private copy.

How do we make this provate copy/restructure the slow part?

"Private copy" just means an object no other thread holds a reference to — so other threads physically can't touch it, and you need no lock to work on it. There are two genuinely different things people mean by this, and it's worth keeping them apart because they solve different problems.

**1. A stack-local snapshot (the one relevant to the under-scoped-CS fix).** You copy the shared state into a local under the lock, release the lock, do the slow work on the local, then reacquire to commit. The local is private by construction — it lives on this thread's stack, no other thread has a pointer to it.

```cpp
std::mutex m;
std::vector<Row> table;            // shared

void rebuild_and_install() {
    std::vector<Row> snapshot;
    {
        std::lock_guard<std::mutex> g(m);
        snapshot = table;          // COPY out under lock -> snapshot is now private
    }                              // lock released; other threads keep using `table`

    expensive_transform(snapshot); // slow work on the PRIVATE copy, no lock held,
                                   // no contention, no UB — nobody else can see snapshot

    {
        std::lock_guard<std::mutex> g(m);
        table = std::move(snapshot);  // commit: install the result atomically under lock
    }
}
```

The catch, and the thing an interviewer will push on: between releasing the lock and reacquiring, the world may have changed. If correctness depends on `table` *not* having changed underneath you, the commit step must **re-validate**, not blindly overwrite — otherwise you've recreated the split-acquisition bug from #2, just with a slow middle. This is the same shape as a CAS retry loop:

```cpp
size_t version; // needs to be to be a gloal variable shared between threads
void apply_delta() {
    for (;;) {
        std::vector<Row> snapshot;
       
        {
            std::lock_guard<std::mutex> g(m);
            snapshot = table;
            version  = table_version;   // capture a version stamp under lock
        }

        auto result = slow_compute(snapshot);   // private work

        {
            std::lock_guard<std::mutex> g(m);
            if (table_version != version)        // RE-VALIDATE: did anyone change it?
                continue;                        // someone did -> our result is stale, retry
            table = std::move(result);
            ++table_version;                     // publish a new version
            return;
        }
    }
}
```

That's the disciplined answer: copy out, work private, re-check the version (or the specific precondition) on commit, retry if it moved. The version counter is what lets the commit detect that its snapshot went stale — without it you're just hoping nobody wrote in the gap.

**2. `thread_local` — genuinely per-thread storage, a different tool.** This is for when each thread should have its *own persistent* instance of something, never shared at all — not a temporary snapshot of shared state. Think per-thread scratch buffers, per-thread arenas, per-thread RNG, per-thread accumulators that you fold together at the end.

```cpp
thread_local std::vector<char> scratch;   // each thread gets its own, lives for the
                                          // thread's lifetime, zero synchronization needed

void process(const Request& r) {
    scratch.clear();                      // my scratch — no other thread can see it
    scratch.resize(r.size);
    // ... use scratch freely, no lock, ever ...
}
```

The trap with `thread_local`: it's per-thread, so on a thread *pool* the same buffer is reused across unrelated tasks that happen to land on the same worker — fine if you reset it at the top (the `clear()` above), a latent bug if you assume it's fresh. And folding per-thread accumulators into a global total at the end still needs synchronization at *that* step.

So: snapshot-local when you're escaping a lock to do slow work and then committing back (your #2 case); `thread_local` when the data is legitimately per-thread for its whole life and never needs to merge. The first is about *narrowing* contention, the second about *eliminating* sharing. Conflating them in an interview — e.g. reaching for `thread_local` to fix a shared-counter race — is a tell that the model isn't clear, so it's worth being able to say which one you mean and why.

## 3. TOCTOU / check-then-act

**Theory.** Time-of-check to time-of-use: you test a condition, then act on the assumption it still holds — but between the check and the act, another thread invalidates it. It's the read-side cousin of #2's under-scoped critical section: there the gap let a *write* slip in; here the canonical shape is two threads both *passing the same check* and both proceeding to act, each thinking it's the only one. The mechanism is that the check and the act are separately atomic (or the check isn't even locked) but not atomic *together*, so the truth the check established has an expiry the act ignores.

**Broken — the classic insert-if-absent gap:**

```cpp
std::unordered_map<int, Conn*> pool;     // shared
std::mutex m;

Conn* get_or_create(int k) {
    {
        std::lock_guard<std::mutex> g(m);
        if (pool.count(k)) return pool[k];   // CHECK: not present? fall through
    }                                        // lock dropped — the gap lives here
    Conn* c = new Conn(k);                   // expensive, done outside lock (on purpose)
    {
        std::lock_guard<std::mutex> g(m);
        pool[k] = c;                         // ACT: blindly insert
        return c;
    }
}
// Interleaving — two threads, same key k, neither present:
//   thread A: locks, count(k)==0, UNLOCKS
//   thread B: locks, count(k)==0, UNLOCKS   <- both passed the check
//   thread A: new Conn(k)  -> cA
//   thread B: new Conn(k)  -> cB            <- two objects created for one key
//   thread A: pool[k] = cA
//   thread B: pool[k] = cB                  <- B clobbers A's entry
//   A returned cA, but the map now holds cB. cA is leaked, and any thread that
//   got cA is using a Conn that isn't the canonical one in the pool. Double-create
//   + leak + split-brain on which Conn is "the" one for k.
```

The naive "fix" — just hold the lock the whole time — *is* correct but serializes the expensive `new Conn(k)` under the lock, blocking every other key's lookups while one connection is being built. That tension (correctness vs. not holding the lock across slow work) is exactly what the interviewer wants you to navigate. The disciplined answer is **re-check after the expensive work, under the lock, and discard your loser:**

```cpp
Conn* get_or_create(int k) {
    {
        std::lock_guard<std::mutex> g(m);
        auto it = pool.find(k);
        if (it != pool.end()) return it->second;   // fast path: already there
    }

    Conn* c = new Conn(k);                          // expensive work OUTSIDE the lock

    {
        std::lock_guard<std::mutex> g(m);
        auto [it, inserted] = pool.emplace(k, c);   // RE-CHECK via emplace:
        if (!inserted) {                            // someone beat us to it
            delete c;                               // discard our redundant object
            return it->second;                      // return the canonical winner
        }
        return c;                                   // we won the race; ours is canonical
    }
}
// emplace is the re-check and the act fused into one atomic step: it inserts only
// if absent and tells us (inserted) which happened. The post-work window can still
// have a double-CREATE (two threads build a Conn), but never a double-INSTALL — the
// map stays single-source-of-truth and the loser is cleaned up. We traded a tiny bit
// of wasted work for not holding the lock across construction.
```

Note the key move: `emplace`/`try_emplace` returns `{iterator, bool}`, so the "is it absent?" check and the insert are a *single* locked operation — you never have a gap between them. Compare the broken version's `if (count(k)) ... ` then later `pool[k] = c` as two separate steps. Whenever you see check and act as two statements, ask whether one atomic primitive does both.

**Where it shows up / the tells.** Lazy caches and memo tables (`if (!cache.count(k)) cache[k] = compute(k);`). Connection/thread pools deciding whether to spin up a new resource. Singletons and lazy init done by hand (the *right* tool there is `std::call_once` or a function-local static — see the bridge to #9, double-checked locking). File or buffer allocation: "does this page exist? no? create it." Reference-count transitions: "refcount == 0? then free" — two threads both read 0 and both free (double-free). Set-membership guards: "not in the in-flight set? add it and start the work" — two threads both start.

function local static comes later in `9.`but we can also do it like so
```cpp
Singleton& instance() {
    static Singleton s;     // initialized exactly once, on first call; the compiler
    return s;               // emits a thread-safe guard around the init (C++11+,
}                           // "magic statics" / [stmt.dcl]). Concurrent first-callers
                            // block until init finishes; later calls are a plain load.
```

The textual tells: `if (!container.count(k))` or `if (map.find(k) == map.end())` followed — anywhere, same critical section or not — by an insert/assignment to that same key. `if (x == nullptr) x = new ...`. Any `count()`/`contains()`/`find()` whose boolean result is consumed by a *later* statement rather than fused into the mutating call. A check and its dependent mutation sitting in two different `lock_guard` scopes. And the specific smell of "expensive construction deliberately moved outside the lock" with a plain assignment on the way back in — that's almost always missing the re-check.

The interview framing to say out loud: "check-then-act is only safe if the check and the act are one atomic step; if I must do slow work between them, I do it on a throwaway and re-validate at commit, keeping the map the single source of truth and discarding the loser." That sentence covers `emplace`-style fusion *and* the snapshot-retry pattern from #2 in one breath.

If we need to decide between one check and act vs splitting in the mddle and revalidating before acting; the tradeoff is between
- slow execution but no wasted work
- wasted work but the threads don't block/spin lock

## 4. AB-BA deadlock
## 4. AB-BA deadlock

**Theory.** Two threads each need two locks, and they acquire them in *opposite* orders. Thread 1 holds A and waits for B; thread 2 holds B and waits for A. Neither will release what it holds until it gets the other, so both block forever — a cycle in the wait-for graph. The mechanism is purely the *ordering* of acquisition: nothing is wrong with either thread in isolation, the deadlock only exists because their orders disagree and the two acquisitions are interleaved at exactly the wrong moment.

**Broken — transfer between two locked accounts:**

```cpp
struct Account { std::mutex m; long balance; };

void transfer(Account& from, Account& to, long amt) {
    std::lock_guard<std::mutex> g1(from.m);   // lock #1: source
    std::lock_guard<std::mutex> g2(to.m);     // lock #2: destination
    from.balance -= amt;
    to.balance   += amt;
}
// Deadlock interleaving — two transfers in opposite directions:
//   thread A: transfer(X, Y)  -> locks X.m            (holds X, wants Y)
//   thread B: transfer(Y, X)  -> locks Y.m            (holds Y, wants X)
//   thread A: tries to lock Y.m -> blocks (B holds it)
//   thread B: tries to lock X.m -> blocks (A holds it)
//   cycle: A waits for B waits for A. Forever.
// The order depends on the *arguments*, so the two calls disagree on lock order.
```

**Fix 1 — `std::scoped_lock` (C++17), the idiomatic answer:**

```cpp
void transfer(Account& from, Account& to, long amt) {
    std::scoped_lock lk(from.m, to.m);   // locks BOTH atomically via a deadlock-
    from.balance -= amt;                 // avoidance algorithm (lock/try_lock with
    to.balance   += amt;                 // backoff). Argument order no longer matters:
}                                        // it will never hold one and block on the other.
// scoped_lock with 2+ mutexes is the drop-in cure for AB-BA. Pre-C++17 the
// equivalent is std::lock(a, b) followed by adopting both with lock_guard:
//   std::lock(from.m, to.m);
//   std::lock_guard g1(from.m, std::adopt_lock);
//   std::lock_guard g2(to.m,   std::adopt_lock);
```

**Fix 2 — consistent global order (works when you can't lock both at once):**

```cpp
void transfer(Account& from, Account& to, long amt) {
    // Impose a total order on the locks — here by address — so EVERY thread
    // acquires them in the same sequence regardless of which is from/to.
    Account* first  = &from;
    Account* second = &to;
    if (std::less<Account*>{}(second, first)) std::swap(first, second);  // order by address

    std::lock_guard<std::mutex> g1(first->m);   // always lower address first
    std::lock_guard<std::mutex> g2(second->m);  // always higher address second
    from.balance -= amt;
    to.balance   += amt;
}
// Now transfer(X,Y) and transfer(Y,X) both lock min(X,Y) then max(X,Y).
// No opposite ordering can exist -> no cycle. Use std::less for pointer
// comparison; raw < on unrelated pointers is technically unspecified.
```

`scoped_lock` is the answer to reach for when you hold the locks in the same scope. The ordering discipline is the answer when the acquisitions are *spread out* — locked in different functions, at different times, can't be wrapped in one `scoped_lock`. That's the general principle behind both: **a process-wide consistent lock order makes AB-BA impossible**, because a cycle in the wait-for graph requires at least two locks acquired in opposing orders, and a global order forbids that by construction.

One subtlety to have ready: the self-transfer edge case. `transfer(X, X)` with the broken code double-locks the same non-recursive mutex → UB/deadlock-on-self. `scoped_lock` handles distinct mutexes; if `from` and `to` can be the same account you must special-case it (`if (&from == &to) return;`) or the ordering fix's `g1`/`g2` will both target the same mutex.

**Where it shows up / the tells.** Anywhere a single operation touches two lockable things: account transfers, moving an item between two queues, hand-over-hand traversal of a linked list or B-tree (lock node, lock child, release parent — fine if always parent→child, deadlock if any path goes child→parent), a buffer pool plus a per-buffer lock, graph algorithms locking two vertices for an edge, bank of shards where an operation spans two shards. Also the nested-call version: function `f()` takes lock A then calls `g()` which takes lock B, while elsewhere `h()` takes B then calls a path that takes A — the ordering disagreement is hidden across call boundaries, which is the hard one to spot.

The textual tells: two `lock_guard`/`unique_lock` lines back-to-back on *different* mutexes, where the mutexes come from parameters or data (so the order is data-dependent, not fixed). Any function taking two `T&`/`T*` of the same lockable type and locking both — immediately ask "what's the order, and does every caller agree?" A lock acquired, then a call to another function that locks something else (cross-function ordering). Recursive structures locked in traversal. And the dead giveaway: the same two mutexes locked in `A, B` order in one place and `B, A` in another anywhere in the codebase — grep for both orders.

The interview move: when you see two locks taken in data-dependent order, say "this is AB-BA if any caller can invert the arguments — I'll either take them with one `scoped_lock`, or impose a total order by address/id so all threads agree." Then flag the self-lock edge case unprompted; that's the detail that signals you've actually built this rather than memorized it.

## Condition-variable wakeup bugs (lost wakeup, missing notify, spurious wakeup mishandled)
Per your handoff note, I'm clubbing 5/6/7 — they're three faces of the same mistake (treating a condition variable as if it carried state), so they're clearer together than apart. One self-contained block.

## 5+6+7. Condition-variable wakeup bugs (lost wakeup, missing notify, spurious wakeup mishandled)

**The one mental model that unifies all three.** A condition variable is *not* a flag and carries *no memory*. It's purely a "park this thread until poked" mechanism. The real state lives in a separate **predicate** — a shared boolean/count you own under the mutex (`ready_`, `!queue.empty()`, `count_ > 0`). The CV only exists so waiters don't spin-poll that predicate. Every CV bug is a failure to respect this split: either you let a poke land when nobody's parked (it's gone forever), or you fail to poke when the predicate changes, or you trust the poke instead of re-reading the predicate. The single correct shape that defeats all three:

```cpp
// WAITER:                                  // SIGNALER:
std::unique_lock<std::mutex> lk(m);         {
cv.wait(lk, []{ return predicate; });           std::lock_guard<std::mutex> g(m);
// predicate is TRUE here, lock held            predicate = true;   // mutate under lock
                                            }                       // then notify (in or
                                            cv.notify_one();        //  out of lock)
```

`cv.wait(lk, pred)` is exactly sugar for `while (!pred) cv.wait(lk);` — and that `while` (not `if`) plus the under-lock predicate mutation is what makes the whole thing robust. Now the three bugs, each as a deviation from this shape.

**#5 Lost wakeup — notify races ahead of wait, and/or `if` instead of `while`:**

```cpp
bool ready = false;
std::mutex m;
std::condition_variable cv;

// WAITER (broken):
void consumer() {
    std::unique_lock<std::mutex> lk(m);
    if (ready)                       // <- checks once
        return;
    cv.wait(lk);                     // BUG: no predicate in the wait. If the producer
    use_data();                      // already ran notify BEFORE we got here, that
}                                    // notify is GONE — CVs don't remember. We park
                                     // forever waiting for a poke that already happened.
// WAITER (also broken — the `if` flavor):
void consumer2() {
    std::unique_lock<std::mutex> lk(m);
    if (!ready)                      // BUG: `if`, not `while`
        cv.wait(lk);                 // wakes on notify OR spuriously; either way we
    use_data();                      // fall straight through to use_data() without
}                                    // re-checking `ready`. Proceeds on a false predicate.

// PRODUCER:
void producer() {
    { std::lock_guard<std::mutex> g(m); ready = true; }
    cv.notify_one();                 // if this fires before consumer reaches wait(), lost.
}
// Interleaving (lost wakeup):
//   producer: ready=true; notify_one()   <- no one is parked yet; poke vanishes
//   consumer: locks, calls cv.wait(lk)   <- waits for a poke that already came -> hangs
```

```cpp
// FIXED: predicate-form wait. The lambda is checked BEFORE parking, so if `ready`
// is already true we never park at all — the early notify isn't needed.
void consumer() {
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, []{ return ready; });   // re-checks predicate every wake; can't lose it
    use_data();
}
```

The fix for lost wakeup *is* the predicate-form wait: because `wait` evaluates the predicate while holding the lock before blocking, a notify that "arrived early" is irrelevant — the state it set is still true, the predicate sees it, and you skip parking entirely.

**#6 Missing notify — state changed, nobody poked:**

```cpp
std::queue<Task> q;
std::mutex m;
std::condition_variable cv;

void push(Task t) {
    std::lock_guard<std::mutex> g(m);
    q.push(std::move(t));            // BUG: predicate (queue non-empty) just became
}                                    // true, but we NEVER call cv.notify_*. Any thread
                                     // parked in pop()'s wait sleeps forever even though
                                     // there's work for it.
void pop_and_run() {
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, []{ return !q.empty(); });   // correct waiter — but it's waiting on a
    auto t = std::move(q.front()); q.pop();  // signal that never comes
    lk.unlock();
    t();
}
```

```cpp
// FIXED: every state change that could satisfy a waiter's predicate must be paired
// with a notify.
void push(Task t) {
    { std::lock_guard<std::mutex> g(m); q.push(std::move(t)); }
    cv.notify_one();                 // poke a waiter now that the predicate can be true
}
```

The lost-wakeup waiter (#5) and the missing-notify signaler (#6) are mirror images: #5 is the waiter failing to re-check, #6 is the signaler failing to announce. A correct system needs *both* halves — predicate-form wait *and* a notify after every relevant mutation.

**#7 Spurious wakeup mishandled — trusting the wake instead of the predicate:**

```cpp
void consumer() {
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk);                     // BUG: bare wait. The OS is allowed to return from
    use_data();                      // wait() with NO matching notify — a "spurious"
}                                    // wakeup. We then use_data() though `ready` may be
                                     // false. Same failure surface as the `if` in #5.
```

```cpp
// FIXED: loop on the predicate so a spurious wake just re-parks.
void consumer() {
    std::unique_lock<std::mutex> lk(m);
    while (!ready)                   // spurious wake -> condition still false -> wait again
        cv.wait(lk);                 // (or: cv.wait(lk, []{ return ready; }); — same thing)
    use_data();
}
```

Spurious wakeups are real and platform-permitted, not theoretical — this is *why* the standard idiom is a predicate loop and never a bare `wait`. Note #5's `if` flavor and #7 are literally the same defect viewed two ways: an `if` re-checks zero times after the wake, a bare `wait` re-checks zero times at all. Both are cured by the same `while`/predicate-form.

**Why clubbing them is right.** All three vanish under one discipline: **(a)** the predicate is shared state guarded by the mutex; **(b)** waiters always `cv.wait(lk, pred)` (equivalently `while(!pred) wait`), never a bare or `if`-guarded wait; **(c)** every mutation that can make some waiter's predicate true is followed by a `notify`. Miss (b) and you get lost-wakeup / spurious-wakeup (5/7). Miss (c) and you get missing-notify (6). Get both and all three are structurally impossible.

**Where they show up / the tells.** Producer–consumer queues and thread pools (the canonical home of all three). Bounded buffers / buffer pools where producers wait on "space available" and consumers on "item available" — and here's the trap: with `notify_one` on a *two-sided* predicate you can wake the wrong kind of waiter, so bounded buffers often need `notify_all` or two separate CVs. Latches and one-shot "ready" flags (lost wakeup is most common here — the setter runs before the waiter parks). Worker shutdown: a `done_` flag set without a `notify_all` leaves workers parked at exit (missing notify). Futures/promises hand-rolled over a CV.

The textual tells, scanning: any `cv.wait(lk)` with **no second argument** — immediate suspect for 5/7. An `if (!pred) cv.wait(lk);` — the `if` is the bug, should be `while` or predicate-form. A mutation of shared state (`push`, `ready = true`, `--count`, `done = true`) under a lock with **no `cv.notify_*` after it** — suspect #6; grep each predicate and confirm every writer of it notifies. A `notify_*` call but the matching `wait` has no predicate — lost wakeup if ordering can invert. `notify_one` on a CV with *heterogeneous* waiters (producers and consumers on the same CV) — wrong-thread-woken hazard, wants `notify_all` or split CVs. And the ordering smell: state set and `notify` called *before* the waiter could plausibly have reached `wait` — fine *only* if the waiter uses predicate-form, broken otherwise.

Two precision points worth saying out loud in the interview. First: **mutate the predicate under the lock**, then notify — notifying *can* be done outside the lock (sometimes better, avoids waking a thread that immediately blocks on the mutex you still hold), but the state change must be locked or the waiter's predicate check races. Second: `notify_one` vs `notify_all` is a correctness decision, not just perf — `notify_one` is safe only when any single waiter can make progress and waiters are interchangeable; if predicates differ across waiters (bounded buffer, or waiters waiting on different conditions sharing one CV), `notify_one` can poke a thread whose predicate is still false, which re-parks and leaves the *right* thread asleep — a lost wakeup in disguise. When unsure, `notify_all` is the conservative correct choice and you optimize to `notify_one` only once you've argued interchangeability.

## 6. Spin / blocking while holding a lock
## 8. Spin / blocking while holding a lock

**Theory.** The critical section is correct, but it does something *slow* while holding the lock — I/O, a syscall, `sleep`, a condition-variable wait, a memory allocation, a callback into unknown code, or a busy spin. Correctness can survive this; throughput doesn't. Every other thread that needs the lock is stalled for the duration of the slow thing, so a lock meant to protect a 100ns update ends up serializing a 10ms disk read. Worst case it's not just slow but a *deadlock*: if the slow operation under the lock can only complete after some other thread acquires the same lock, you've built a cycle. The mechanism is a category error — holding an exclusion primitive across an operation whose cost or blocking behavior is unbounded.

**Broken — I/O inside the critical section:**

```cpp
std::mutex m;
std::unordered_map<int, std::string> cache;

std::string get(int k) {
    std::lock_guard<std::mutex> g(m);        // lock acquired
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    std::string v = read_from_disk(k);       // BUG: blocking I/O (could be 10ms) runs
    cache[k] = v;                            // with the lock HELD. Every other thread
    return v;                                // calling get() for ANY key is blocked the
}                                            // whole time, even pure cache hits. The lock
                                             // now serializes disk latency, not a map op.
```

```cpp
// FIXED: do the slow work outside the lock; this is exactly the TOCTOU/#3 shape,
// so it carries the same re-check-on-commit discipline.
std::string get(int k) {
    {
        std::lock_guard<std::mutex> g(m);
        auto it = cache.find(k);
        if (it != cache.end()) return it->second;   // fast path under lock: cheap
    }                                                // lock RELEASED before slow work

    std::string v = read_from_disk(k);               // slow I/O with NO lock held

    {
        std::lock_guard<std::mutex> g(m);
        auto [it, inserted] = cache.try_emplace(k, v);   // re-check: someone may have
        return it->second;                                // filled k while we read disk;
    }                                                     // return the canonical entry
}
```

The deadlock-flavored version is the one that bites hardest, because it doesn't merely slow down — it hangs:

```cpp
std::mutex m;
std::condition_variable cv;
std::queue<Task> q;

void drain() {
    std::unique_lock<std::mutex> lk(m);
    while (q.empty())
        std::this_thread::sleep_for(10ms);   // BUG: sleeping while HOLDING lk. The only
    auto t = q.front(); q.pop();             // thread that can fill q is push(), which
    /* ... */                                // needs lk to enqueue. We hold lk and sleep;
}                                            // push() blocks on lk forever -> deadlock.
// The correct tool is cv.wait(lk, pred), which ATOMICALLY releases lk while parked
// and reacquires on wake — so push() can take the lock, enqueue, and notify. A manual
// sleep-under-lock can't release, so it starves the very producer it's waiting on.
```

That last one is the crisp lesson: `cv.wait` is special precisely *because* it releases the mutex while blocking. Any other way of "waiting" inside a critical section — `sleep`, a blocking read, joining a thread, acquiring a second lock that another holder won't release until it gets *yours* — keeps the lock held across the block and is a candidate deadlock.

Spinning has its own narrower trap even when it's not a hard deadlock:

```cpp
std::mutex m;
bool done = false;

void waiter() {
    std::lock_guard<std::mutex> g(m);
    while (!done) { }                        // BUG: busy-spin under the lock. `done` is
    /* ... */                                // set by another thread that must take `m`
}                                            // to do so safely -> can't, we hold it ->
                                             // spin forever, 100% CPU, no progress.
```

**Where it shows up / the tells.** Caches that fetch-on-miss with the lock held (the `get` above — extremely common). Connection/thread pools that *create* a connection (a blocking socket connect) inside the pool lock. Logging or metrics that do a blocking `write()`/network send while holding the lock that protects the log buffer. Allocators or buffer pools that call `malloc`/`mmap` (which take their own locks, and can be slow) inside the pool's critical section — also a latent AB-BA with the allocator's internal lock. Callbacks invoked under a lock: you call user/plugin code holding your mutex, it does anything blocking or re-enters your API and tries to re-lock → deadlock (this is why "never call out into unknown code while holding a lock" is a standing rule). Any hand-rolled wait loop using `sleep` instead of a CV.

The textual tells, scanning: inside a `lock_guard`/`unique_lock` scope, look for `read`, `write`, `recv`, `send`, `fopen`, `fread`, any filesystem or socket call; `sleep_for`/`sleep_until`/`yield`; `std::async`, `future.get()`, `thread::join`; `new`/`malloc`/large container resize; a second lock acquisition; a call to a function pointer / `std::function` / virtual method that could be overridden (callback into unknown code); or a `while(...)`/`for(;;)` spin with no `wait`. The meta-tell is *duration mismatch*: the lock's name or purpose says "protect a small data structure," but the body contains something whose cost is unbounded or whose completion depends on another thread. Whenever the thing under the lock can *block on another thread*, ask whether that other thread needs this same lock — if yes, it's not slow, it's deadlocked.

The interview move: when you spot slow work under a lock, separate the two failure modes explicitly — "if this just blocks on external latency it's a throughput bug, fix by doing it outside the lock with a re-check on commit; if the blocked-on event can only be produced by a thread that needs *this* lock, it's a deadlock, and the fix is `cv.wait` which releases the lock while parked, or restructuring so the lock isn't held across the wait." Naming which of the two you're looking at, and that `cv.wait`'s lock-release is the thing that makes waiting-under-lock safe, is what shows you understand the mechanism rather than the slogan.

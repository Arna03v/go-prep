Coming from Go to C++ concurrency is like moving from a self-driving car with smart safety features (goroutines, channels, and automated stack management) to a high-performance sports car with a manual transmission. C++ gives you absolute control, but it will gladly let you grind the gears if you aren't careful.

The biggest mental shift you need to make is **RAII (Resource Acquisition Is Initialization)**. Where Go uses `defer mu.Unlock()`, C++ uses object lifetimes (destructors) to unlock things automatically when a variable goes out of scope.

Here is your comprehensive C++ concurrency cheat sheet for your interview.

---

## 1. The Mutexes (The Data Protectors)

In C++, a **Mutex** is just the raw synchronization primitive. You almost never call `.lock()` and `.unlock()` on them directly because if an exception is thrown or a function returns early, you’ll cause a deadlock. Instead, you pass them to **Lock Managers** (see Section 2).

### `std::mutex`

* **The Theory:** Exactly like Go’s `sync.Mutex`. It is exclusive. Only one thread can hold it at a time.
* **How to use it:**
```cpp
#include <mutex>

std::mutex my_mutex;
int shared_balance = 0;

void deposit(int amount) {
    // We rarely use raw mutexes like this, but this is what happens under the hood:
    my_mutex.lock();
    shared_balance += amount;
    my_mutex.unlock();
}

```



### `std::shared_mutex` (C++17)

* **The Theory:** Exactly like Go’s `sync.RWMutex`. It allows **multiple readers** to hold the lock simultaneously, but only **one writer**. If a thread wants to write, it blocks until all readers are done.
* **How to use it:** Paired with shared locks (see next section).

---

## 2. Locks and Lock Guards (The Safety Managers)

These are RAII wrappers. When you create them, they lock the mutex. When they go out of scope (at the closing brace `}`), their destructor automatically unlocks the mutex.

### `std::lock_guard` & `std::scoped_lock` (C++17)

* **The Theory:** Strict scope-based locking. You buy it, it locks; it dies, it unlocks. You cannot manually unlock it early. `std::scoped_lock` is the modern C++17 version that can lock *multiple* mutexes at once without causing deadlocks.
* **How to use it:**
```cpp
#include <mutex>

std::mutex mtx;

void safe_print() {
    // Locks mtx immediately. Unlocks automatically when the function ends.
    std::lock_guard<std::mutex> lock(mtx); 
    // Modern C++17 way (preferred): std::scoped_lock lock(mtx);

    std::cout << "Thread safe print!" << std::endl;
} // <--- lock goes out of scope here, mutex unlocks!

```



### `std::unique_lock`

* **The Theory:** The heavy-duty, flexible version of `lock_guard`. It still unlocks automatically at the end of the scope, but it allows you to **manually unlock and relock** midway through, and it can be **moved** (`std::move`) between scopes (which is how we did the "hand-over-hand" tree traversal in your previous question).
* **How to use it:**
```cpp
std::mutex mtx;

void complex_task() {
    std::unique_lock<std::mutex> lock(mtx); // Locked here

    // Do protected work...

    lock.unlock(); // Explicitly unlock early to let other threads work

    // Do heavy computation that doesn't need a lock...

    lock.lock(); // Relock to finish up
} // <--- Unlocks here too, just in case it was still locked

```



### `std::shared_lock`

* **The Theory:** The "Reader" lock wrapper. It tells a `std::shared_mutex` that you only want shared read-only access.
* **How to use it:**
```cpp
#include <shared_mutex>

std::shared_mutex rw_mtx;
int data = 42;

int read_data() {
    // Multiple threads can run this at the same time!
    std::shared_lock<std::shared_mutex> lock(rw_mtx); 
    return data;
}

void write_data(int new_val) {
    // Only one thread can run this, blocks all readers.
    std::unique_lock<std::shared_mutex> lock(rw_mtx); 
    data = new_val;
}

```



---

## 3. Semaphores (C++20)

Go doesn’t have a native semaphore type (people usually abuse buffered channels for this). C++20 introduced them natively.

* **The Theory:** A semaphore maintains an internal counter.
* `acquire()` decrements the counter. If the counter is 0, it blocks.
* `release()` increments the counter and wakes up waiting threads.
* **`std::counting_semaphore<N>`**: Counter can go up to N. Great for resource pools (e.g., limiting database connections).
* **`std::binary_semaphore`**: An alias for `std::counting_semaphore<1>`. Can be used like a mutex, but *crucially*, a semaphore can be released by a **different thread** than the one that acquired it (unlike a mutex, where the owning thread *must* unlock it).


* **How to use it:**
```cpp
#include <semaphore>
#include <thread>

// Initialize with 0 tokens. Anyone calling acquire() will block immediately.
std::binary_semaphore signal_to_worker(0); 

void worker() {
    signal_to_worker.acquire(); // Wait until main thread says go
    // Do work...
}

int main() {
    std::jthread t(worker);
    // Prepare data...
    signal_to_worker.release(); // Wake up worker thread
}

```



---

## 4. What Else You Are Missing

Interviews will expect you to know these two concepts to round out your concurrency knowledge:

### A. Condition Variables (`std::condition_variable`)

* **The Theory:** Equivalent to Go’s `sync.Cond`. It allows threads to suspend execution (sleep) until notified by another thread that a specific condition is met. **Crucial rule:** It *must* be paired with a `std::unique_lock`.
* **How to use it:**
```cpp
#include <condition_variable>
#include <mutex>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker() {
    std::unique_lock<std::mutex> lock(mtx);

    // cv.wait releases the lock and sleeps. 
    // When woken up, it re-acquires the lock and checks the predicate (the lambda).
    cv.wait(lock, [] { return ready; }); 

    // Perform work safely knowing 'ready' is true
}

void ship_work() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one(); // Wake up one waiting thread
}

```



### B. Atomics (`std::atomic`)

* **The Theory:** Equivalent to Go's `sync/atomic`. For simple variables (like counters or flags), locking a whole mutex is overkill. Atomics use CPU-level hardware instructions to ensure operations happen lock-free and instantly.
* **How to use it:**
```cpp
#include <atomic>

std::atomic<int> global_counter(0);

void increment() {
    global_counter++; // Thread-safe, lock-free, incredibly fast
}

```



---

## 💡 Quick Interview Cheat-Sheet Summary

| C++ Concept | Go Equivalent | When to use it? |
| --- | --- | --- |
| `std::mutex` + `std::scoped_lock` | `sync.Mutex` | Your default tool to protect shared data from concurrent writes. |
| `std::shared_mutex` + `std::shared_lock` | `sync.RWMutex` | When you have tons of readers but rare writes. |
| `std::unique_lock` | No direct equivalent | When you need advanced control (Condition Variables, moving locks, manual unlocking). |
| `std::condition_variable` | `sync.Cond` / Channels | When Thread A needs to sleep until Thread B finishes an event. |
| `std::binary_semaphore` | Buffered Channel `chan struct{}` size 1 | Signaling events across different threads. |
| `std::atomic<T>` | `sync/atomic` | Simple counters, status flags, or metrics. |

Would you like to try implementing a classic interview problem using these primitives—such as a thread-safe bounded queue (Producer-Consumer pattern)—to practice the syntax?
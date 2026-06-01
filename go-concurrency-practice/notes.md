Theory notes

- use go doc to find out about packages/functions

1. Mutex and RWMutex
    Mutex
    - a mutual exclusion lock
    - in Go, mutex == counting semaphore.

    - func (m *Mutex) Lock()
        Lock locks m. If the lock is already in use, the calling goroutine blocks
        until the mutex is available.

    - func (m *Mutex) Unlock()
        Unlock unlocks m. It is a run-time error if m is not locked on entry to
        Unlock.

        A locked Mutex is not associated with a particular goroutine. It is allowed
        for one goroutine to lock a Mutex and then arrange for another goroutine to
        unlock it.

    - func (m *Mutex) TryLock() bool // since Lock() is blocking
        TryLock tries to lock m and reports whether it succeeded.

        Note that while correct uses of TryLock do exist, they are rare, and use of
        TryLock is often a sign of a deeper problem in a particular use of mutexes.

    RWMutex
    - A RWMutex is a reader/writer mutual exclusion lock. Unlike a regular Mutex (which allows only one goroutine at a time), an RWMutex allows:
        - Many readers simultaneously, OR                                                                                                    
        - One writer exclusively

    - func (rw *RWMutex) Lock() : Writer Acquire
        - Blocks until all readers and writers have released the lock. Once acquired, no other goroutine (reader or writer) can get in.
        - if a writer calls Lock() while readers hold the lock, new calls to RLock() will block until that writer gets its
        turn. This prevents readers from monopolizing the lock forever.

    - Unlock() — Writer Release
        Releases the write lock. Must pair 1:1 with Lock(). Calling Unlock() without a matching Lock() is a runtime panic.

    - RLock() - Reader acquire
        Multiple goroutines can call RLock() at the same time. They all proceed in parallel.

    - RUnlock() - Reader Release
        Releases one read lock. Must pair 1:1 with RLock(). Calling RUnlock() without a matching RLock() is a runtime panic.

    - cannot upgarade RLock to Lock (the writer waits for itself to RUnlock), cannot degrade from Lock to RLock

3. sync.WaitGroup
    - informs one go routine to wait for a certain number of other go routines to finish their execution
    - generally used by main, to wait for the go routines to finish their execution before exiting.
    - wg.Add() : adds a go routine to wait for; wg.Add() is typically called by the goroutine spawning the workers
    - wg.Done() : removes a go routine to wait for; generally by teh goroutine while it is ending
    - wg.Wait() ; waiting for the wg to become = 0; generally by the waiting go routine

4. sync.Once
    - Once is an object that will perform exactly one action.
    - Guarantees that a specific function passed to it will be executed exactly once
    - Any subsequent calls by other goroutines will safely block until the first initialization is complete, and then they will immediately move on without executing the function again.

    General uses
        - Closing an already-closed channel in Go causes a panic. If you have multiple producers that might independently decide a channel needs to be closed, wrapping the close(ch) call inside a sync.Once guarantees it only happens one time, preventing panics.

        - If you have a heavy resource (like a database client, an in-memory cache, or a complex configuration file), you might not want to initialize it at startup if it isn't always needed. sync.Once lets you lazily load it the very first time a goroutine requests it, keeping your startup times fast.

    ```Go

    package main

    import (
        "fmt"
        "sync"
    )

    func main() {
        var once sync.Once
        
        // The function we only want to run one time
        initializeCode := func() {
            fmt.Println("Performing expensive initialization...")
        }

        var wg sync.WaitGroup
        
        // Launch 10 concurrent goroutines
        for i := 0; i < 10; i++ {
            wg.Add(1)
            go func(id int) {
                defer wg.Done()
                fmt.Printf("Goroutine %d calling once.Do\n", id)
                
                // All 10 goroutines call this, but initializeCode only runs once!
                once.Do(initializeCode) 
                
                fmt.Printf("Goroutine %d proceeding\n", id)
            }(i)
        }

        wg.Wait()
    }

    ```

5. context.Content
    - generally used for timeouts, deadlines, signal handling
    - check for usage in the `graceful shutdown with context` folder
    when we call cancel/stop(), the done channel associated with the cntext closes. reading from a closed channel never blocks, and thus
    ```Go
    select{
		case <- ctx.Done() // this returns immediately; otherwise it is blocked till it sees a value
	}

    ```

6. buffered and unbuffered channels
    - channels are used for sync between go routines
    - can be buffered (fixed capacity) or unbuffered (0 element)
    - For unbuffered channels; sending waits until someone receives, receivers waits until someone sends
    - For buffered channels; sending waits until channel is full. receiving waits until channel is empty

7. defer
    - when a program/function stops (returns or panics). it calls its defer statement, in backwards order (last defer -> second last defer ..)
    - is great for cleanup of locks, waitgroups, etc.
    - defer unlock(); guarantees an unlock even if something panics during the execution


2. 2. sync.Cond
    - if moving data or ownership from one place to another, use a channel
    - If you are waiting for a complex environmental state to change, or need to repeatedly wake up an army of goroutines without passing them data, use sync.Cond

    Here is what the `sync.Cond` struct looks like inside:

    * **`L Locker`**: The mutex you provide when you create it.
    * **`notify notifyList`**: The secret sauce. This is a specialized queue managed directly by the lowest levels of the Go runtime.
    * **`checker copyChecker`**: A safety mechanism to panic if you accidentally copy the struct by value (which would corrupt the state of the queue).

    ### **The "Ticket" System (`notifyList`)**

    The `notifyList` works a lot like a ticket dispenser at a deli or a DMV. Under the hood, it keeps track of two main ticket counters:

    1. **`wait`**: The ticket number that will be handed to the *next* goroutine that joins the queue.
    2. **`notify`**: The ticket number of the *next* goroutine that is allowed to be woken up.

    Here is exactly what happens when the methods are called:

    ### **1. What happens during `Wait()`?**

    1. **Take a Ticket:** The goroutine calls into the Go runtime to register itself on the `notifyList` and gets a unique, sequential ticket number (incrementing the `wait` counter).
    2. **Unlock:** It unlocks your `sync.Mutex` so other goroutines can access the shared data.
    3. **Go to Sleep:** The goroutine tells the Go runtime scheduler, "Park me (suspend me) until my specific ticket number is called." The goroutine is now completely asleep and consumes almost zero CPU.
    4. **Re-lock:** Once the runtime eventually wakes it up, the very first thing it does before returning from the `Wait()` function is re-acquire your `sync.Mutex`.

    ### **2. What happens during `Signal()`?**

    When you call `Signal()`, `sync.Cond` checks the `notifyList`.

    * If `wait == notify` (meaning the dispenser and the "now serving" sign show the same number), no one is waiting, and it does absolutely nothing.
    * If someone is waiting, it looks at the `notify` ticket number, increments it by 1, and tells the Go scheduler: "Wake up the exact goroutine holding this specific ticket."
    * This ensures goroutines are cleanly woken up in a First-In, First-Out (FIFO) order.

    ### **3. What happens during `Broadcast()`?**

    `Broadcast()` is slightly different. Instead of waking up just the next ticket, it grabs the current maximum `wait` number and immediately fast-forwards the `notify` number to match it.

    * It then hands that entire range of tickets to the Go scheduler and says, "Unpark *every single goroutine* holding a ticket in this batch."
    * All those goroutines are instantly moved to a "runnable" state. However, because the final step of `Wait()` requires them to re-acquire the `sync.Mutex`, they can't all proceed at exactly the same time; they will serialize and wake up one-by-one as they sequentially win the lock.

    Common usecases; Channels fall short in two very specific situations involving complex state and broadcasting.
        - 1. Repeated Broadcasting (The "Pause/Resume" Problem)
            Channels have a built-in broadcast mechanism: closing the channel. If you have 100 workers reading from a channel, calling close(ch) instantly wakes all of them up.

            However, you can only close a channel once. If you are building a system that needs to repeatedly pause and resume all workers (e.g., pausing processing when the system is under heavy load, and resuming when it's safe), channels make this incredibly difficult. You would have to constantly create new channels, tear down old ones, and synchronize all 100 workers to look at the new channel.

            With sync.Cond, you just change a boolean isPaused flag to false, and call cond.Broadcast(). You can do this thousands of times.

        - 2. Multi-Variable or Complex Conditions
            A goroutine waiting on a channel is waiting for exactly one thing: data to arrive or space to open up.

            But what if a goroutine should only proceed if a complex set of conditions is met?

            Wait until: SystemRAM < 80% AND ActiveConnections < 500 AND IsShuttingDown == false

            You cannot easily encode that logic into a channel. With sync.Cond, it is trivial because you hold a Mutex. You lock the state, evaluate your complex if statement, and if it's not met, you call Wait().
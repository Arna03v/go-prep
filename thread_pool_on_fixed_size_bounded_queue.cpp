#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <stdexcept>
#include <string>
using namespace std;

/*
Design a fixed-size thread pool. Callers submit tasks; a pool of N worker threads
pull tasks off a shared queue and execute them. The task queue is BOUNDED — if it's
full, submit blocks until there's room (backpressure). Support graceful shutdown
(finish queued tasks, then stop) and immediate shutdown (stop after current task,
drop the rest).
*/

// max size of queue = C;
// max number of worker threads = N;

// on immediate shutdown: wait for all currently executing tasks to finish and then shut down. dont accept anything new from the queue

// ========= PUBLIC INTERFACE ===============
// we should have different types of tasks. but for the scope of this prpe we will limit ourself to one type of task and one type of result.
struct Task{
    int id; // task id
    string taskName; // some random task name; does not matter
};

struct Result{
    int id; // maps to task id
    string resultField; // some random res; does not matter
};

class threadPoolInterface{
    // destructor
    public:
        virtual ~threadPoolInterface() = default;

        // need to give the task to the interface
        // pushTask after close will return an runtime_error that the threadpool is closed
        // we push the task and wait fir the result
        virtual future<Result> pushTask(const Task& current_task) = 0;

        // close gracefully
        virtual void gracefulShutdown() = 0;

        // close immediately
        virtual void immediateShutdown() = 0;

};


// promise and future
// we put the task in the queue and the promise<Result> from the user side
// the thread will take the task -> do work -> and push the result in the promise
// the reader will then read this result as a type of future<Result>
    // the promise isn't "from the user side." The pushTask function creates the promise internally, extracts the future, hands the future back to the caller, and moves the promise into the queue. The caller never sees the promise.

// =========== IMPLEMENTATION =====================

/*
lets dfine the push-pop flow and come with the data structures required while discussing

the user gives a task and calls pushTask
- we will extract the task, create a promise of the task and push it in the queue
    - we have fixed number of slots in the queue; need one mutex to read the value and increment it 
    - need the queue

- now to consume;  we are making the action of picking the task a part of the threads job instead of manually assigning it the task

- for gracefulshutdown
    - set a bool gracefulShutdown == true (cannot be atomic; look athe scenario at the end)
        - checked during every push

- for immediateShutdown
    - set another atomic bool immediateShutdown so that the user knows (cannot be atomic, look athe scenario below)
    - wipe the entire queue (tasks yet to be picked up will not be picked up anymore)
    - wakeup all threads -> sees closed -> breaks out of the loop and returns
        - finish

// if queue == full; pushTask sleeps. if queue == empty the workers sleep
    - 2 condition variables

    notFull sleeps until a task is consumed. signaled by the worker thread ones a job is consumed from the task and the size is decremented

    notEmpty sleeps until something is pushed into the queue and the size is incremented

notify_one for normal enqueue/dequeue is correct.

- one atomic bool for graceful and one for immeidate
    - closed == graceful || immediate

 One mutex protects everything: queue, shutdown flags, all of it. Both CVs use this same mutex. otherwise look at this scenario

 Thread A (shutdown):          Thread B (worker):
                              lock mutex
                              check predicate: queue.empty() && !shutdown → true
                              → about to sleep on CV...
set shutdown = true (atomic, no lock)
notify_all()
                              → now enters sleep (missed the notify!)
                              → stuck forever, join() deadlocks

What about callers blocked on not_full? Queue is full, pushTask is sleeping. Someone calls shutdown. Those callers need to wake up too — otherwise they're stuck forever. You need not_full.notify_all() during shutdown as well, and pushTask's predicate needs to check the shutdown flag.

*/
class threadPoolImplementation : public threadPoolInterface {
    private:
        size_t maxQueueSize;
        vector<thread> workers;
        queue<pair<Task,promise<Result>>> taskQueue; // pass in task and promsie of the result

        // mutexes
        mutex poolMutex;
        condition_variable cvNotEmpty; // for workers waiting for tasks
        condition_variable cvNotFull; // for pushing the task into the queue

        // flasg for closed
        bool isImmediateShutdown = false;
        bool isGracefulShutdown = false;

        void workerLoop(){
            // to implement
            while(true){
                std::pair<Task, std::promise<Result>> grabbedTask;

                // acquire lock and take the task from the queue
                {
                    std::unique_lock<std::mutex> lock(poolMutex);

                    cvNotEmpty.wait(lock, [this]() {
                        return !taskQueue.empty() || isImmediateShutdown || isGracefulShutdown;
                    });

                    if (isImmediateShutdown) break;
                    if (isGracefulShutdown && taskQueue.empty()) break;

                    grabbedTask = std::move(taskQueue.front());
                    taskQueue.pop();

                    cvNotFull.notify_one();
                }

                try {
                    Result res;
                    res.id = grabbedTask.first.id;
                    res.resultField = "Processed: " + grabbedTask.first.taskName; 
                    grabbedTask.second.set_value(res); 
                } catch (...) {
                    grabbedTask.second.set_exception(std::current_exception());
                }
            }
        }

    public:
        // constructor
        // Constructor initializes the worker threads and capacity
        threadPoolImplementation(size_t numThreads, size_t queueCapacity) 
            : maxQueueSize(queueCapacity) {
            
            for (size_t i = 0; i < numThreads; ++i) {
                workers.emplace_back(&threadPoolImplementation::workerLoop, this);
            }
        }

        // sine we already have the implementation, we can just map it
        ~threadPoolImplementation() override {
            // Default to safe graceful shutdown if user forgot to call close manually
            gracefulShutdown();
        }

        future<Result> pushTask(const Task& current_task) override{
            // acquire teh lock
            // if !notFUll then go sleep on the condition variable

            promise<Result> taskPromise;
            future<Result> taskFuture = taskPromise.get_future(); // this the read end given to the user

            {
                unique_lock<mutex> lock(poolMutex);

                // 1. Refuse new tasks if a shutdown sequence has started
                if (isGracefulShutdown || isImmediateShutdown) {
                    throw runtime_error("Submission failed: Thread pool is shutting down.");
                }

                // wait for queue to not be full
                // cv.wait(lock, predicate); if preicate == true : wakes up
                cvNotFull.wait(lock, [this](){
                    // If the queue is completely full and a caller thread is sleeping, what happens if someone suddenly calls immediateShutdown()
                    return taskQueue.size() < maxQueueSize || isGracefulShutdown || isImmediateShutdown;
                });

                // now it is woken up
                // what if someone called shutdown after check and before acquiring the lock
                if (isGracefulShutdown || isImmediateShutdown) {
                    throw runtime_error("Submission failed: Thread pool closed during wait.");
                }

                taskQueue.push({current_task, std::move(taskPromise)});
                cvNotEmpty.notify_one(); // notify the next thread waiting on the queue to not be empty
            }

            return taskFuture;
        }

        void gracefulShutdown() override{
            // acquire lock and check
            {
                std::unique_lock<std::mutex> lock(poolMutex);
                if(isGracefulShutdown || isImmediateShutdown) return; // the thread checks if it was already called
                isGracefulShutdown = true;
            }

            cvNotFull.notify_all();
            cvNotEmpty.notify_all();

            for (std::thread& worker : workers) {
                if (worker.joinable()) worker.join();
            }

        }

        void immediateShutdown() override{
            {
                std::unique_lock<std::mutex> lock(poolMutex);
                if (isImmediateShutdown || isGracefulShutdown) return; // the cntract called first wins. not priority based for now
                isImmediateShutdown = true;

                // empty the queue
                while (!taskQueue.empty()) {
                    taskQueue.pop(); // caller gets an error. intended for this scope

                }
            }

            cvNotFull.notify_all();
            cvNotEmpty.notify_all();

            for (std::thread& worker : workers) {
                if (worker.joinable()) worker.join();
            }
        }


};


/*
HOW A CONNECTION POOL IS DIFFERENT


Solid implementation — the shutdown handling and the dual-CV design are exactly right. Here's the connection pool delta for your notes.

````markdown
# Connection Pool vs Thread Pool — the delta

## The one-line difference
Thread pool: the pool **owns worker threads** that pull tasks and run them. The pool is active.
Connection pool: the pool **owns reusable resources** (connections) that callers borrow and return. The pool is passive — it doesn't run anything; callers do the work, holding a connection while they do.

So the direction of flow flips:
- Thread pool: caller *pushes work in* → pool's threads execute it → result comes back via future.
- Connection pool: caller *pulls a resource out* → caller does the work itself → caller *puts the resource back*.

## What disappears
- **No worker threads.** No `workerLoop`, no `vector<thread>`, no thread spawning in the constructor. This is the biggest simplification — the whole consumer side is gone.
- **No promise/future.** Nobody's running a task on your behalf, so there's no async result to deliver. `acquire()` returns the connection synchronously.
- **No task execution, no try/catch around set_value.** The pool never runs user code.
- **cvNotEmpty's meaning changes** (see below).

## What stays structurally identical
- One mutex protecting everything.
- A bounded resource count + blocking when exhausted.
- One condition variable for "wait until a resource is available."
- Shutdown flags checked in the wait predicate, notify_all on shutdown to wake blocked waiters.
- Throw if acquire() is called after shutdown.

## The interface
```cpp
class ConnectionPoolInterface {
public:
    virtual ~ConnectionPoolInterface() = default;

    // borrow a connection; BLOCKS if none free (this is your cvNotEmpty wait)
    virtual Connection acquire() = 0;          // or unique_ptr<Connection> / a handle

    // give it back so someone else can use it (this is what notifies a waiter)
    virtual void release(Connection conn) = 0;

    virtual void shutdown() = 0;
};
```

## The data structure
```cpp
queue<Connection> idleConnections;   // the FREE connections, ready to lend out
// (vs thread pool's queue<pair<Task, promise<Result>>> of pending WORK)
```
Key reframing: the thread pool's queue holds **work waiting to be done**. The connection pool's queue holds **resources waiting to be used**. Opposite semantics — one is demand, the other is supply.

## acquire() — mirror of the worker's dequeue, not of pushTask
```cpp
Connection acquire() {
    unique_lock<mutex> lock(poolMutex);
    if (isShutdown) throw runtime_error("pool closed");

    cvAvailable.wait(lock, [this]{
        return !idleConnections.empty() || isShutdown;   // wait for a FREE connection
    });
    if (isShutdown) throw runtime_error("pool closed during wait");

    Connection c = std::move(idleConnections.front());
    idleConnections.pop();
    return c;          // caller now OWNS it until they release. NOT under lock while they use it.
}
```

## release() — this is the notify side (replaces the worker's "I finished" step)
```cpp
void release(Connection c) {
    {
        unique_lock<mutex> lock(poolMutex);
        idleConnections.push(std::move(c));     // resource back in the pool
    }
    cvAvailable.notify_one();                    // wake ONE waiter blocked in acquire()
}
```

## Why typically ONE cv, not two
The thread pool needed two (notFull for producers, notEmpty for consumers) because tasks
flow one way through a queue with two distinct wait conditions. The connection pool has a
**fixed set of N connections cycling between in-use and idle** — callers only ever wait on
one condition: "is a connection free?" There's no "queue full" backpressure because callers
aren't adding new resources, just borrowing existing ones. So one cv (`cvAvailable`) covers it.

(If you *also* bound how many callers can queue up waiting, a second cv reappears — but the
base design is one.)

## The thing interviewers poke at: release must always happen (RAII)
A caller that acquires and then throws/returns early without calling release() **leaks a
connection permanently** — the pool slowly drains to zero and every acquire() blocks forever.
The C++ fix is RAII: hand back a wrapper whose destructor auto-releases, so the connection
returns even on an exception. This is the connection pool's signature design point — the
thread pool didn't have it because the pool owned the threads; here the *caller* holds the
resource, so you must guarantee its return.

```cpp
// instead of raw Connection, return a guard:
class PooledConnection {
    ConnectionPool* pool_;
    Connection conn_;
public:
    ~PooledConnection() { pool_->release(std::move(conn_)); }  // auto-return on scope exit
    Connection& get() { return conn_; }
    // delete copy, allow move
};
```
This is the same RAII principle as lock_guard — acquire in constructor, release in destructor —
applied to a pooled resource instead of a mutex.

## Shutdown — nearly identical, simpler
Set isShutdown under the lock, notify_all on cvAvailable to wake everyone blocked in acquire().
No worker threads to join (you don't own any). You may optionally tear down the actual
connections in the queue (close sockets etc.). No "drain the queue and finish tasks"
distinction — there are no tasks, so graceful vs immediate mostly collapses into one:
do you wait for borrowed connections to come back before destroying them, or force-close?
That's the only graceful/immediate axis here.

## Summary table
| | Thread pool | Connection pool |
|---|---|---|
| Pool owns | worker threads | reusable connections |
| Queue holds | pending tasks (work/demand) | idle connections (resource/supply) |
| Who does the work | pool's threads | the caller |
| Result delivery | future<Result> | none — synchronous return |
| acquire/submit blocks when | queue full | no free connection |
| cvs needed | 2 (notFull + notEmpty) | 1 (available) |
| Signature risk | holding lock during task exec | caller leaks resource → RAII return |
| Shutdown joins threads | yes | no threads to join |
````

The mental switch to carry: a thread pool is a **consumer of work**, a connection pool is a **lender of resources**. Once you see that the queue means opposite things in each (work-to-do vs things-to-lend), every other difference falls out. The RAII auto-return wrapper is the one new idea worth being able to write cold — it's the connection pool's equivalent of "don't hold the lock during execution," i.e. the thing they're checking you know.
*/
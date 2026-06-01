package tokenbucketratelimitedworkerpool

import (
	"fmt"
	"sync"
	"time"
	"context"
	"errors"
)

// functions we need open to public
// Submit() : to schedule the job
// the job will give the result in a channel associated with the pool
// expose read-only stream to the public: Results
// Stop()

type Job func() string

type Dispatcher struct{
	// each dispatcher/pool will have
	// a list of workers
		// no need to store the details of each worker
		// just the number of wokers is enough
		// only need it to spin up; and for nothing else

	// an input stream for the workers to read from; reads the functions
	jobs chan Job
	// an output stream for the workers to write to; alreday in the contract that output is a string
	output chan string


	// only one worker at a time to read from the stream : readingMutex
	// only one worker at a time to write to the stream  : writingMutex
		// channels are by default threadsafe; DONT NEED THE MUTEXES

	// a value to indicate how many requests per second can be processed
	// rateLimit int; dont need it after initialising the ticker
	// a ticker assocaited with above. One global ticker, shared across all workers. That's what enforces the pool-wide rate limit.
	ticker *time.Ticker

	// the stop function will rewuire an internal done channel (explained below); nothing is passed through the done chanel; so empty structs
	done chan struct{}

	// it will also need to wait for all workers to finish working: a waitgroup
	wg sync.WaitGroup

}

func NewDispatcher(workerCount int, rateLimitPerSec int) *Dispatcher{
	d := &Dispatcher{}
	d.ticker = time.NewTicker(time.Second/time.Duration(rateLimitPerSec))

	// initialise the channels
	d.jobs = make(chan Job, workerCount) // to prevent more GR from reading
	d.output = make(chan string, workerCount) // to prvent more GR from writing
	d.done = make(chan struct{})

	// spin up these many go go routines
	for i:=0; i<workerCount; i++{
		d.wg.Add(1)
		go worker(d) // is a thread that waits for a task to come in
	}

	return d
}

func worker(d *Dispatcher){
	// waits for the job from Jobs channel
	// takes it and submits the result to the results channel
	// also checks done channel before picking up the job and then re-loops
	defer d.wg.Done()

	for{
		select{
		case <- d.done:
			// done channel has closed
			// stop taking more inputs
			fmt.Println("worker not accepting any more inputs ...")
			return

		case <- d.ticker.C:
			// can block on the jobs queue and if done is closed 
			// we wont see it
			select{
			case <- d.done:
				// same as the above worker
				fmt.Println("worker not accepting any more inputs ...")
				return

			case curr_job := <- d.jobs:
				// pick the job; do the processing and push to the output channel
				fmt.Println("processing the current job: ", curr_job)
				result := curr_job()
				d.output <- result 
				fmt.Println("finished processing the job: ", curr_job)
			}
			
			
		}
	}
}


func (d *Dispatcher) Submit(ctx context.Context, job Job) error {
    // Push a job to the workers. 
	// if the context times out then calls stop function
		// wrong
		// a single job timinOut d=should not stop the entire dispatcher
	
	select{
	case <- ctx.Done():
		// the context timed out
		return ctx.Err()
	case <- d.done:
		// the done channel is closed; no more jobs being accepted
		// how to return error here
		return errors.New("dispatcher stopped")
	case d.jobs <- job: // wait until the jobs queue blocks
		return nil

	}


}

func (d *Dispatcher) Results() <-chan string {
    // Expose read-only stream of completed job outputs
	return d.output
}

func (d *Dispatcher) Stop() {
    // Tear down workers and close channels cleanly without panicking

	// closes the done channel	
		// enures no more requests being taken up

	// closes the jobs channel
	
	// waits for the waitgroup
		// can be done in background
		// closes the results channel
		// cannot be backgorund
		// wait for the results to close before exiting
		// might as well be sync

	close(d.done)
	close(d.jobs)

	d.wg.Wait()
	d.ticker.Stop()
	close(d.output)


}

// how will thr stop function work
/*

Yes, that sequence is correct. Here's your notes summary:

---

**`Stop()` — what it does, why `done` exists, and how it solves the problem**

**What `Stop()` is supposed to do:**
- Prevent new jobs from being accepted
- Let workers finish their current in-flight job
- Wait until all workers have fully exited
- Then clean up channels

**The problem:**
`Stop()` and `Submit()` can be called from different goroutines simultaneously. If `Stop()` closes the jobs channel while `Submit()` is about to write to it — that's a **panic** in Go. Writing to a closed channel always panics.

**Why we can't just close the jobs channel to signal workers:**
It would work for workers (they just see the channel closed and exit), but it leaves `Submit()` vulnerable to the race above.

**The solution — the `done` channel:**
A dedicated `done` channel that only `Stop()` ever closes. `Submit()` checks it first in a `select` before touching the jobs channel. Once `done` is closed, `Submit()` backs off and returns an error — safely, no panic.

**Final `Stop()` sequence:**
```
Stop() called
    ↓
close(done)     ← Submit() sees this, backs off safely
    ↓
close(jobs)     ← workers range over jobs, see it closed, exit
    ↓
wg.Wait()       ← block until every worker goroutine finishes
    ↓
close(results)  ← safe now, no worker will ever write to it again
```

---

Ready to move on to the struct fields now?



*/
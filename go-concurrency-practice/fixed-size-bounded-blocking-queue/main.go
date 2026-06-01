package main

import "sync"

// Implement a thread-safe, fixed-size queue where writing to a full queue blocks until space is available,
// and reading from an empty queue blocks until data is available.

/*

Multiple goroutines can call Enqueue and Dequeue simultaneously without
corrupting the internal state. Concretely — two goroutines calling Enqueue
at the same time should not both write to the same slot in the ring buffer.

The lock ensures only one goroutine mutates the queue's internal state at a time.
- only one read OR write at a time

*/

// NO USING BUFFERED QUEUE

type BoundedQueue struct{
	data []interface{} // slice of any type
	head int           // reading happens from here
	tail int           // writing happens to this index
	count int          // number of elements in the queue right now
	max_capacity int   // maximum capacity of the queue
	mu sync.Mutex      // mutex to ensure only one GR is operating on the queue
	waiting *sync.Cond  // conditional mutex to wait
}

// now we need to initliase this struct
// this needs to work for any type T
func newBoundedQueue(capacity int) *BoundedQueue{
	// make a new bounded queue
	q := &BoundedQueue{} // q stores teh address of a bounded queue
	q.data = make([]interface{}, capacity);
	q.head = 0 
	q.tail = 0
	q.count = 0
	q.max_capacity = capacity

	/*
		can also just do
		q := &BoundedQueue{
        	data:         make([]interface{}, capacity),
        	max_capacity: capacity,
    	}

	*/

	// setup the condition variable
	q.waiting =  sync.NewCond(&q.mu)

	return q
}

func (q *BoundedQueue) Enqueue(el interface{}) {
	/*

	acquire lock
	for count == max_capacity → Wait()   // sleep, wake up, recheck
	write to data[tail]
	tail = (tail + 1) % max_capacity
	count++
	Broadcast()                          // wake everyone — Dequeue waiters will now proceed
	release lock

	*/

	q.mu.Lock()
	defer q.mu.Unlock()

	for q.count >= q.max_capacity {
		// call wait on the condition variable
		// goes to sleep uless woken up
		// Wait is atomic
		q.waiting.Wait() 

		//if it wakes up but still satisfies condition; goes back to Wait
	}

	// out of this for loop and hence woken up
	// recheck logic done
	// reacquires the mutex lock
	q.data[q.tail] = el;
	q.tail = (q.tail + 1 ) % q.max_capacity
	q.count++

	// inform others that work is done
	q.waiting.Broadcast()
}

func (q *BoundedQueue) Dequeue() interface{} {
	/*
		acquire lock
		for count == 0 -> Wait()
		once up; read from head
		head ++
		count --
		Broadcast
	*/

	q.mu.Lock()
	defer q.mu.Unlock()

	for q.count == 0{
		q.waiting.Wait()
	}

	// now out of this looop
	// read
	element_read := q.data[q.head]
	q.head = ( q.head + 1 ) % q.max_capacity
	q.count--

	q.waiting.Broadcast()

	return element_read

}

func main(){
	// multiple go routines can work with queue
	// and call enqueue (push) and dequeue(pop)
	// writing to queue pushes and reading pops
	
	// queue is of fixed size
	// only one read OR write at a time

	// need to define the queue
	// since we cant use buffered channels
	// it needs to be a slice of whatever data

	// now to handle size
	// we need a mutex to handle the size (increment/decrement)

	// if size >= N; all push waits
		// if size --; then all threads can wake up and write (can be the same mutex as above to increment/decrement)
	// if size <= 0; all pop waits
		// if size++; then all threads can wake ip and read ( '' )

	// can be done by sync.Cond

	// need to figure out how to write to end of the slice
	// and how to read from the start of the slice
	
	// for reading we can just delete teh first element using `s = s[1:]` and for writing at the end we can do `// Add a new item to the end       
      // s = append(s, "cherry")` both of which are O(1)

	// NO WE CANT
	// s = s[1:] leaks memory.
	// It moves the slice header forward but the underlying array still holds references to the old items. Over time the backing array grows without being freed.
	 
	// Pre-allocated slice + head and tail indices is the ring buffer. One thing to fix though: count = tail - head breaks the moment tail wraps around past head.

	// One sync.Cond works. The for loop handles it — wrong waiters wake up, recheck, fail the condition, and go back to sleep. It is correct code.
		// simpler code but less efficient

	// With two sync.Cond you use Signal() precisely:
		// After dequeue, signal notFull — only an enqueue waiter wakes
		// After enqueue, signal notEmpty — only a dequeue waiter wakes
		// more complex code but more efficient

	// going ahead with the broadcast
	

}


// build a counting semaphore
package main

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

// accept the number of concurrent operations allowed
// acquire()
// release()

type Semaphore struct{
	// since we cant pass constructors to structs
	ch chan struct{} // define channel of empty structs
}


type Worker struct{
	id int
}

// now construct it
func NewSemaphore(N int) *Semaphore{
	return &Semaphore{
		ch : make(chan struct{}, N),
	}

	// return the address of the semaphore because we need to make modification on it, and not its value which will be copiedwh
}

func (s *Semaphore) Acquire(){
	// tries to push an empty struct into this channel 
	// returns when done
	// else blocked
	s.ch <- struct{}{}
	return
	
}

func (s *Semaphore) Release(){
	// tries to read an empty struct from this channel
	// returns when done
	// else blocked
	<- s.ch
	return

}

func work(w *Worker, sem *Semaphore, wg *sync.WaitGroup){
	defer wg.Done()
	fmt.Println("[WORKER-", w.id, "] ::: printing before execution")

	sem.Acquire()
	fmt.Println("[WORKER-", w.id, "] ::: printing during execution, at time: ", time.Now())
	time.Sleep(2 * time.Second)
	
	sem.Release()
}

func main(){
	fmt.Println("Starting the counting semaphore...")
	fmt.Print("Please enter the number of concurrent operations allowed at once: ")

	unprocessed_input, err := bufio.NewReader(os.Stdin).ReadString('\n')
	if err != nil {
		fmt.Println("Error reading the input: ", err)
	}

	input, err := strconv.Atoi(strings.TrimSpace(unprocessed_input))
	if(err != nil){
		fmt.Println("Error coverting input to an integer: ", err)
	}

	// we need something to keep track of the semaphore counter
	// default implementation? 
		// counter; increment and decrement
		// but the increment and decrement need to atomic
		// and we cant use sync.Mutex

	/*
	but since we cant use mutex, we need something else. 
	a counter; and a channel of size 1 (int); the increment function pushes +1 to the counter, the decrment function pushes -1 to the counter. 

	counter starts from 0; if the value hits N, it means that N processes are already have the lock and we will  not increment more. meaning the increments should block. but the decrements should not block

	which means we probably need different channels for incrementing (+1) and decrementing (-1) the value of teh counter (stores how many processes have acquired the locks)

	// or we can have something like a queue
	// each process tries to acquire the lock by passing its PID
	// if the PID is in the queue (of size N) it means that the process has acquired the semaphore
	// otherwise it is waiting outside the queue to enter and acquire the semaphore
	it can only enter when the queue size goes down

	acquire function pushes teh PID into the queue
	and the release function pops ths PID from the queue freeing up space for other processes.

	meaning it has to be a buffered channel of size N; capacity will represent the number of concurrent processes allowed to run at the same time

	// we probably dont need to pass the PID, we can just pass the token which when accepted; doesnt block the code. what can this token be? 
		passing an int or bool is fine/ but we can pass an empty struct which takes up 0 bytes of space
		leading to no memory


	*/

	// semaphore is just a channel of empty structs
	// acquire pushes an empty struct
	// release pops an empty 
	
	sem := NewSemaphore(input)

	// now lets fire of 20 concurrent workers (say with worker id)
	// each printing their id and exiting
	// but no more than 3 at a time
	// lets add a random wait for each as well

	// we also need the main to wait until their execution finishes
	var wg sync.WaitGroup

	// initiliase the workers
	for i := 0; i < 20; i++{
		w := Worker{id: i}
		fmt.Println("[MAIN] : initialised a worker with id: ", i)
		wg.Add(1)
		go work(&w, sem, &wg)
	}

	// need to wait until all the workers are done
	wg.Wait()

	fmt.Println("[MAIN] ::: all workers have finished execution")
	fmt.Println("Exiting ...")

}
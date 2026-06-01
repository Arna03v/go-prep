package main

import (
	"context"
	"os"
	"os/signal"
	"sync"
	"fmt"
	"time"
)

func worker(ctx context.Context, wg *sync.WaitGroup){
	defer wg.Done()

	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()

	// select statement on the ticker
	for {
        select {
        case <-ticker.C:
            // This case triggers every 500ms
            fmt.Println("Worker: Doing work...")
            
        case <-ctx.Done():
            // This case triggers instantly when Ctrl+C is pressed
            fmt.Println("Worker: Received shutdown signal. Starting cleanup...")
            
            // TODO: Simulate cleanup taking 2 seconds (using time.Sleep)
			time.Sleep(2 * time.Second)
            // TODO: Print a message saying cleanup is finished
            
            return // Crucial: breaks out of the infinite for-loop
        }
    }

}

func main(){
	// need to create a context which listens for ctrl C
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)

	defer stop()	// to stop intercepting the signal
	var wg sync.WaitGroup

	// start the worker
	wg.Add(1)
	go worker(ctx, &wg)

	fmt.Println("Main: Waiting for Ctrl+C to stop...")

	wg.Wait() // until the routine is done
	fmt.Println("exiting from main ....")
}
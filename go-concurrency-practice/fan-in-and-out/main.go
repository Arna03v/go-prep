package main

import (
	"fmt"
	"sync"
	"math/rand"
	"time"
)


func generate(input_channel chan int){
	for i := 1; i<=10; i++{
		input_channel <- i
	}

	close(input_channel)
}

// multiplies the number by 2
func processor1(input_channel chan int, output_channel chan int){
	for val := range input_channel{
		output_channel <- (val * 2)
		fmt.Println("[PROCESSOR1] doubling the number: ", val)
		time.Sleep(time.Duration(1+rand.Float64()) * time.Second) 
	}
}

func processor2(input_channel chan int, output_channel chan int){

	for val := range input_channel{
		output_channel <- (val * val)
		fmt.Println("[PROCESSOR2] squaring the number: ", val)
		time.Sleep(time.Duration(1+rand.Float64()) * time.Second) 
	}

}

func main(){
	input_channel := make(chan int)

	fmt.Println("[MAIN] ::: calling generate function")
	go generate(input_channel) // this has to be a go routine; or generate gets blocked -> meaning main gets blocked

	output_channel := make(chan int)

	var processor_wait_group sync.WaitGroup 


	// determine when both processors are done working
	// close the merged channel

	// we need to monitor the execution in bacjground; and close when both are done

	processor_wait_group.Add(1)
	go func(){
		defer processor_wait_group.Done()
		processor1(input_channel, output_channel)
	} ()

	processor_wait_group.Add(1)
	go func(){
		defer processor_wait_group.Done()
		processor2(input_channel, output_channel)
	} ()

	// another function in the background just to monitor

	go func(){
		processor_wait_group.Wait()
		// close the output channel
		close(output_channel)
	} ()


	// print the result from the merged channel using range
	// so we know that if there is an error with closing; it shows up
	for val := range output_channel{
		fmt.Println("[MAIN] ::: printing value from the output channel", val)
	}




}
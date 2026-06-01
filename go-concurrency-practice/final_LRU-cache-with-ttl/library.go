package lrucachewithttl

import (
	"context"
	"sync"
	"time"
)

/*

does close() also wait for the get, put to finish?
	No
	In-flight Get and Put calls are the caller's responsibility to not issue after Close(). What Close() does guarantee is that once it returns, the background goroutine has fully exited — no more evictions happening under the hood




*/

// Cache is a fixed-capacity LRU cache with TTL-based expiry.
type Cache interface {
    // Get returns the value for a key and marks it as recently used.
    // Returns false if the key does not exist or has expired.
    // Expired items are lazily evicted on Get.
    Get(key string) (string, bool)

    // Put inserts or updates a key with a value and a TTL.
    // If the cache is full, the least recently used item is evicted first.
    // Resets the TTL if the key already exists.
    Put(key string, value string, ttl time.Duration) error

    // Len returns the number of items currently in the cache.
    // Does not count expired items.
    Len() int

    // Close stops the background cleanup goroutine and waits for it
    // to finish. No operations are accepted after Close is called.
    Close() error
}

type Node struct{
	key string
	val string
	next *Node
	prev *Node
	ttl time.DateTime

}

type LRUMap map[string]Node

// we expect this LRUMap to expose
// Get (key string) (string, bool)
	// this checks the map and returns the value and does teh updation in the Linked list

// Put (key string, value string)
	// inserts teh value. and does the eviction if size is exeeceded

// Your background goroutine needs to evict a specific expired key by name. Does LRUMap expose an operation for that?
	// yes we need a funtion to delete a entry in LL by the key

// And iteration is handled by cacheImpl directly since LRUMap is just a map underneath.

	/* LRUMap exposes

	Get(key string) (string, bool)
	Put(key string, value string)
	Delete(key string)

	*/


type cacheImpl struct{
	/*
	
	A general LRU is implemented as a map[key]Node (node in the linked list)
	the linked list is a doubly linked list; which has a head and tail
	// head -> next is the most recently used and the tail is the least recently used
	when capacity is exceeded we evict from the tail

	we are not implmeneting the LRU here, since this interview focuses on

	*/

	cache LRUMap

	// once the close is called, no more new get/put should be supported
	ctx context.Context
	cancel context.CancelFunc

	max_capacity int   // used by the internal LRUMap
	// curr_capacity int  // handled by the internal LRUMap

	/*


	to finish designing the impl struct we need to discuss how the background thread will be running. 

	all the time; paused during reads/writes
	runs every x millisecond intervals (lesser CPU usage) for almost similar performance
	runs before every call[get/put] (makes the call wait)
	runs after every call[get/put] but at the risk of reading stale values. need to add a extra check for stale values after being read and before being returned
	based on this, it seems easiest to to a runs every x milliseconds, as well as a check for stale values if the expiryhappens after runX but before runY. 

	is that alright?
		YES

	Validated periodic cleanup with lazy eviction fallback approachThat's the right call. Periodic cleanup handles bulk eviction cheaply, and lazy eviction in Get() closes the gap for items that expire between two runs.

	*/

	// need a ticker

	ticker time.Ticker

	// now the write and read calls will modify teh LRU list order
	// need a mutex for it, one operation at a time
	mu sync.Mutex

	// close() also needs to wait for the background process to finish
	wg sync.WaitGroup
	

}

/*

taking from user

user gets more control over CPU usage
user understands their workload better and hence can optimise for performance
the user needs to remember this exists, if nothing is passed i can set it as a default value as well. this ensures that even if some user does not wish to use this faciltiy the code still works
if this is the case i will use a config struct, over a option function mainly because it is easier to read with such few paramaters

*/

type ConfigStruct struct{
	MaximumCacheCapacity int
	BackgroundCleanerInterval int // lets assume it is in seconds for code simplicity
}

// constructor
// returning the cacheImpl beats the purpose of the interface
func NewCacheImpl(cfg ConfigStruct) Cache{
	c := &cacheImpl{}

	c.max_capacity = cfg.MaximumCacheCapacity

	if cfg.BackgroundCleanerInterval == 0{
		c.ticker = *time.NewTicker(2*time.Second)
	}else{
		c.ticker = *time.NewTicker(time.Duration(cfg.BackgroundCleanerInterval))
	}

	c.ctx,c.cancel = context.WithCancel(context.Background())
	c.cache = make(LRUMap, c.max_capacity)

	c.wg.Add(1)
	go worker(c)


	return c
}

	/* LRUMap exposes

	Get(key string) (string, bool)
	Put(key string, value string)
	Delete(key string)

	*/

// Get returns the value for a key and marks it as recently used.
    // Returns false if the key does not exist or has expired.
    // Expired items are lazily evicted on Get.
func (c *cacheImpl) Get(key string) (string, bool) {
	// background is running 
	// background does not change the order during reads
	// so reads dont need lock
	// bg worker uses locks when deleting

	// check if the cache is closed
	select{
	case <- c.ctx.isDone():
		return "", false
	
	default:
		c.mu.Lock() // wait for the workers write to be done
		defer c.mu.Unlock() 

		// manually check for expired keys by iterating through the map
			// why? only check for this key

		// if found then delete
		// else continue with the get
		val, ok := c.LRUMap.Get(key)
		if !ok {
			return "", false
		}else{
			if c.LRUMap[key].ttl < time.Now(){
				// delete the key aswell
				c.LRUMap.Delete(key)
				return "", false
			}else{
				return val, true
			}

		}
	}
	

}

// Put inserts or updates a key with a value and a TTL.
    // If the cache is full, the least recently used item is evicted first.
    // Resets the TTL if the key already exists.
func (c *cacheImpl) Put(key string, value string, ttl time.Duration) error{
	select{
	case <- c.ctx.isDone():
		return errors.new("cache is closed")
	
	default:
		c.mu.Lock() // wait for the workers write to be done
		defer c.mu.Unlock() 

		// manually check for expired keys by iterating through the map
			// why? only check for this key

		// if found then delete
		// else continue with the get
		err := c.LRUMap.Put(key, value)
		if err != nil {
			return errors.new("error while inserting")
		}else{
			return nil

		}
	}
}

// Len returns the number of items currently in the cache.
// Does not count expired items.
func (c *cacheImpl) Len() int{
	// handled by the LRUMAP to take care of count
	return c.cache.len()
}

func (c *cacheImpl) Close() error{
	
}

// for the worker
/*

we can have 2 implementations;
1. scan the entire list; store the expired keys; then remove them

2. scan; and remove as soon as you find

*/
package iteratorplusindex

import (
	"context"
	"sync"
)

/*

sure, a couple of clarifying questions

1. `Insert(id uint64, text string) error` takes in the id and the string associated with it to be stored in the table. but the index only stores the id and offset it is stored at in the table.
	Yes, that's correct. Insert receives the full text so your library can persist it to main storage and derive the offset. The index itself only needs to store (id → offset).

2. this offset can be the rowId?

3. Query returns an iterator over all records where Id > minId. what does this mean? do we have one iterator per record?
   1. or if we have n fragmented sets in the table then we return n iterators?
   2. or something else?


   The iterator is a cursor. The caller drives it forward with HasNext() / Next() until they've consumed what they want or hit the limit. What does that tell you about what state the iterator needs to hold?


	the table will be stored in a file
	- each record will be a line

	for the index; if we do map[rowId]lineNUmber
		we have to scan the entire taable to find the values that satisfy; no point of the index

	index should be on the key being used to query
	orderedMap[Id]lineNumber

	if index returns line number: iterator will have to jump to the line : O(n)
	instead if it returns te byteoffset it is O(1)
	orderedMapp[Id]byteOffset = index

	so index will return a list of byte offsets that satisfy a query.
	now iterator just needs to hold these byte offsets in a list, and read them from the file whenever hasnext or next is called.

	thsi way iterator just stores a slice of byteoffset

	since we need to close the iterator, it can have a bool finished, or a done channel which is closed when the close function is called. this can be assocaietd with a context stored in teh iterator. same for the index

	the iterator also needs to store a index pointing to the last element returned by the iterator.

	But think about the access pattern. When two goroutines are reading from different offsets simultaneously, do they need to lock each other out?

	* no they dont, which is solved by per offset mutex

	the writes on the file are just being appended, but modifying the index requires restructugin since it is always ordered. this means we cannot allow any reads with a write. so lets have a RWMutex which also concurrent reads. but blocks when writes are going to happen

	Where exactly do you acquire the read lock, and where do you release it?

	So can a reader and a writer ever be touching the same byte offset simultaneously? yeah the file does not need any locking; the reader cannot read a row that does not exist/being written to

	* Where exactly do you acquire the read lock, and where do you release it?
	* when the iterator is going to read the line, the Next() function
		* wrong, iterator reads from the file, the lock is for the index. meaning the read lock is acquired during the query function, once the iterator is populated we return the lock
	* Where do you acquire the write lock, and where do you release it?
	* during the insert() query
	* the read locks need to be held only during the index traversal (for the reason mentioned above)

	Does the iterator see that new record? Should it?
	no it should not, we should only read the values that are present during the query being run;


	And separately — in `Insert()`, you have two things happening:

	1. Writing the Text to the file
	2. Inserting the `(id → offset)` into the BST

	first we update the file, and then we update the index; otherwise we can update teh index; release the lock and a read might read the offset with an empty line/garbage value if the line has npt been written yet


	so we need the lock after the write to file finishes and before the write to index starts

	You said the file needs no locking because writes are appends and readers can't touch the same offset. But now consider this: two goroutines call Insert() simultaneously. Both try to append to the file at the same time.
		- cannot assume that writes are atomic; if atomic then no issue
		-else A separate mutex for file appends solves both problems — it serializes the appends and ensures each goroutine knows exactly what offset its record landed at after the write returns.

	insert:

		lock the file; append; unlock the file; locj the index; update index; unlock the index

	query:
		lock the index; return the list of offset; unlock the index

*/

// Index manages the in-memory index over the table.
type Index interface {
    // Insert adds a new record to the index.
    // The index stores only the Id and a pointer/offset to
    // where the Text lives in main storage — not the Text itself.
    // Must be safe to call concurrently with reads.
    Insert(id uint64, text string) error

    // Query returns an iterator over all records where Id > minId.
    // Results are limited to `limit` records.
    // Multiple goroutines can call Query simultaneously.
    Query(minId uint64, limit int) Iterator

    // Close shuts down the index and releases all resources.
    Close() error
}

// Iterator is a cursor over a query result set.
// Each goroutine owns its own iterator instance — not goroutine-safe.
// Fetches one record at a time from main storage using offsets from the index.
type Iterator interface {
    // HasNext returns true if there are more records to fetch.
    HasNext() bool

    // Next returns the next matching record and advances the cursor.
    // Fetches the full Text from main storage using the stored offset.
    // Returns an error if the record cannot be fetched.
    Next() (Record, error)

    // Close releases the iterator's resources.
    // Must be called when the caller is done iterating.
    Close() error
}

// Record is a single row returned by the iterator.
type Record struct {
    Id   uint64
    Text string
}

type fileData struct{
	fileName string
	mu sync.Mutex // to writes 
}

func newFileData (fname string) *fileData{
	f := &fileData{}
	f.fileName = fname

	return f
}

// we are also assuming that writing to the file; whci is ope -> seek -> read -> close. does not close the file; so that we dont have to open it again and again

type fileOffset uint64
type orderedMap map[uint64]fileOffset

type indexImpl struct{
	// assume that orderedMap is a ordered_map like in cpp
	indexMap orderedMap

	// what file is this index associated to
	dbFileData *fileData

	// needs a lock
	mu sync.RWMutex

	// needs a waitgroup for the processing using this; for the close function wait for
	wg sync.WaitGroup

	// a contxt and a cancel func; to stop taking in new orders
	ctx context.Context
	cancel context.CancelFunc

}

func newIndexImpl(fData *fileData) *indexImpl{
	i := &indexImpl{}
	i.dbFileData = fData
	i.indexMap = make(orderedMap)
	i.ctx, i.cancel = context.WithCancel(context.Background())

	return i
}

type iteratorImpl struct{
	// contains a slice of byte offsets retured by index
	fileOffsets []fileOffset

	// an index pointing to the last element of the iterator that was read
	lastReadIndex int

	// similaryl a close implementation
	// note that there is no concurrent work in the iterator
	isClosed bool

	// also needs to hold the record fetched by Next()
	// dont need this
	// return value of the Next() is the last read record

	// we need to actually open the file and read from it
	dbFileData *fileData
	// we dont need any locks to read


}

func newIteratorImpl (fdata *fileData) *iteratorImpl{
	i := &iteratorImpl{}
	i.dbFileData = fdata
	i.fileOffsets = make([]fileOffset, 0)
	i.isClosed = bool(false)

	return i
}


// before that, ideally i should implement some error variables. like so

// ```
// var (
//   errIndexClosed = errors.new("the index has been closed)
//   ....
// )
// ```

// but in the interest of time (the inteview is only supposed to be for a fixed duration), i will skip these and just use them as and when they come up, is that okay?


func (i *indexImpl) Insert(id uint64, text string) error{
	// gets the id and string

	// lock the file; append; unlock the file; locj the index; update index; unlock the index
	
	// open the file
	// assume the file_descriptor gives the last line where we can write

	// before accepting the request, check if the done channel is called
	select{
	case <- i.ctx.Done():
		return errors.new("not accepting anymore inserts, the index is closed")
	
	default:
		i.wg.Add(1)
		defer i.wg.Done()
		file_descriptor, err := open(i.dbFileData.fileName)
		if(err != nil) return errors.new("error opening the file")

		// writeers lock
		i.dbFileData.mu.Lock()

		r := &Record{}
		r.id = id
		r.text = text

		// append to the file
		byte_offset, err := file_descriptor.writeData(r)
		if(err != nil) {
			i.dbFileData.mu.Unlock()
			return errors.new("error writing to the file")
		}

		i.dbFileData.mu.Unlock()

		// now update teh index
		i.mu.Lock()
		defer i.mu.Unlock()

		i.indexMap[id] = byte_offset

		return nil
	}
	
}

func (i *indexImpl) Query(minId uint64, limit int) Iterator {
	// before accepting the request, check if the context is cancelled
	select {
	case <-i.ctx.Done():
		// index is closed — return an empty iterator
		// caller will see HasNext() = false immediately
		return newIteratorImpl(i.dbFileData)
	default:
		i.wg.Add(1)
		defer i.wg.Done()

		// read lock — held only during index traversal
		// multiple Query calls can run simultaneously
		// released before returning — iterator reads directly from file, no lock needed
		i.mu.RLock()
		defer i.mu.RUnlock()

		offsets := make([]fileOffset, 0)
		count := 0

		// traverse the ordered map in ascending Id order
		// since orderedMap is assumed ordered, iteration is in key order
		// we start from the first key > minId and collect up to limit offsets
		for id, offset := range i.indexMap {
			if id > minId {
				offsets = append(offsets, offset)
				count++
			}
			if count >= limit {
				break
			}
		}

		// snapshot of offsets at query time
		// any Insert() that happens after this point is not visible to this iterator
		iter := newIteratorImpl(i.dbFileData)
		iter.fileOffsets = offsets

		return iter
	}
}

func (i *indexImpl) Close() error {
	// stop accepting new Insert and Query calls
	i.cancel()

	// wait for all in-flight operations to complete before returning
	i.wg.Wait()

	return nil
}

func (i *iteratorImpl) HasNext() bool {
	if i.isClosed {
		return false
	}
	// true if there are still offsets we have not read yet
	return i.lastReadIndex < len(i.fileOffsets)
}

func (i *iteratorImpl) Next() (Record, error) {
	if i.isClosed {
		return Record{}, errors.New("iterator is closed")
	}

	if !i.HasNext() {
		return Record{}, errors.New("no more records")
	}

	// get the offset at the current cursor position
	offset := i.fileOffsets[i.lastReadIndex]

	// advance the cursor before the read
	// if the read fails, cursor is still advanced — caller should check error
	i.lastReadIndex++

	// open the file and read from the exact byte offset
	// no locking needed — this offset was written before the query ran
	// no concurrent writer can touch this offset
	file_descriptor, err := open(i.dbFileData.fileName)
	if err != nil {
		return Record{}, errors.New("error opening the file")
	}

	// read the record at the byte offset
	r, err := file_descriptor.readAt(offset)
	if err != nil {
		return Record{}, errors.New("error reading from the file")
	}

	return *r, nil
}

func (i *iteratorImpl) Close() error {
	// mark the iterator as closed
	// no concurrent work in the iterator so no lock needed
	i.isClosed = true
	return nil
}
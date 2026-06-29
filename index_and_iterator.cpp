#include <fstream>
#include <shared_mutex>
#include <mutex>          // For std::unique_lock
#include <map>
#include <memory>
#include <iostream>
#include <vector>
using namespace std;
/*
## The Problem

You are building an indexing library for a database engine. The table has the following schema:

```
Id   uint64
Text string  // averages 5KB per record
```

The total table size is 100GB. Your library must build an in-memory index over this table and expose an iterator interface for querying it.

**Constraints:**
- The index must not exceed 50GB in memory
- The index must support concurrent reads from multiple goroutines simultaneously
- The index must support writes — new records can be inserted while reads are in progress
- Query: `SELECT * FROM table WHERE Id > X LIMIT Y`
*/

// ======= CLARIFYING QUESTIONS =========
/*
1. bounded or unbounded?
- we can accept any number of incomiing queries

2. blocking on contention
- if a thread has to wait to complete, it will. instead of returning a COULD_NOT_FINISH_OPERATION

3. it will be thread-safe
*/

// ============ PUBLIC INTERFACE =============
struct Record{
    uint64_t id;   // for id > 20M
    string value;
};


// Iterator is a cursor over a query result set.
// Each thread owns its own iterator instance — not thread-safe.
// Fetches one record at a time from main storage using offsets from the index.
class iteratorInterface{
    public:
        virtual bool hasNext() = 0; // return true if there are more records to fetch

        virtual Record next() = 0;

        
        virtual ~iteratorInterface() = default;
};


class indexInterface{
    public:
        // want to insert a value
        virtual void insert(uint64_t id, const string& value) = 0;

        // want to query a value
        // Query returns an iterator over all records where Id > minId.
        // Results are limited to `limit` records.
        // Multiple goroutines can call Query simultaneously.
        // virtual iteratorInterface query(int minId, int limit) = 0; wont compile, cant return abstract classes
        virtual std::unique_ptr<iteratorInterface> query(uint64_t minId, int limit) = 0;
        // want to close the index : done by the destructor. Unlike go we dont have to implement a close() function
        virtual ~indexInterface() = default;
};

// ========= IMPLEMENTING THE INTERFACE ================

/*
design :
the table is a file; each line contains a record {id, value}

since the wueries are only inserts and fetch, we are not updating anything. the query will return a list of file offsets which store the records
the uterator will process these offsets and return the actual record. 
we dont want what the iterator is pointing to to move. so keep the table append only

index : unordered_map<id, file_offset>
query will return all the ids that fit the criteria
    - ordered_map : inserts will be O(logn) but reading will be mich faster
    - unordered_map : inserts will be O(1) but reading might take a while to find the values which exist

    based on the query; it is a range scan -> ordered_map

    in an ideal engine would have a B+ tree for this index. B+ tree — many keys per node. Cache-friendly. Leaves are linked, so the range walk after finding X is a straight leaf scan. What real engines use.
the iterator will receive a list of offsets, and then return the vlaue at that offset when asked. 


one entry in index : id + offset = 16 bytes. with the map overhead of pointers + color bit say about 50 bytes. 

if we have 20 million entries : 20 * 10^6; muktiple by size of each (*50) = 1000 * 10^6 = 1gb, fits the criteria correctly. 

if we stored the text directly then it wouldve been over the size. which we are not doing
*/

/*
The map is not thread safe; one write can rehash/collisoins can happen. so we need a lock for it. This is how the thre read and write flows would work

WRITE
- we get the insert query
- get a lock on the file (only one write at a time)
    - write it in the file
- unlock the file
- lock the map
    - update the map
- unlock the map
    it is possible that someone writes to the file and before wrioting to the map, the map is updted by another thread. 
    this is a problem if we are storing the current offset to write to in the file and updating that after the right

- so one lock for file + map write then unlock

READ
- lock the map for reading (no writes happeninng here)
- find all the file offsets and store them appropraitely
- unlock
- the iterator picks this list up
- traverses as asked byt eh client. no lock needed here. it is client specific

writing to file first is important. if we update map first -> befor file write teh reader can take this offset to be read from (even though nothing is written)


fixing the offset flow
- can have a shared offset 
    - get the lock for the file; write to offset; store the current offset to return; and then increment the offset
    - then unlock the file write
    - lock the map; write id : file_offset stored to return

since both write and read lock the map; lets use a shared_mutex -> unique_lock for writes and shared_lock for reads. allows multiple reads at the same time. only one writer at a timme
*/

std::ostream& operator<<(std::ostream& os, const Record& r) {
    os << r.id << '\t' << r.value;   // breaks if value contains '\t' or '\n'
    return os;
}

class iteratorImpl : public iteratorInterface{
    private:
        // have a list of offset returned by the indexImpl
        // have a stram to the file (predefined name) owned by the class
        // no need for read lock on the file (not interacting with the writes)
        vector<off_t> offsets;

        // need a currentIndex in the offsets vector
        size_t currentIndex;

        string fileName;
        std::ifstream inFile;

    public:
        iteratorImpl(const vector<off_t>& scannedOffsets) : offsets(scannedOffsets), currentIndex(0) {
            fileName = "index_and_iterator_example_table.txt";
            // Open a separate file handle for this specific iterator instance
            inFile.open(fileName, std::ios::in | std::ios::binary);

            if(!inFile){
                throw runtime_error ("iterator could not open file to read");
            }
            return;
        }
        bool hasNext() override{
            return currentIndex < offsets.size() && inFile.is_open();
        }

        Record next() override{
            if (!hasNext()) {
                throw std::out_of_range("No more elements in the iterator");
            }

            off_t targetOffset = offsets[currentIndex++];

            // Seek directly to the record's byte offset in the file
            inFile.seekg(targetOffset);

            Record record;
            // Parse the exact output format specified by your operator<<: "id\tvalue\n"
            if (inFile >> record.id) {
                char tab;
                inFile.get(tab); // Eat the delimiter character '\t'
                std::getline(inFile, record.value); // Grab the rest of the line as string value
            }

            return record;
        }

};

class indexImpl : public indexInterface{
    private: 
        // we need a map like we had disscussed
        // we also need a file_name to point to the file where the table is stored
            // this can just be a random file_name for now. in a sophisticated program it cna be set during the init phase

        // need a mutex (shared mutex)
        // a unique lock (for writers)
        // a shared lock (for readers)
        map<uint64_t, off_t> idToOffsetMap;
        std::mutex fileMutex; // no need for readers lock. readers will only read from lines that done interact with writers
        std::shared_mutex mapMutex; // both readers and writers
        // off_t currentFileOffset; // to return after the write to add in the map
        string fileName;
        std::ofstream outFile; // the file stream handler

    public:
        indexImpl(){
            fileName = "index_and_iterator_example_table.txt";
            // currentFileOffset = 0;

            // open the file to append
            outFile.open(fileName, std::ios::out | std::ios::app | std::ios::binary);

            if (!outFile) {
                // Handle file open error (optional but good practice)
                throw runtime_error("could not open file to store the table");
            }
        }

        void insert(uint64_t id, const string& value) override{
            // add to the file
            off_t currenOffset;
            {
                unique_lock<mutex> fileLock(fileMutex);

                // get the currentOffset
                currenOffset = outFile.tellp();

                // write the data
                Record curr_record = {id, value};
                outFile << curr_record << "\n"; // working on newline to find might might be risky. ideally should be some sort of length prefix

                outFile.flush(); // flushing per insert to guarantee bytes are on disk before the offset is published; in a real engine I'd batch/group-commit.
            }

            // update the map
            {
                unique_lock<shared_mutex> lock(mapMutex);
                idToOffsetMap[id] = currenOffset;
            }
            return;
            
        }

        std::unique_ptr<iteratorInterface> query(uint64_t minId, int limit) override{
            // read lock the map
            // find the initial record (may or maynot exist)
            // find records from here till the limit is satisfied
            // return appropraitely (return list of offsets
            // iterator owns its own ifstream on the file — yes?)

            // cannot implement until we implement the itreator interface

            std::vector<off_t> collectedOffsets;

            {
                std::shared_lock<std::shared_mutex> lock(mapMutex);
                // 2. Find the first key strictly greater than minId
                // Since std::map is a balanced BST, this is highly efficient: O(log N)
                auto it = idToOffsetMap.upper_bound(minId);

                // 3. Iterate through the map until the limit or end of map is reached
                while (it != idToOffsetMap.end() && collectedOffsets.size() < static_cast<size_t>(limit)) {
                    collectedOffsets.push_back(it->second);
                    ++it;
                }
            }

            return make_unique<iteratorImpl>(std::move(collectedOffsets));

        }        
};

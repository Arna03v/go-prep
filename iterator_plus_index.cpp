// index.cpp — C++17 migration of the Go index + iterator.
//
// Data layout preserved from the Go version:
//   - index stores  std::map<uint64_t, FileOffset>   (id -> byte offset)
//   - iterator stores  std::vector<FileOffset> + cursor + closed + FileData*
//   - records stored in the file as lines:  "<id> <text>\n"
//     offset points at the start of the line.
//
// Ownership / lifetime (raw pointers, caller-owned):
//   - makeIndex() returns Index*, caller deletes it.
//   - Query() returns Iterator*, caller deletes it.
//   - An Iterator borrows the index's FileData (fd) by raw pointer.
//     ==> Do NOT use or delete an Iterator after deleting its Index.
//
// Concurrency:
//   - writeMu : serializes file appends (one std::mutex).
//   - idxMu   : guards the map (std::shared_mutex; shared in Query, unique in Insert).
//   - lifeMu+cv+inFlight+closed : lifecycle, replaces Go's ctx + WaitGroup.
//   These three locks are never held nested -> no deadlock.
//   pread in the iterator is positional and lockless.

#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

using FileOffset = uint64_t;

struct Record {
    uint64_t    id;
    std::string text;
};

// ---- interfaces -----------------------------------------------------------

class Iterator {
public:
    virtual ~Iterator() = default;
    virtual bool HasNext() = 0;
    virtual bool Next(Record& out) = 0;   // true = out filled; false = done-or-error
    virtual void Close() = 0;
};

class Index {
public:
    virtual ~Index() = default;
    virtual int       Insert(uint64_t id, std::string text) = 0; // 0 ok / nonzero err
    virtual Iterator* Query(uint64_t minId, int limit) = 0;      // always non-null
    virtual int       Close() = 0;
};

// ---- shared file handle ---------------------------------------------------

struct FileData {
    int      fd = -1;
    std::mutex writeMu;
    uint64_t writeOffset = 0;   // current end of file = next append position

    explicit FileData(const std::string& path) {
        fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644); // starts empty
        if (fd < 0) std::cerr << "error opening file: " << std::strerror(errno) << "\n";
    }
    ~FileData() { if (fd >= 0) ::close(fd); }
};

// ---- iterator -------------------------------------------------------------

class iteratorImpl : public Iterator {
public:
    std::vector<FileOffset> fileOffsets;
    size_t    cursor = 0;
    bool      closed = false;
    FileData* fileData = nullptr;   // borrowed, not owned

    explicit iteratorImpl(FileData* fd) : fileData(fd) {}

    bool HasNext() override {
        if (closed) return false;
        return cursor < fileOffsets.size();
    }

    bool Next(Record& out) override {
        if (closed) { std::cerr << "iterator is closed\n"; return false; }
        if (!HasNext()) return false;            // clean end of stream

        FileOffset offset = fileOffsets[cursor];
        ++cursor;                                // advance before read
        return readRecordAt(offset, out);
    }

    void Close() override { closed = true; }     // no concurrency in the iterator

private:
    // Lockless positional read: pread does not move the fd offset, safe across threads.
    bool readRecordAt(FileOffset offset, Record& out) {
        std::string buf;
        char chunk[256];
        FileOffset pos = offset;
        size_t nl = std::string::npos;

        while (true) {
            ssize_t n = ::pread(fileData->fd, chunk, sizeof(chunk), pos);
            if (n < 0) { std::cerr << "error reading from file\n"; return false; }
            if (n == 0) break;                   // EOF before newline
            buf.append(chunk, static_cast<size_t>(n));
            nl = buf.find('\n');
            if (nl != std::string::npos) break;
            pos += static_cast<FileOffset>(n);
        }
        if (nl == std::string::npos) { std::cerr << "malformed record (no newline)\n"; return false; }

        std::string line = buf.substr(0, nl);
        size_t sp = line.find(' ');
        if (sp == std::string::npos) { std::cerr << "malformed record (no space)\n"; return false; }

        out.id   = std::stoull(line.substr(0, sp));
        out.text = line.substr(sp + 1);
        return true;
    }
};

// ---- index ----------------------------------------------------------------

class indexImpl : public Index {
public:
    explicit indexImpl(const std::string& path) : fileData(path) {}

    int Insert(uint64_t id, std::string text) override {
        if (!enter()) { std::cerr << "index closed: rejecting insert\n"; return 1; }
        InFlightGuard g(this);

        std::string line = std::to_string(id) + " " + text + "\n";

        // 1) append to file, learn the landing offset  (writeMu only)
        uint64_t offset;
        {
            std::lock_guard<std::mutex> wlk(fileData.writeMu);
            offset = fileData.writeOffset;
            ssize_t n = ::pwrite(fileData.fd, line.data(), line.size(), offset);
            if (n < 0 || static_cast<size_t>(n) != line.size()) {
                std::cerr << "error writing to file\n";
                return 2;
            }
            fileData.writeOffset += line.size();
        }

        // 2) only then update the index  (idxMu unique)
        {
            std::unique_lock<std::shared_mutex> ilk(idxMu);
            indexMap[id] = offset;
        }
        return 0;
    }

    Iterator* Query(uint64_t minId, int limit) override {
        if (!enter()) return new iteratorImpl(&fileData);  // empty: HasNext() == false
        InFlightGuard g(this);

        auto* iter = new iteratorImpl(&fileData);
        {
            std::shared_lock<std::shared_mutex> ilk(idxMu);   // held only during traversal
            int count = 0;
            for (auto it = indexMap.upper_bound(minId);       // first id > minId, ordered
                 it != indexMap.end() && count < limit; ++it, ++count) {
                iter->fileOffsets.push_back(it->second);      // snapshot at query time
            }
        }
        return iter;
    }

    int Close() override {
        std::unique_lock<std::mutex> lk(lifeMu);
        closed = true;
        cv.wait(lk, [this] { return inFlight == 0; });        // drain in-flight ops
        return 0;
    }

private:
    std::map<uint64_t, FileOffset> indexMap;
    FileData                       fileData;       // owned by-value; iterators borrow &fileData
    std::shared_mutex              idxMu;

    // lifecycle (replaces ctx + WaitGroup); check-and-increment under one lock
    std::mutex              lifeMu;
    std::condition_variable cv;
    int                     inFlight = 0;
    bool                    closed   = false;

    bool enter() {
        std::lock_guard<std::mutex> lk(lifeMu);
        if (closed) return false;
        ++inFlight;
        return true;
    }
    void leave() {
        std::lock_guard<std::mutex> lk(lifeMu);
        if (--inFlight == 0) cv.notify_all();
    }
    struct InFlightGuard {
        indexImpl* idx;
        explicit InFlightGuard(indexImpl* i) : idx(i) {}
        ~InFlightGuard() { idx->leave(); }
    };
};

Index* makeIndex(const std::string& path) { return new indexImpl(path); }

// ---- demo / smoke test ----------------------------------------------------

#include <thread>

int main() {
    Index* idx = makeIndex("/tmp/index_table.db");

    // concurrent inserts
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([idx, t] {
            for (int k = 0; k < 5; ++k) {
                uint64_t id = static_cast<uint64_t>(t * 5 + k);
                idx->Insert(id, "row-" + std::to_string(id));
            }
        });
    }
    for (auto& th : ts) th.join();

    // query id > 7, limit 100
    std::cout << "records with id > 7:\n";
    Iterator* it = idx->Query(/*minId=*/7, /*limit=*/100);
    Record r;
    while (it->HasNext()) {
        if (it->Next(r)) std::cout << "  id=" << r.id << " text=" << r.text << "\n";
    }
    it->Close();
    delete it;            // delete iterator BEFORE index (it borrows the fd)

    idx->Close();
    delete idx;
    return 0;
}
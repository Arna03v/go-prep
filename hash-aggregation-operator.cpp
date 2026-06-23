/*
a hash aggregation operator. 
Input is a stream of rows (key, value); output is one row per distinct key with its aggregate(s). Build it interface-first in C++
*/

#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// ---------- the aggregate interface (exposed for extension) ----------
class Aggregate {
public:
    // define destructor
        // a virtaul function may or maynot have an implementation and the child class can override its implementation
        // default means that the compiler generates the body for the functions; used for constructors, destructors etc
    virtual ~Aggregate() = default;

    // = 0 means the child class MUST override this function; and has no implementation in the current class
        // class becomes abstract and cannot be instantiated
    virtual void   accumulate(double value) = 0;          // fold one value into state
    virtual double finalize() const = 0;                  // produce the final number
    virtual std::unique_ptr<Aggregate> clone() const = 0; // mint a fresh empty one
};

// ---------- concrete aggregates ----------
class CountAgg : public Aggregate {
    uint64_t count = 0;
public:
    void   accumulate(double /*value*/) override { count++; }
    double finalize() const override { return static_cast<double>(count); }
    std::unique_ptr<Aggregate> clone() const override {
        return std::make_unique<CountAgg>();
    }
};

class SumAgg : public Aggregate {
    double sum = 0.0;
public:
    void   accumulate(double value) override { sum += value; }
    double finalize() const override { return sum; }
    std::unique_ptr<Aggregate> clone() const override {
        return std::make_unique<SumAgg>();
    }
};

class AvgAgg : public Aggregate {
    double   sum   = 0.0;
    uint64_t count = 0;
public:
    void   accumulate(double value) override { sum += value; count++; }
    double finalize() const override { return count ? sum / count : 0.0; }
    std::unique_ptr<Aggregate> clone() const override {
        return std::make_unique<AvgAgg>();
    }
};

// ---------- result row ----------
struct ResultRow {
    std::string key;
    double      result;
};

// ---------- the operator (interface exposed to the user) ----------
class HashAggregator {
    std::unique_ptr<Aggregate> prototype;                                  // template per group
    std::unordered_map<std::string, std::unique_ptr<Aggregate>> table;     // key -> accumulator

public:
    explicit HashAggregator(std::unique_ptr<Aggregate> proto)
        : prototype(std::move(proto)) {}

    // feed one row
    void addRow(const std::string& key, double value) {
        auto it = table.find(key);
        if (it == table.end()) {
            it = table.emplace(key, prototype->clone()).first;  // new group -> fresh accumulator
        }
        it->second->accumulate(value);
    }

    // pull final results, one row per distinct group
    std::vector<ResultRow> getResults() const {
        std::vector<ResultRow> out;
        out.reserve(table.size());
        for (const auto& [key, acc] : table) {
            out.push_back({key, acc->finalize()});
        }
        return out;
    }
};
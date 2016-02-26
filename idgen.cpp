#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <set>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cassert>

/**
 * \brief   Generates unique monotonically increasing ids.
 *          Allows better multithread performance due to simple synchronization but does not allow to reuse ids
 */
class SeqIdGenerator
{
public:

    typedef uint64_t id_t;

    SeqIdGenerator() : m_id(0) {}

    // std::atomic increment for integer types should resolve to whatever locked xchg instruction is supported for this platform
    // f0 48 0f c1 10    lock xadd %rdx,(%rax)
    id_t next() {
        return m_id++;
    }

private:
    std::atomic<id_t> m_id;
};


/**
 * \brief   Non-sequential ID generator that reuses released ids.
 *          Worse multi-threaded performance compared to SeqIdGenerator but allows reuse of IDs
 *
 * Implementation stores released ids in a simple queue.
 * Thus it is optimized for usecase when next() and put() are called interchargebly across execution.
 *
 * Alternative implementations may use free id ranges stored in a search heap
 * to optimize memory footprints for workloads that allocate and release ids in chunks.
 */
class IdGenerator
{
public:

    typedef uint64_t id_t;

    IdGenerator() : m_id(0) {}

    id_t next() {
        // Check for available min id in free queue
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (!m_free_queue.empty()) {
                id_t id = m_free_queue.front();
                m_free_queue.pop();
                return id;
            }
        }

        // No elements in free queue, return next available
        return m_id++;
    }

    void put(id_t id) {
        std::lock_guard<std::mutex> lock(m_lock);
        m_free_queue.push(id);
    }

private:
    std::atomic<id_t> m_id;             // Next sequential id to return
    std::queue<id_t> m_free_queue;      // Released ids queue
    std::mutex m_lock;                  // Serializes queue access
};

////////////////////////////////////////////////////////////////////////////////

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>

namespace
{
    enum {
        kTotalTestIds = 1000,
    };
}

// Tests generic id generator that only supports next() call
template < typename T >
void GeneridGeneratorTest()
{
    T gen;

    std::vector<typename T::id_t> ids1;
    std::vector<typename T::id_t> ids2;

    std::thread t1([&]() {
        for (int i = 0; i < kTotalTestIds; ++i) {
            id_t id = gen.next();
            ids1.push_back(id);
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < kTotalTestIds; ++i) {
            id_t id = gen.next();
            ids2.push_back(id);
        }
    });

    t1.join();
    t2.join();

    // Both sequences should be sorted in acending order, by genereator definition
    BOOST_CHECK(std::is_sorted(ids1.begin(), ids1.end()));
    BOOST_CHECK(std::is_sorted(ids2.begin(), ids2.end()));

    // Merge sequences into set to see if all values are unique
    std::set<typename T::id_t> set;
    std::merge(ids1.begin(), ids1.end(), ids2.begin(), ids2.end(), std::inserter(set, set.begin()));

    BOOST_CHECK_EQUAL(set.size(), ids1.size() + ids2.size());
    BOOST_CHECK_EQUAL(0, *std::min_element(set.begin(), set.end()));
    BOOST_CHECK_EQUAL(kTotalTestIds * 2 - 1, *std::max_element(set.begin(), set.end()));
}

BOOST_AUTO_TEST_CASE(SeqIdGeneratorTest)
{
    GeneridGeneratorTest<SeqIdGenerator>();
}

BOOST_AUTO_TEST_CASE(IdGeneratorTest)
{
    GeneridGeneratorTest<IdGenerator>();
}

// Additional test with reusing ids
BOOST_AUTO_TEST_CASE(IdGeneratorSparseTest)
{
    IdGenerator gen;

    std::mutex lock;
    std::condition_variable cond;
    std::queue<IdGenerator::id_t> q;

    // One thread will generate ids into a shared queue
    std::thread producer([&]() {
        for (int i = 0; i < kTotalTestIds; ++i) {
            std::unique_lock<std::mutex> lock_guard(lock);
            q.push(gen.next());
	    lock_guard.unlock();
            cond.notify_one();
        }
    });

    // The other thread will remove generated ids from the queue and return them to generator, until it removes hals as many ids
    std::thread consumer([&]() {
        for (int i = 0; i < (kTotalTestIds / 2); ++i) {
            std::unique_lock<std::mutex> lock_guard(lock);
            while (q.empty()) {
                cond.wait(lock_guard);
            }

            id_t id = q.front();
            q.pop();
            gen.put(id);
        }
    });

    producer.join();
    consumer.join();

    // What we have now is a queue which contains (kTotalTestIds / 2) elements, maximum possible value should be kTotalTestIds
    // Let us regenerate remaining (kTotalTestIds / 2) ids and check that we get ids in sequential range [0..kTotalTestIds)
    BOOST_CHECK_EQUAL(q.size(), (kTotalTestIds / 2));
    for (int i = 0; i < (kTotalTestIds / 2); ++i) {
        q.push(gen.next());
    }

    std::set<IdGenerator::id_t> set;
    while (!q.empty()) {
        id_t id = q.front();
        q.pop();
        set.insert(id);
    }

    BOOST_CHECK_EQUAL(set.size(), kTotalTestIds);
    BOOST_CHECK_EQUAL(0, *std::min_element(set.begin(), set.end()));
    BOOST_CHECK_EQUAL(kTotalTestIds - 1, *std::max_element(set.begin(), set.end()));
}

////////////////////////////////////////////////////////////////////////////////

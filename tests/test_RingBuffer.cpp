#include <gtest/gtest.h>
#include "util/RingBuffer.h"
#include <thread>
#include <vector>

using namespace yawn::util;

TEST(RingBufferTest, EmptyOnConstruction) {
    RingBuffer<int, 16> rb;
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);
}

TEST(RingBufferTest, PushAndPop) {
    RingBuffer<int, 16> rb;
    EXPECT_TRUE(rb.push(42));
    EXPECT_FALSE(rb.empty());
    EXPECT_EQ(rb.size(), 1u);

    int val = 0;
    EXPECT_TRUE(rb.pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferTest, PopFromEmptyReturnsFalse) {
    RingBuffer<int, 16> rb;
    int val = 0;
    EXPECT_FALSE(rb.pop(val));
}

TEST(RingBufferTest, FillToCapacity) {
    // Capacity 8 means 7 usable slots (one slot reserved for full/empty distinction)
    RingBuffer<int, 8> rb;
    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(rb.push(i)) << "push failed at i=" << i;
    }
    EXPECT_EQ(rb.size(), 7u);
    EXPECT_FALSE(rb.push(999)); // should fail — full
}

TEST(RingBufferTest, FIFO_Order) {
    RingBuffer<int, 16> rb;
    for (int i = 0; i < 10; ++i) {
        rb.push(i);
    }
    for (int i = 0; i < 10; ++i) {
        int val = -1;
        EXPECT_TRUE(rb.pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST(RingBufferTest, Wraparound) {
    RingBuffer<int, 4> rb;  // 3 usable slots
    rb.push(1); rb.push(2); rb.push(3);
    int v;
    rb.pop(v); rb.pop(v); // drain 2
    // Now head and tail have wrapped around
    EXPECT_TRUE(rb.push(4));
    EXPECT_TRUE(rb.push(5));
    
    rb.pop(v); EXPECT_EQ(v, 3);
    rb.pop(v); EXPECT_EQ(v, 4);
    rb.pop(v); EXPECT_EQ(v, 5);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferTest, Clear) {
    RingBuffer<int, 16> rb;
    rb.push(1); rb.push(2); rb.push(3);
    rb.clear();
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);

    int v;
    EXPECT_FALSE(rb.pop(v));
}

TEST(RingBufferTest, ConcurrentProducerConsumer) {
    constexpr int kCount = 100000;
    RingBuffer<int, 1024> rb;

    std::vector<int> received;
    received.reserve(kCount);

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            while (!rb.push(i)) {
                // spin
            }
        }
    });

    std::thread consumer([&] {
        int val;
        int count = 0;
        while (count < kCount) {
            if (rb.pop(val)) {
                received.push_back(val);
                count++;
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), static_cast<size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(received[i], i) << "mismatch at index " << i;
    }
}

struct TrivialStruct {
    int a;
    float b;
    char c;
};

TEST(RingBufferTest, WorksWithStructs) {
    RingBuffer<TrivialStruct, 8> rb;
    TrivialStruct in{10, 3.14f, 'x'};
    EXPECT_TRUE(rb.push(in));

    TrivialStruct out{};
    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.a, 10);
    EXPECT_FLOAT_EQ(out.b, 3.14f);
    EXPECT_EQ(out.c, 'x');
}

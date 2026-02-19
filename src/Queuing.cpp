#include "Queuing.h"

volatile KeyEvent queue[Q_SIZE];
volatile int head = 0;
volatile int tail = 0;
volatile std::uint32_t queueOverwriteCount = 0;

// Helper to add data (Called by USB Thread)
void enqueue(uint8_t key, uint8_t mod, uint32_t ts) {
    int next = (head + 1) % Q_SIZE;
    if (next == tail) {
        // Keep the freshest events when saturated.
        tail = (tail + 1) % Q_SIZE;
        ++queueOverwriteCount;
    }

    queue[head].key = key;
    queue[head].modifiers = mod;
    queue[head].timestamp = ts;
    head = next;
}

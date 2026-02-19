#ifndef QUEUING_H
#define QUEUING_H

#include <cstdint>

void enqueue(uint8_t key, uint8_t mod, uint32_t ts);

struct KeyEvent {
    uint8_t key;
    uint8_t modifiers;
    uint32_t timestamp;
};

const int Q_SIZE = 256;

extern volatile KeyEvent queue[Q_SIZE];
extern volatile int head;
extern volatile int tail;
extern volatile std::uint32_t queueOverwriteCount;

#endif

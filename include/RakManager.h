#ifndef RAK_MANAGER_H
#define RAK_MANAGER_H

#include <Arduino.h>
#include <TeensyThreads.h>

class RakManager {
public:
    static RakManager& getInstance();

    void begin();

private:
    RakManager() = default;

    static void listenerThread(void* arg);
};

#endif
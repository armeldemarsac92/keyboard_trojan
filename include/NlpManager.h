#ifndef NLPMANAGER_H
#define NLPMANAGER_H

#include <Arduino.h>
#include <TeensyThreads.h>

class NlpManager {
public:
    static NlpManager& getInstance();

    void begin();
    void analyzeSentence(String sentence);
    void setCallback(void (*callback)(String topic, float confidence)) {
        _callback = callback;
    }

    // 🔍 Debug function - call this to verify hash compatibility
    void debugHashVerification();
    void debugPrediction(String text); // New: detailed prediction debug

private:
    NlpManager() : _hasNewData(false), _callback(nullptr) {}
    NlpManager(const NlpManager&) = delete;
    NlpManager& operator=(const NlpManager&) = delete;

    static void processingThread(void* arg);
    int predict_topic(String text, float* out_confidence);

    // Core NLP functions
    static uint32_t murmurhash3_32(const char* key, uint32_t len, uint32_t seed);
    static float relu(float x);
    static float activation_func(float x); // Support both relu and tanh
    static void normalize_and_lower(char* str);
    void hashAndAccumulate(const char* token, float* hidden1, int weight);
    void softmax(float* values, int size);

    // Thread synchronization
    Threads::Mutex _mutex;
    volatile bool _hasNewData;
    String _inputBuffer;
    void (*_callback)(String topic, float confidence);
};

#endif // NLPMANAGER_H

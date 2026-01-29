#ifndef NLP_MANAGER_H
#define NLP_MANAGER_H

#include <Arduino.h>
#include <TeensyThreads.h>

/**
 * NlpManager - Neural Network-based NLP for Teensy 4.1
 *
 * Features:
 * - Multi-word context hashing (bigrams, trigrams)
 * - Character n-grams for typo tolerance
 * - Positional encoding for sentence structure
 * - Thread-safe operation
 * - Confidence scoring
 */
class NlpManager {
public:
    static NlpManager& getInstance();

    /**
     * Initialize the NLP manager and start processing thread
     */
    void begin();

    /**
     * Queue a sentence for analysis (thread-safe)
     * @param sentence The text to analyze
     */
    void analyzeSentence(String sentence);

private:
    NlpManager() : _hasNewData(false) {}
    NlpManager(const NlpManager&) = delete;
    NlpManager& operator=(const NlpManager&) = delete;

    // Thread-safe data exchange
    String _inputBuffer;
    bool _hasNewData;
    Threads::Mutex _mutex;

    /**
     * Background processing thread
     */
    static void processingThread(void* arg);

    /**
     * MurmurHash3 32-bit hash function
     * @param key Data to hash
     * @param len Length of data
     * @param seed Hash seed (default 0)
     * @return 32-bit hash value
     */
    static uint32_t murmurhash3_32(const char *key, uint32_t len, uint32_t seed);

    /**
     * ReLU activation function
     * @param x Input value
     * @return max(0, x)
     */
    static float relu(float x);

    /**
     * Hash a token and accumulate to hidden layer
     * @param token Token string to hash
     * @param hidden1 Hidden layer to accumulate to
     */
    void hashAndAccumulate(const char* token, float* hidden1);

    /**
     * Predict topic from text using neural network
     * @param text Input text
     * @param out_confidence Optional output parameter for confidence score
     * @return Index of predicted topic (use TOPICS[] to get label)
     */
    int predict_topic(String text, float* out_confidence = nullptr);
};

#endif // NLP_MANAGER_H
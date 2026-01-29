#include "NlpManager.h"
#include "ModelWeights.h"
#include <cstring>

NlpManager& NlpManager::getInstance() {
    static NlpManager instance;
    return instance;
}

void NlpManager::begin() {
    Serial.println("[AI] Initializing Neural Network...");
    Serial.print("[AI] Model: ");
    Serial.print(INPUT_SIZE);
    Serial.print(" -> ");
    Serial.print(HIDDEN1_SIZE);
    Serial.print(" -> ");
    Serial.print(HIDDEN2_SIZE);
    Serial.print(" -> ");
    Serial.println(OUTPUT_SIZE);
    
    // Launch thread with 16K stack (AI needs local RAM)
    threads.addThread(processingThread, this, 16384);
    
    Serial.println("[AI] Thread started.");
}

void NlpManager::analyzeSentence(String sentence) {
    if (sentence.length() < 3) return;
    
    Threads::Scope scope(_mutex);
    _inputBuffer = sentence;
    _hasNewData = true;
}

void NlpManager::processingThread(void* arg) {
    NlpManager* ai = (NlpManager*)arg;
    
    while (1) {
        String localCopy = "";
        bool workToDo = false;
        
        {
            Threads::Scope scope(ai->_mutex);
            if (ai->_hasNewData) {
                localCopy = ai->_inputBuffer;
                ai->_hasNewData = false;
                workToDo = true;
            }
        }
        
        if (workToDo) {
            unsigned long t0 = micros();
            float confidence = 0.0f;
            int topicIdx = ai->predict_topic(localCopy, &confidence);
            unsigned long t1 = micros();
            
            Serial.print("[AI] Detected: ");
            Serial.print(TOPICS[topicIdx]);
            Serial.print(" (confidence: ");
            Serial.print(confidence * 100.0f, 1);
            Serial.print("%, ");
            Serial.print(t1 - t0);
            Serial.println(" us)");
        }
        
        threads.yield();
    }
}

// MurmurHash3 32-bit hash function
uint32_t NlpManager::murmurhash3_32(const char *key, uint32_t len, uint32_t seed) {
    uint32_t c1 = 0xcc9e2d51;
    uint32_t c2 = 0x1b873593;
    uint32_t r1 = 15;
    uint32_t r2 = 13;
    uint32_t m = 5;
    uint32_t n = 0xe6546b64;
    uint32_t h = seed;
    uint32_t k = 0;

    // Process 4-byte chunks
    for (uint32_t i = 0; i < len / 4; i++) {
        k = ((uint32_t *)key)[i];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;
        h ^= k;
        h = (h << r2) | (h >> (32 - r2));
        h = h * m + n;
    }

    // Process remaining bytes
    k = 0;
    uint8_t *tail = (uint8_t *)(key + (len & ~3));
    switch (len & 3) {
        case 3: k ^= tail[2] << 16;
        case 2: k ^= tail[1] << 8;
        case 1: k ^= tail[0];
                k *= c1;
                k = (k << r1) | (k >> (32 - r1));
                k *= c2;
                h ^= k;
    }

    // Finalization
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
}

// ReLU activation function
float NlpManager::relu(float x) {
    return (x > 0.0f) ? x : 0.0f;
}

// Hash a token and accumulate to hidden layer
void NlpManager::hashAndAccumulate(const char* token, float* hidden1) {
    uint32_t h = murmurhash3_32(token, strlen(token), 0);
    int index = h % INPUT_SIZE;
    
    const float* w1_row = WEIGHTS_1 + (index * HIDDEN1_SIZE);
    for (int j = 0; j < HIDDEN1_SIZE; j++) {
        hidden1[j] += w1_row[j];
    }
}

// Main prediction function with multi-word hashing
int NlpManager::predict_topic(String text, float* out_confidence) {
    // Allocate buffers on stack (thread has 16K)
    float hidden1[HIDDEN1_SIZE];
    float hidden2[HIDDEN2_SIZE];
    float output[OUTPUT_SIZE];

    // --- LAYER 1: Initialize with bias ---
    memcpy(hidden1, BIAS_1, HIDDEN1_SIZE * sizeof(float));

    // --- PREPROCESSING ---
    text.toLowerCase();
    text.trim();
    
    // Convert to char array with length limit for safety
    const int MAX_TEXT_LEN = 512;
    int len = min(text.length(), MAX_TEXT_LEN);
    char buffer[MAX_TEXT_LEN + 1];
    text.toCharArray(buffer, len + 1);

    // --- TOKENIZE INTO WORDS ---
    const int MAX_WORDS = 15;  // Limit to match Python
    char* wordList[MAX_WORDS];
    int wordCount = 0;

    char* token = strtok(buffer, " ,.-!?;:()[]\"'\t\n\r");
    while (token != NULL && wordCount < MAX_WORDS) {
        wordList[wordCount++] = token;
        token = strtok(NULL, " ,.-!?;:()[]\"'\t\n\r");
    }

    // If no words found, return default
    if (wordCount == 0) {
        if (out_confidence) *out_confidence = 0.0f;
        return 0;
    }

    // --- LAYER 1.1: CHARACTER N-GRAMS (2-4 chars) ---
    for (int w = 0; w < wordCount; w++) {
        char paddedToken[128];
        snprintf(paddedToken, sizeof(paddedToken), "<%s>", wordList[w]);
        int paddedLen = strlen(paddedToken);

        // Variable length n-grams (2-4 characters)
        for (int n = 2; n <= 4 && n <= paddedLen; n++) {
            for (int i = 0; i <= paddedLen - n; i++) {
                char ngram[8];
                memcpy(ngram, &paddedToken[i], n);
                ngram[n] = '\0';
                
                hashAndAccumulate(ngram, hidden1);
            }
        }
    }

    // --- LAYER 1.2: WORD BIGRAMS ---
    for (int i = 0; i < wordCount - 1; i++) {
        char bigram[256];
        snprintf(bigram, sizeof(bigram), "W2:%s_%s", wordList[i], wordList[i + 1]);
        hashAndAccumulate(bigram, hidden1);
    }

    // --- LAYER 1.3: WORD TRIGRAMS ---
    for (int i = 0; i < wordCount - 2; i++) {
        char trigram[384];
        snprintf(trigram, sizeof(trigram), "W3:%s_%s_%s", 
                 wordList[i], wordList[i + 1], wordList[i + 2]);
        hashAndAccumulate(trigram, hidden1);
    }

    // --- LAYER 1.4: POSITIONAL MARKERS ---
    if (wordCount >= 2) {
        char startMarker[256];
        char endMarker[256];
        
        // Beginning of sentence
        snprintf(startMarker, sizeof(startMarker), "START:%s_%s", 
                 wordList[0], wordList[1]);
        hashAndAccumulate(startMarker, hidden1);
        
        // End of sentence
        snprintf(endMarker, sizeof(endMarker), "END:%s_%s", 
                 wordList[wordCount - 2], wordList[wordCount - 1]);
        hashAndAccumulate(endMarker, hidden1);
    }

    // --- LAYER 1.5: FIRST AND LAST WORD MARKERS ---
    char firstWord[256];
    char lastWord[256];
    snprintf(firstWord, sizeof(firstWord), "FIRST:%s", wordList[0]);
    snprintf(lastWord, sizeof(lastWord), "LAST:%s", wordList[wordCount - 1]);
    hashAndAccumulate(firstWord, hidden1);
    hashAndAccumulate(lastWord, hidden1);

    // Apply ReLU activation
    for (int i = 0; i < HIDDEN1_SIZE; i++) {
        hidden1[i] = relu(hidden1[i]);
    }

    // --- LAYER 2: HIDDEN1 -> HIDDEN2 ---
    memcpy(hidden2, BIAS_2, HIDDEN2_SIZE * sizeof(float));
    
    for (int i = 0; i < HIDDEN1_SIZE; i++) {
        if (hidden1[i] > 0.0f) {  // Skip if neuron is inactive (ReLU optimization)
            const float* w2_row = WEIGHTS_2 + (i * HIDDEN2_SIZE);
            for (int j = 0; j < HIDDEN2_SIZE; j++) {
                hidden2[j] += hidden1[i] * w2_row[j];
            }
        }
    }
    
    // Apply ReLU activation
    for (int i = 0; i < HIDDEN2_SIZE; i++) {
        hidden2[i] = relu(hidden2[i]);
    }

    // --- LAYER 3: HIDDEN2 -> OUTPUT ---
    memcpy(output, BIAS_3, OUTPUT_SIZE * sizeof(float));
    
    for (int i = 0; i < HIDDEN2_SIZE; i++) {
        if (hidden2[i] > 0.0f) {  // Skip if neuron is inactive
            const float* w3_row = WEIGHTS_3 + (i * OUTPUT_SIZE);
            for (int j = 0; j < OUTPUT_SIZE; j++) {
                output[j] += hidden2[i] * w3_row[j];
            }
        }
    }

    // --- ARGMAX: Find best prediction ---
    int best_idx = 0;
    float max_val = output[0];
    
    for (int i = 1; i < OUTPUT_SIZE; i++) {
        if (output[i] > max_val) {
            max_val = output[i];
            best_idx = i;
        }
    }

    // --- SOFTMAX (approximate confidence) ---
    // For efficiency, we use a simplified confidence measure
    // True softmax would require exp() on all outputs
    if (out_confidence) {
        float sum = 0.0f;
        for (int i = 0; i < OUTPUT_SIZE; i++) {
            sum += exp(output[i] - max_val);  // Normalize by max to avoid overflow
        }
        *out_confidence = 1.0f / sum;  // Approximation of softmax probability
    }

    return best_idx;
}
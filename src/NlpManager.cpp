#include "NlpManager.h"
#include "ModelWeights.h"
#include <cstring>
#include <cmath>

NlpManager& NlpManager::getInstance() {
    static NlpManager instance;
    return instance;
}

void NlpManager::begin() {
    Serial.println(F("\n========================================"));
    Serial.println(F("   AI Neural Network Initialization"));
    Serial.println(F("========================================"));
    Serial.printf("Input Size:      %d\n", INPUT_SIZE);
    Serial.printf("Hidden Layer 1:  %d\n", HIDDEN1_SIZE);
    Serial.printf("Hidden Layer 2:  %d\n", HIDDEN2_SIZE);
    Serial.printf("Output Classes:  %d\n", OUTPUT_SIZE);
    Serial.println(F("----------------------------------------"));
    Serial.printf("Feature Weights:\n");
    Serial.printf("  W_CHAR:  %d\n", FeatureParams::W_CHAR);
    Serial.printf("  W_WORD:  %d\n", FeatureParams::W_WORD);
    Serial.printf("  W_BI:    %d\n", FeatureParams::W_BI);
    Serial.printf("  W_TRI:   %d\n", FeatureParams::W_TRI);
    Serial.printf("  W_POS:   %d\n", FeatureParams::W_POS);
    Serial.printf("Char n-gram: [%d, %d]\n", FeatureParams::CHAR_MIN, FeatureParams::CHAR_MAX);
    Serial.println(F("========================================\n"));

    threads.addThread(processingThread, this, 32768);
    Serial.println(F("[AI] Processing thread started."));
}

void NlpManager::analyzeSentence(String sentence) {
    if (sentence.length() < 2) return;

    Threads::Scope scope(_mutex);
    _inputBuffer = sentence;
    _hasNewData = true;
}

void NlpManager::processingThread(void* arg) {
    NlpManager* ai = static_cast<NlpManager*>(arg);

    while (true) {
        String localCopy;
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
            float confidence = 0.0f;
            int topicIdx = ai->predict_topic(localCopy, &confidence);

            if (ai->_callback) {
                ai->_callback(String(CATEGORIES[topicIdx]), confidence);
            }
        }

        threads.yield();
    }
}

// ============================================
// MURMURHASH3 (EXACT PYTHON IMPLEMENTATION)
// ============================================

uint32_t NlpManager::murmurhash3_32(const char* key, uint32_t len, uint32_t seed) {
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    const uint32_t r1 = 15;
    const uint32_t r2 = 13;
    const uint32_t m = 5;
    const uint32_t n = 0xe6546b64;

    uint32_t h = seed;
    uint32_t k = 0;

    // Process 4-byte blocks
    const uint32_t nblocks = len / 4;
    const uint32_t* blocks = reinterpret_cast<const uint32_t*>(key);

    for (uint32_t i = 0; i < nblocks; i++) {
        k = blocks[i];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;
        h ^= k;
        h = (h << r2) | (h >> (32 - r2));
        h = h * m + n;
    }

    // Tail
    k = 0;
    const uint8_t* tail = reinterpret_cast<const uint8_t*>(key + nblocks * 4);
    switch (len & 3) {
        case 3: k ^= tail[2] << 16; [[fallthrough]];
        case 2: k ^= tail[1] << 8;  [[fallthrough]];
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

// ============================================
// NORMALIZATION (MATCHING PYTHON NFD)
// ============================================

void NlpManager::normalize_and_lower(char* str) {
    unsigned char* p = reinterpret_cast<unsigned char*>(str);
    unsigned char* write_ptr = p;

    while (*p) {
        if (*p >= 'A' && *p <= 'Z') {
            *write_ptr++ = *p + 32;
            p++;
        }
        else if (*p == 0xC3) {  // Latin-1 Supplement
            unsigned char next = *(p + 1);

            // Uppercase mappings (À-Þ)
            if (next >= 0x80 && next <= 0x85) *write_ptr++ = 'a';
            else if (next == 0x86) { *write_ptr++ = 'a'; *write_ptr++ = 'e'; }
            else if (next == 0x87) *write_ptr++ = 'c';
            else if (next >= 0x88 && next <= 0x8B) *write_ptr++ = 'e';
            else if (next >= 0x8C && next <= 0x8F) *write_ptr++ = 'i';
            else if (next == 0x90) *write_ptr++ = 'd';
            else if (next == 0x91) *write_ptr++ = 'n';
            else if (next >= 0x92 && next <= 0x96) *write_ptr++ = 'o';
            else if (next == 0x98) *write_ptr++ = 'o';
            else if (next >= 0x99 && next <= 0x9C) *write_ptr++ = 'u';
            else if (next == 0x9D) *write_ptr++ = 'y';
            // Lowercase mappings (à-ÿ)
            else if (next >= 0xA0 && next <= 0xA5) *write_ptr++ = 'a';
            else if (next == 0xA6) { *write_ptr++ = 'a'; *write_ptr++ = 'e'; }
            else if (next == 0xA7) *write_ptr++ = 'c';
            else if (next >= 0xA8 && next <= 0xAB) *write_ptr++ = 'e';
            else if (next >= 0xAC && next <= 0xAF) *write_ptr++ = 'i';
            else if (next == 0xB0) *write_ptr++ = 'd';
            else if (next == 0xB1) *write_ptr++ = 'n';
            else if (next >= 0xB2 && next <= 0xB6) *write_ptr++ = 'o';
            else if (next == 0xB8) *write_ptr++ = 'o';
            else if (next >= 0xB9 && next <= 0xBC) *write_ptr++ = 'u';
            else if (next >= 0xBD && next <= 0xBF) *write_ptr++ = 'y';
            else { *write_ptr++ = *p; *write_ptr++ = next; }

            p += 2;
        }
        else if ((*p & 0xE0) == 0xC0) {
            *write_ptr++ = *p++; if (*p) *write_ptr++ = *p++;
        }
        else if ((*p & 0xF0) == 0xE0) {
            *write_ptr++ = *p++; if (*p) *write_ptr++ = *p++; if (*p) *write_ptr++ = *p++;
        }
        else if ((*p & 0xF8) == 0xF0) {
            *write_ptr++ = *p++; if (*p) *write_ptr++ = *p++;
            if (*p) *write_ptr++ = *p++; if (*p) *write_ptr++ = *p++;
        }
        else {
            *write_ptr++ = *p++;
        }
    }

    *write_ptr = '\0';
}

float NlpManager::relu(float x) {
    return (x > 0.0f) ? x : 0.0f;
}

float NlpManager::activation_func(float x) {
    return tanhf(x);  // Votre modèle utilise tanh
}

void NlpManager::softmax(float* values, int size) {
    float max_val = values[0];
    for (int i = 1; i < size; i++) {
        if (values[i] > max_val) max_val = values[i];
    }

    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        values[i] = expf(values[i] - max_val);
        sum += values[i];
    }

    if (sum > 0.0f) {
        for (int i = 0; i < size; i++) {
            values[i] /= sum;
        }
    }
}

void NlpManager::hashAndAccumulate(const char* token, float* hidden1, int weight) {
    if (weight <= 0) return;

    const uint32_t h = murmurhash3_32(token, strlen(token), 0);
    const int32_t signed_h = static_cast<int32_t>(h);
    const float sign = (signed_h < 0) ? -1.0f : 1.0f;

    const uint32_t safe_abs = (signed_h == INT32_MIN) ?
        0x80000000u : static_cast<uint32_t>(std::abs(signed_h));

    const int index = safe_abs % INPUT_SIZE;
    const float val = sign * static_cast<float>(weight);

    // ⚠️ CRITICAL: Accès à W1 tel que défini dans ModelWeights.h
    for (int j = 0; j < HIDDEN1_SIZE; j++) {
        const float w = pgm_read_float(&W1[j][index]);  // [ligne][colonne]
        hidden1[j] += w * val;
    }
}

// ============================================
// PREDICTION (MATCHING PYTHON EXACTLY)
// ============================================

int NlpManager::predict_topic(String text, float* out_confidence) {
    float hidden1[HIDDEN1_SIZE];
    float hidden2[HIDDEN2_SIZE];
    float output[OUTPUT_SIZE];

    // Initialize with biases
    for (int i = 0; i < HIDDEN1_SIZE; i++) {
        hidden1[i] = pgm_read_float(&b1[i]);
    }

    // Text preprocessing
    char buffer[512];
    strncpy(buffer, text.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    normalize_and_lower(buffer);

    // Replace punctuation
    for (int i = 0; buffer[i] != '\0'; i++) {
        if (strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", buffer[i])) {
            buffer[i] = ' ';
        }
    }

    // Tokenization
    constexpr int MAX_WORDS = 50;
    char* wordList[MAX_WORDS];
    int wordCount = 0;

    char* token = strtok(buffer, " \t\n\r");
    while (token != nullptr && wordCount < MAX_WORDS) {
        wordList[wordCount++] = token;
        token = strtok(nullptr, " \t\n\r");
    }

    if (wordCount == 0) {
        if (out_confidence) *out_confidence = 0.0f;
        return 0;
    }

    // Feature extraction
    char scratch[128];

    // Character n-grams (POSITION FIRST, then SIZE)
    if (FeatureParams::W_CHAR > 0) {
        for (int w = 0; w < wordCount; w++) {
            char padded[64];
            snprintf(padded, sizeof(padded), "<%s>", wordList[w]);
            const int padded_len = strlen(padded);

            for (int i = 0; i < padded_len; i++) {
                for (int n = FeatureParams::CHAR_MIN; n <= FeatureParams::CHAR_MAX; n++) {
                    if (i + n <= padded_len) {
                        char ngram[32];
                        strncpy(ngram, padded + i, n);
                        ngram[n] = '\0';

                        snprintf(scratch, sizeof(scratch), "C_%s", ngram);
                        hashAndAccumulate(scratch, hidden1, FeatureParams::W_CHAR);
                    }
                }
            }
        }
    }

    // Word unigrams
    if (FeatureParams::W_WORD > 0) {
        for (int i = 0; i < wordCount; i++) {
            snprintf(scratch, sizeof(scratch), "W_%s", wordList[i]);
            hashAndAccumulate(scratch, hidden1, FeatureParams::W_WORD);
        }
    }

    // Bigrams
    if (FeatureParams::W_BI > 0 && wordCount > 1) {
        for (int i = 0; i < wordCount - 1; i++) {
            snprintf(scratch, sizeof(scratch), "B_%s_%s", wordList[i], wordList[i+1]);
            hashAndAccumulate(scratch, hidden1, FeatureParams::W_BI);
        }
    }

    // Trigrams
    if (FeatureParams::W_TRI > 0 && wordCount > 2) {
        for (int i = 0; i < wordCount - 2; i++) {
            snprintf(scratch, sizeof(scratch), "T_%s_%s_%s",
                     wordList[i], wordList[i+1], wordList[i+2]);
            hashAndAccumulate(scratch, hidden1, FeatureParams::W_TRI);
        }
    }

    // Positional features
    if (FeatureParams::W_POS > 0 && wordCount > 0) {
        snprintf(scratch, sizeof(scratch), "POS_START_%s", wordList[0]);
        hashAndAccumulate(scratch, hidden1, FeatureParams::W_POS);

        snprintf(scratch, sizeof(scratch), "POS_END_%s", wordList[wordCount - 1]);
        hashAndAccumulate(scratch, hidden1, FeatureParams::W_POS);
    }

    // Layer 1: Activation
    for (int i = 0; i < HIDDEN1_SIZE; i++) {
        hidden1[i] = activation_func(hidden1[i]);
    }

    // Layer 2: Hidden1 -> Hidden2
    for (int i = 0; i < HIDDEN2_SIZE; i++) {
        hidden2[i] = pgm_read_float(&b2[i]);
        for (int j = 0; j < HIDDEN1_SIZE; j++) {
            hidden2[i] += hidden1[j] * pgm_read_float(&W2[i][j]);
        }
        hidden2[i] = activation_func(hidden2[i]);
    }

    // Layer 3: Hidden2 -> Output
    for (int i = 0; i < OUTPUT_SIZE; i++) {
        output[i] = pgm_read_float(&b3[i]);
        for (int j = 0; j < HIDDEN2_SIZE; j++) {
            output[i] += hidden2[j] * pgm_read_float(&W3[i][j]);
        }
    }

    // Softmax
    softmax(output, OUTPUT_SIZE);

    // Find best class
    int best_idx = 0;
    float best_score = output[0];
    for (int i = 1; i < OUTPUT_SIZE; i++) {
        if (output[i] > best_score) {
            best_score = output[i];
            best_idx = i;
        }
    }

    if (out_confidence) {
        *out_confidence = best_score * 100.0f;
    }

    return best_idx;
}

// ============================================
// DEBUG FUNCTIONS
// ============================================

void NlpManager::debugHashVerification() {
    Serial.println(F("\n=== HASH VERIFICATION TEST ==="));

    const char* test_tokens[] = {
        "C_<bo", "C_bon", "C_onj", "C_njo",
        "W_bonjour", "B_bonjour_ca",
        "POS_START_bonjour", "POS_END_va"
    };

    for (const auto* token : test_tokens) {
        uint32_t h = murmurhash3_32(token, strlen(token), 0);
        int32_t signed_h = static_cast<int32_t>(h);
        uint32_t abs_h = (signed_h == INT32_MIN) ? 0x80000000u :
                         static_cast<uint32_t>(std::abs(signed_h));
        int index = abs_h % INPUT_SIZE;
        float sign = (signed_h < 0) ? -1.0f : 1.0f;

        Serial.printf("%-25s | %10u | %+2.0f | %5d\n", token, h, sign, index);
    }

    Serial.println(F("\n=== Normalization Test ==="));
    char test1[] = "Bonjour ça va?";
    char test2[] = "ÉLÉPHANT";

    normalize_and_lower(test1);
    normalize_and_lower(test2);

    Serial.printf("\"Bonjour ça va?\" -> \"%s\"\n", test1);
    Serial.printf("\"ÉLÉPHANT\" -> \"%s\"\n", test2);
    Serial.println(F("=== END ===\n"));
}

void NlpManager::debugPrediction(String text) {
    Serial.println(F("\n=== PREDICTION DEBUG ==="));
    Serial.printf("Input: \"%s\"\n\n", text.c_str());

    float confidence = 0.0f;
    int topicIdx = predict_topic(text, &confidence);

    Serial.printf("Result: %s (%.2f%%)\n", CATEGORIES[topicIdx], confidence);
    Serial.println(F("=== END ===\n"));
}

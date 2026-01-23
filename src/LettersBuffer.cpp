#include "LettersBuffer.h"

LettersBuffer::LettersBuffer() {
    buffer.reserve(MAX_LEN);
}

bool LettersBuffer::add(char c) {
    if (c == 0) return false;

    if (buffer.length() >= MAX_LEN) {
        return false;
    }

    buffer.push_back(c);
    return true;
}

void LettersBuffer::backspace() {
    if (!buffer.empty()) {
        buffer.pop_back();
    }
}

void LettersBuffer::clear() {
    buffer.clear();
}

const char* LettersBuffer::get_c_str() const {
    return buffer.c_str();
}

const std::string& LettersBuffer::get() const {
    return buffer;
}

size_t LettersBuffer::length() const {
    return buffer.length();
}

bool LettersBuffer::isEmpty() const {
    return buffer.empty();
}

bool LettersBuffer::isFull() const {
    return buffer.length() >= MAX_LEN;
}

float LettersBuffer::getEntropy() const {
    if (buffer.empty()) {
        return 0.0f;
    }

    // 1. Frequency Analysis
    // We use a fixed array on the stack. fast and zero allocation.
    // '256' covers all extended ASCII characters.
    std::array<int, 256> counts;
    counts.fill(0); // Modern equivalent of memset

    for (unsigned char c : buffer) {
        counts[c]++;
    }

    // 2. Calculate Entropy
    float entropy = 0.0f;
    float totalLen = (float)buffer.length();

    // Optimization: Pre-calculate 1/len to replace division with multiplication
    float invLen = 1.0f / totalLen;

    for (int count : counts) {
        if (count > 0) {
            float p = count * invLen; // Probability of this character
            entropy -= p * std::log2(p);
        }
    }

    return entropy;
}
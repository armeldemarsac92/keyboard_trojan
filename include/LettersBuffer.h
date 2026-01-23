#pragma once
#include <string>
#include <vector>

class LettersBuffer {
private:
    std::string buffer;
    static const size_t MAX_LEN = 256; 

public:
    LettersBuffer();

    bool addChar(char keyCode, uint8_t modifier);
    bool addShortcut(char keyCode, uint8_t modifier);

    void backspace();
    void clear();

    const char* get_c_str() const; 
    const std::string& get() const; 

    size_t length() const;
    bool isEmpty() const;
    bool isFull() const;

    float getEntropy() const;
};

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

class LettersBuffer {
private:
    std::string buffer;
    static constexpr std::size_t MAX_LEN = 256;

public:
    LettersBuffer();

    bool addChar(char keyCode, std::uint8_t modifier);
    bool addShortcut(char keyCode, std::uint8_t modifier);

    void backspace();
    void clear();

    const char* get_c_str() const;
    const std::string& get() const;

    std::size_t length() const;
    bool isEmpty() const;
    bool isFull() const;

    float getEntropy() const;
};

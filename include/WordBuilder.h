#ifndef WORDBUILDER_H
#define WORDBUILDER_H

#include <cstdint>
#include <cstring>

class WordBuilder {
private:
    static const int MAX_LEN = 256;
    char buffer[MAX_LEN + 1];
    int currentLen = 0;

public:
    WordBuilder() { clear(); }

    // À mettre dans KeyHelper.h ou WordBuilder.h
    float calculateEntropy(const char* s) {
        int counts[256] = {0};
        int len = strlen(s);
        if (len == 0) return 0.0f;

        for (int i = 0; i < len; i++) {
            counts[(unsigned char)s[i]]++;
        }

        float entropy = 0;
        for (int i = 0; i < 256; i++) {
            if (counts[i] > 0) {
                float p = (float)counts[i] / len;
                entropy -= p * log2f(p);
            }
        }
        return entropy;
    }

    static void getShortcutName(uint8_t code, uint8_t mods, char* dest) {
        dest[0] = '\0';
        strcat(dest, "[");
        if (mods & 0x11) strcat(dest, "CTRL+");
        if (mods & 0x04) strcat(dest, "ALT+");
        if (mods & 0x40) strcat(dest, "ALTGR+");
        if (mods & 0x08) strcat(dest, "WIN+");
        if (mods & 0x22) strcat(dest, "SHIFT+");

        char keyName[20];

        // --- GESTION AZERTY POUR LES RACCOURCIS ---
        switch(code) {
            case 20: strcpy(keyName, "A"); break; // Q position -> A
            case 4:  strcpy(keyName, "Q"); break; // A position -> Q
            case 26: strcpy(keyName, "Z"); break; // W position -> Z
            case 29: strcpy(keyName, "W"); break; // Z position -> W
            case 51: strcpy(keyName, "M"); break; // Touche M

                // Touches de navigation
            case 43: strcpy(keyName, "TAB"); break;
            case 44: strcpy(keyName, "SPACE"); break;
            case 79: strcpy(keyName, "RIGHT"); break;
            case 80: strcpy(keyName, "LEFT"); break;
            case 81: strcpy(keyName, "DOWN"); break;
            case 82: strcpy(keyName, "UP"); break;
            case 41: strcpy(keyName, "ESC"); break;
            case 76: strcpy(keyName, "DEL"); break;

            default:
                if (code >= 5 && code <= 25) { // Lettres de B à Y (sauf swaps)
                    keyName[0] = (code - 4) + 'A';
                    keyName[1] = '\0';
                } else {
                    sprintf(keyName, "K%d", code);
                }
                break;
        }
        strcat(dest, keyName);
        strcat(dest, "]");
    }

    void add(uint8_t keycode, uint8_t modifiers) {
        if (currentLen < MAX_LEN) {
            char c = mapKeyToAscii(keycode, modifiers);
            if (c != 0) {
                buffer[currentLen] = c;
                currentLen++;
                buffer[currentLen] = '\0';
            }
        }
    }

    void backspace() {
        if (currentLen > 0) {
            currentLen--;
            buffer[currentLen] = '\0';
        }
    }

    void clear() {
        currentLen = 0;
        buffer[0] = '\0';
    }

    const char* getWord() { return buffer; }
    bool isEmpty() { return currentLen == 0; }

private:
    char mapKeyToAscii(uint8_t code, uint8_t modifiers) {
        bool isShift = (modifiers & 0x02) || (modifiers & 0x20);
        bool isAltGr = (modifiers & 0x40);

        // 1. LIGNE DES CHIFFRES & SYMBOLES (30 à 39)
        if (code >= 30 && code <= 39) {
            if (isAltGr) {
                switch (code) {
                    case 32: return '#';  case 33: return '{'; case 34: return '[';
                    case 35: return '|';  case 36: return '`'; case 37: return '\\';
                    case 38: return '^';  case 39: return '@'; // AltGr + à = @
                    default: return 0;
                }
            }
            if (isShift) {
                if (code == 39) return '0';
                return '1' + (code - 30);
            } else {
                switch (code) {
                    case 30: return '&'; case 31: return 'e'; case 32: return '"';
                    case 33: return '\'';case 34: return '('; case 35: return '-';
                    case 36: return 'e'; case 37: return '_'; case 38: return 'c';
                    case 39: return 'a';
                }
            }
        }

        // 2. SYMBOLES DROITS & PONCTUATION
        switch (code) {
            case 45: return isShift ? '_' : '-';
            case 46: return isShift ? '+' : '=';
            case 51: return isShift ? 'M' : 'm';
            case 52: return isShift ? '%' : 'u';
            case 54: return isShift ? '.' : ',';
            case 55: return isShift ? '/' : ';';
            case 56: return isShift ? '?' : ':';
            case 100: return isShift ? '>' : '<';
        }

        // 3. LETTRES AZERTY (Swaps)
        if (code == 4)  return isShift ? 'Q' : 'q';
        if (code == 20) return isShift ? 'A' : 'a';
        if (code == 29) return isShift ? 'W' : 'w';
        if (code == 26) return isShift ? 'Z' : 'z';

        // 4. LETTRES STANDARD (B-Y)
        if (code >= 5 && code <= 28) {
            return (code - 4) + (isShift ? 'A' : 'a');
        }

        // 5. PAVÉ NUMÉRIQUE
        if (code >= 89 && code <= 97) return '1' + (code - 89);
        if (code == 98) return '0';

        return 0;
    }
};

#endif
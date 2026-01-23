#include "AzertyLayout.h"

namespace AzertyLayout {

    char mapKeyToAscii(uint8_t code, uint8_t modifiers) {
        // --- LOGIC IDENTICAL TO YOUR ORIGINAL CODE ---
        bool isShift = (modifiers & 0x02) || (modifiers & 0x20);
        bool isAltGr = (modifiers & 0x40);

        // 1. LIGNE DES CHIFFRES & SYMBOLES (30 à 39)
        if (code >= 30 && code <= 39) {
            if (isAltGr) {
                switch (code) {
                    case 32: return '#';  case 33: return '{'; case 34: return '[';
                    case 35: return '|';  case 36: return '`'; case 37: return '\\';
                    case 38: return '^';  case 39: return '@';
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

    std::string getShortcutName(uint8_t code, uint8_t modifiers) {
        std::string result = "[";

        // Use 'reserve' to prevent multiple tiny memory allocations (optimization)
        result.reserve(32);

        // Modifiers
        if (mods & 0x11) result += "CTRL+";
        if (mods & 0x04) result += "ALT+";
        if (mods & 0x40) result += "ALTGR+";
        if (mods & 0x08) result += "WIN+";
        if (mods & 0x22) result += "SHIFT+";

        // Key Name
        switch(code) {
            // AZERTY Swaps
            case 20: result += "A"; break;
            case 4:  result += "Q"; break;
            case 26: result += "Z"; break;
            case 29: result += "W"; break;
            case 51: result += "M"; break;

            // Navigation
            case 43: result += "TAB"; break;
            case 44: result += "SPACE"; break;
            case 79: result += "RIGHT"; break;
            case 80: result += "LEFT"; break;
            case 81: result += "DOWN"; break;
            case 82: result += "UP"; break;
            case 41: result += "ESC"; break;
            case 76: result += "DEL"; break;

            default:
                if (code >= 5 && code <= 25) {
                    // Logic: Calculate ASCII char and append it
                    char c = (code - 4) + 'A';
                    result += c;
                } else {
                    // Logic: "K" + number
                    result += "K" + std::to_string(code);
                }
                break;
        }

        result += "]";
        return result;
    }
}
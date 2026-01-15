#ifndef KEYHELPER_H
#define KEYHELPER_H

#include <cstdint>

class KeyHelper {
public:
    static inline bool isBackspace(uint8_t keycode) { return (keycode == 42); }
    static inline bool isSpace(uint8_t keycode)     { return (keycode == 44); }
    static inline bool isEnter(uint8_t keycode)     { return (keycode == 40 || keycode == 88); }

    static inline bool isModifierOnly(uint8_t code, uint8_t mods) {
        return (code == 0 && mods != 0);
    }

    static inline bool isSystemNoise(uint8_t code) {
        // 103, 106, 107 sont souvent des codes "fantômes" sur Windows/Linux
        if (code >= 102 && code <= 110) return true;
        // On peut aussi ignorer les touches multimédias si elles te polluent
        return false;
    }

    static inline bool hasModifier(uint8_t mods) {
        // On exclut le Shift (0x02/0x20) et AltGr (0x40) car ils servent à la frappe normale
        return (mods & 0x1D); // Filtre pour Ctrl, Alt, et GUI
    }

    static inline bool isCaptureChar(uint8_t code) {
        // On capture presque tout (Lettres, Chiffres, Pavé num, Ponctuation)
        if (code >= 4 && code <= 56) return true;
        if (code >= 89 && code <= 98) return true;
        if (code == 100) return true; // Touche < >
        return false;
    }

    static inline bool isLetterOrNumber(uint8_t code) {
        // 1. Lettres A-Z (Sauf le code 16 qui est virgule sur Azerty)
        if (code >= 4 && code <= 29 && code != 16) return true;

        // 2. Touche 'M' (Code 51 sur Azerty)
        if (code == 51) return true;

        // 3. Ligne du haut (Accents & Chiffres) - Codes 30 à 39
        if (code >= 30 && code <= 39) return true;

        // 4. Pavé Numérique (1-9 et 0) - Codes 89 à 98
        if (code >= 89 && code <= 98) return true;

        return false;
    }

    static inline bool isPunctuation(uint8_t code) {
        switch (code) {
            case 16: // ,
            case 54: // ;
            case 55: // :
            case 56: // !
            case 52: // .
                return true;
            default:
                return false;
        }
    }
};

#endif
// Auto-generated MLP model for Teensy 4.1
// Generated: 20260205_172955
// Input features: 8192

#ifndef MODEL_H
#define MODEL_H

#include <Arduino.h>

// Feature extraction parameters
namespace FeatureParams {
    const int W_CHAR = 2;
    const int W_WORD = 5;
    const int W_BI = 2;
    const int W_TRI = 0;
    const int W_POS = 3;
    const int CHAR_MIN = 3;
    const int CHAR_MAX = 4;
    const int alpha = 0.015773648505201143;
    const int learning_rate_init = 0.0007522927354159601;
    const int hidden_1 = 96;
    const int hidden_2 = 64;
}

// Network architecture
const int INPUT_SIZE = 8192;
const int HIDDEN1_SIZE = 96;
const int HIDDEN2_SIZE = 64;
const int OUTPUT_SIZE = 11;

// Categories
const char* CATEGORIES[] = {
    "ACCOUNTING",
    "BANKING",
    "BUSINESS",
    "CYBER",
    "GOSSIP",
    "HR_COMPLAINT",
    "HR_HIRING",
    "INFRA",
    "LOVE",
    "MISC",
    "TECH",
};

// Input -> Hidden1 weights
const float W1[96][8192] PROGMEM = {
  };

// Hidden1 biases
const float b1[96] PROGMEM = {
};

// Hidden1 -> Hidden2 weights
const float W2[64][96] PROGMEM = {
 ;

// Hidden2 biases
const float b2[64] PROGMEM = {
    -0.176192f, -0.182215f, -0.088808f, 0.018275f, -0.181417f, 0.133125f, 0.010688f, 0.108919f, 0.010435f, -0.055312f, -0.031535f, -0.108572f, -0.083811f, 0.155422f, -0.038871f, -0.183649f, -0.132371f, -0.055694f, 0.130874f, 0.066933f, -0.119778f, -0.021244f, -0.151590f, -0.028912f, -0.104073f, -0.185630f, 0.083024f, -0.115159f, 0.111950f, -0.014114f, 0.059067f, 0.113046f, -0.132179f, -0.027297f, -0.066215f, 0.090006f, -0.054685f, 0.025442f, -0.189565f, 0.022612f, 0.084723f, -0.114516f, -0.070358f, 0.027100f, -0.171239f, -0.078432f, -0.156772f, 0.062401f, 0.061778f, 0.048142f, 0.029453f, 0.030047f, 0.083723f, 0.143981f, 0.104352f, -0.147572f, 0.092763f, -0.004733f, 0.154678f, 0.010375f, 0.095817f, -0.157577f, -0.097889f, 0.080393f
};

// Hidden2 -> Output weights
const float W3[11][64] PROGMEM = {
  };

// Output biases
const float b3[11] PROGMEM = {
};

// Activation function: tanh
inline float activation(float x) {
    return tanh(x);
}

// Softmax for output layer
void softmax(float* input, int size) {
    float max_val = input[0];
    for(int i = 1; i < size; i++) if(input[i] > max_val) max_val = input[i];
    float sum = 0.0f;
    for(int i = 0; i < size; i++) {
        input[i] = exp(input[i] - max_val);
        sum += input[i];
    }
    for(int i = 0; i < size; i++) input[i] /= sum;
}

// Memory estimation: ~3099.42 KB
// Total parameters: 793451

#endif // MODEL_H

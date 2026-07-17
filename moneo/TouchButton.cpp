#include "TouchButton.h"

volatile bool TouchButton::_flag = false;

void IRAM_ATTR TouchButton::_isr() {
    _flag = true;
}

void TouchButton::begin(int pin, int threshold, unsigned long debounceMs) {
    _pin        = pin;
    _threshold  = threshold;
    _debounceMs = debounceMs;
    _lastMs     = 0;
    touchAttachInterrupt(pin, _isr, threshold);
}

bool TouchButton::pressed() {
    if (!_flag) return false;
    _flag = false;

    unsigned long now = millis();
    if (now - _lastMs < _debounceMs) return false;

    // Confirm a real touch is still present. The interrupt can fire on brief
    // electrical noise; a genuine press still reads above the threshold when we
    // reach here, while a noise spike has already decayed and is rejected.
    if (touchRead(_pin) < _threshold) return false;

    _lastMs = now;
    return true;
}

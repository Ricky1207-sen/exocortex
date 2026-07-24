#ifndef TouchButton_h
#define TouchButton_h

#include <Arduino.h>

// ============================================================
// TouchButton — a capacitive touch pad used as a single button.
//
// The pad fires an interrupt that only sets a flag; pressed() is polled from
// the main loop and returns true once per genuine, debounced press. It re-reads
// the pad to reject brief electrical noise. On ESP32-S3, a real press keeps the
// reading above TOUCH_THRESHOLD; a noise spike has decayed below it by then.
// ============================================================
class TouchButton {
public:
    void begin(int pin, int threshold, unsigned long debounceMs);

    // Returns true exactly once per real, debounced press.
    bool pressed();

private:
    static void IRAM_ATTR _isr();
    static volatile bool _flag;   // set by the ISR (one button → static is fine)

    int           _pin        = 0;
    int           _threshold  = 0;
    unsigned long _debounceMs = 0;
    unsigned long _lastMs     = 0;
};

#endif

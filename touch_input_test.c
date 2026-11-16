#include "touch_input.h"
#include <assert.h>
#include <stdio.h>

static TouchAction gesture(double x, double y, double end_x, double end_y, int open) {
    TouchInput input = {0};
    touch_input_begin(&input, x, y, 1300);
    touch_input_motion(&input, end_x, end_y);
    return touch_input_end(&input, end_x, end_y, open);
}

int main(void) {
    TouchInput input = {0};
    assert(gesture(100, 400, 104, 405, 0) == TOUCH_TAP);
    assert(gesture(500, 1260, 510, 1080, 0) == TOUCH_OPEN_MENU);
    assert(gesture(500, 1260, 510, 1200, 0) == TOUCH_NONE);
    assert(gesture(500, 600, 510, 420, 0) == TOUCH_NONE);
    assert(gesture(500, 1260, 760, 1080, 0) == TOUCH_NONE);
    assert(gesture(500, 300, 510, 470, 1) == TOUCH_CLOSE_MENU);
    assert(gesture(500, 300, 510, 470, 0) == TOUCH_NONE);
    assert(gesture(500, 12, 510, 190, 0) == TOUCH_OPEN_SYSTEM_MENU);
    assert(gesture(500, 12, 510, 190, 1) == TOUCH_OPEN_SYSTEM_MENU);
    assert(gesture(500, 12, 510, 65, 0) == TOUCH_NONE);
    assert(gesture(500, 12, 710, 190, 0) == TOUCH_NONE);
    assert(gesture(500, 90, 510, 250, 0) == TOUCH_NONE);
    assert(gesture(500, 1260, 500, 1260, 0) == TOUCH_TAP);
    touch_input_begin(&input, 500, 1260, 1300);
    touch_input_motion(&input, 500, 1120);
    assert(touch_input_end(&input, 500, 1260, 0) == TOUCH_NONE);
    assert(touch_input_end(&input, 500, 1260, 0) == TOUCH_NONE);
    puts("touch input passed: tap, edge swipe, drag rejection, dismissal, cancellation");
    return 0;
}

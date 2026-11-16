#include "touch_input.h"
#include <math.h>

void touch_input_begin(TouchInput *input, double x, double y, double height) {
    input->start_x = x;
    input->start_y = y;
    input->pressed = 1;
    input->dragged = 0;
    input->bottom_edge = height > 0 && y >= height - 120;
}

void touch_input_motion(TouchInput *input, double x, double y) {
    if (input->pressed && hypot(x - input->start_x, y - input->start_y) > 20)
        input->dragged = 1;
}

TouchAction touch_input_end(TouchInput *input, double x, double y, int menu_open) {
    double dx = x - input->start_x;
    double dy = y - input->start_y;
    if (!input->pressed)
        return TOUCH_NONE;
    touch_input_motion(input, x, y);
    input->pressed = 0;
    if (fabs(dy) >= 96 && fabs(dy) >= fabs(dx) * 1.5) {
        if (menu_open && dy > 0)
            return TOUCH_CLOSE_MENU;
        if (!menu_open && input->bottom_edge && dy < 0)
            return TOUCH_OPEN_MENU;
    }
    return input->dragged ? TOUCH_NONE : TOUCH_TAP;
}

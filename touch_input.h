#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

typedef enum { TOUCH_NONE, TOUCH_TAP, TOUCH_OPEN_MENU, TOUCH_CLOSE_MENU, TOUCH_OPEN_SYSTEM_MENU } TouchAction;

typedef struct {
    double start_x, start_y;
    int pressed, dragged, bottom_edge;
} TouchInput;

void touch_input_begin(TouchInput *input, double x, double y, double height);
void touch_input_motion(TouchInput *input, double x, double y);
TouchAction touch_input_end(TouchInput *input, double x, double y, int menu_open);

#endif

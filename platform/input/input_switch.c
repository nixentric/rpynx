/* libnx -> rsl_raw_input. The only file in the input subsystem that needs libnx. */
#include <switch.h>

#include "input_switch.h"

/* rsl_npad_bit (input.c) must match libnx's HidNpadButton layout. */
#define NPAD(btn, nx) _Static_assert(HidNpadButton_##nx == BIT(btn), #nx " moved in libnx")
NPAD(0, A); NPAD(1, B); NPAD(2, X); NPAD(3, Y);
NPAD(4, StickL); NPAD(5, StickR); NPAD(6, L); NPAD(7, R);
NPAD(8, ZL); NPAD(9, ZR); NPAD(10, Plus); NPAD(11, Minus);
NPAD(12, Left); NPAD(13, Up); NPAD(14, Right); NPAD(15, Down);
NPAD(24, LeftSL); NPAD(25, LeftSR); NPAD(26, RightSL); NPAD(27, RightSR);
_Static_assert(JOYSTICK_MAX == RSL_STICK_MAX, "stick range changed");

void rsl_switch_input_init(PadState *pad)
{
    /* One player, every standard style (handheld, dual/single Joy-Con, Pro). */
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(pad);
    hidInitializeTouchScreen();
}

void rsl_switch_input_read(PadState *pad, rsl_raw_input *out)
{
    padUpdate(pad);
    out->buttons = rsl_normalize_npad(padGetButtons(pad));
    for (int s = 0; s < 2; s++) {
        HidAnalogStickState st = padGetStickPos(pad, s);
        out->stick_x[s] = st.x;
        out->stick_y[s] = st.y;
    }
    HidTouchScreenState ts = {0};
    out->touching = hidGetTouchScreenStates(&ts, 1) && ts.count > 0;
    out->touch_x = out->touching ? (float)ts.touches[0].x : 0.0f;
    out->touch_y = out->touching ? (float)ts.touches[0].y : 0.0f;
}

uint32_t rsl_switch_pad_signature(const PadState *pad)
{
    /* Changes when a controller connects, disconnects or changes style
     * (e.g. Joy-Con detached from the console). */
    return padIsConnected(pad) ? (uint32_t)padGetStyleSet(pad) | 0x80000000u : 0;
}

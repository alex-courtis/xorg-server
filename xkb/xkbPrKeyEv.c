/************************************************************
Copyright (c) 1993 by Silicon Graphics Computer Systems, Inc.

Permission to use, copy, modify, and distribute this
software and its documentation for any purpose and without
fee is hereby granted, provided that the above copyright
notice appear in all copies and that both that copyright
notice and this permission notice appear in supporting
documentation, and that the name of Silicon Graphics not be
used in advertising or publicity pertaining to distribution
of the software without specific prior written permission.
Silicon Graphics makes no representation about the suitability
of this software for any purpose. It is provided "as is"
without any express or implied warranty.

SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
THE USE OR PERFORMANCE OF THIS SOFTWARE.

********************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdio.h>
#include <math.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include "misc.h"
#include "inputstr.h"
#include "exevents.h"
#include "eventstr.h"
#include <xkbsrv.h>
#include <ctype.h>
#include "events.h"

#define LENGTH(X) (sizeof X / sizeof X[0])

enum Side {
    LEFT = 0,
    RIGHT = 1,
};

typedef struct RawKey {
    const char *desc;
    uint32_t keycode;
    enum Side side;
    int pressed;
} RawKey;

static RawKey modifierkeys[] = {

        /* R4 */
        {"SHIFT", 50,  LEFT},
        {"SHIFT", 62,  RIGHT},

        /* R5 */
        {"CTRL",  37,  LEFT},
        {"ALT",   64,  LEFT},
        {"SUPER", 133, LEFT},
        {"SUPER", 134, RIGHT},
        {"ALT",   108, RIGHT},
        {"CTRL",  105, RIGHT},
};

static RawKey squishablekeys[] = {

        /* R1 */
        {"ESC",       9,  LEFT},
        {"1",         10, LEFT},
        {"2",         11, LEFT},
        {"3",         12, LEFT},
        {"4",         13, LEFT},
        {"5",         14, LEFT},
        {"6",         15, LEFT},
        {"7",         16, RIGHT},
        {"8",         17, RIGHT},
        {"9",         18, RIGHT},
        {"0",         19, RIGHT},
        {"MINUS",     20, RIGHT},
        {"EQUALS",    21, RIGHT},
        {"BACKSPACE", 22, RIGHT},
        {"GRAVE",     49, RIGHT},

        /* R2 */
        {"TAB",       23, LEFT},
        {"Q",         24, LEFT},
        {"W",         25, LEFT},
        {"E",         26, LEFT},
        {"R",         27, LEFT},
        {"T",         28, LEFT},
        {"Y",         29, RIGHT},
        {"U",         30, RIGHT},
        {"I",         31, RIGHT},
        {"O",         32, RIGHT},
        {"P",         33, RIGHT},
        {"BRACE_L",   34, RIGHT},
        {"BRACE_R",   35, RIGHT},
        {"BACKSLASH", 51, RIGHT},

        /* R3 */
        {"A",         38, LEFT},
        {"S",         39, LEFT},
        {"D",         40, LEFT},
        {"F",         41, LEFT},
        {"G",         42, LEFT},
        {"H",         43, RIGHT},
        {"J",         44, RIGHT},
        {"K",         45, RIGHT},
        {"L",         46, RIGHT},
        {"SEMICOLON", 47, RIGHT},
        {"QUOTE",     48, RIGHT},
        {"ENTER",     36, RIGHT},

        /* R4 */
        {"Z",         52, LEFT},
        {"X",         53, LEFT},
        {"C",         54, LEFT},
        {"V",         55, LEFT},
        {"B",         56, LEFT},
        {"N",         57, RIGHT},
        {"M",         58, RIGHT},
        {"PERIOD",    60, RIGHT},
        {"SLASH",     61, RIGHT},
};

void
XkbTrackModifierState(DeviceEvent *event) {
    for (int i = 0; i < LENGTH(modifierkeys); i++)
        if (modifierkeys[i].keycode == event->detail.key)
            modifierkeys[i].pressed = event->type == ET_KeyPress;
}

Bool
XkbSquishPressByModifierSide(DeviceEvent *event) {
    static char buf[1024];

    char *bp;
    RawKey *pressedkey;
    int squish;

    squish = 0;

    /* identify */
    pressedkey = 0;
    for (int i = 0; i < LENGTH(squishablekeys); i++) {
        if (squishablekeys[i].keycode == event->detail.key) {
            pressedkey = &squishablekeys[i];
            break;
        }
    }

    /* squish interesting keys only */
    if (!pressedkey)
        return 0;

    /* decide */
    bp = buf;
    for (int i = 0; i < LENGTH(modifierkeys); i++) {
        if (modifierkeys[i].pressed && modifierkeys[i].side == pressedkey->side) {
            bp += sprintf(bp, " %u,%s", modifierkeys[i].keycode, modifierkeys[i].desc);
            squish = 1;
        }
    }

    /* log */
    if (squish) {
        fprintf(stderr, "Squishing %u,%s%s\n", pressedkey->keycode, pressedkey->desc, buf);
    }

    return squish;
}

/***====================================================================***/

void
XkbProcessKeyboardEvent(DeviceEvent *event, DeviceIntPtr keybd)
{
    KeyClassPtr keyc = keybd->key;
    XkbSrvInfoPtr xkbi;
    int key;
    XkbBehavior behavior;
    unsigned ndx;

    xkbi = keyc->xkbInfo;
    key = event->detail.key;
    if (xkbDebugFlags & 0x8)
        DebugF("[xkb] XkbPKE: Key %d %s\n", key,
               (event->type == ET_KeyPress ? "down" : "up"));

    if (xkbi->repeatKey == key && event->type == ET_KeyRelease &&
        !(xkbi->desc->ctrls->enabled_ctrls & XkbRepeatKeysMask))
        AccessXCancelRepeatKey(xkbi, key);

    behavior = xkbi->desc->server->behaviors[key];
    /* The "permanent" flag indicates a hard-wired behavior that occurs */
    /* below XKB, such as a key that physically locks.   XKB does not   */
    /* do anything to implement the behavior, but it *does* report that */
    /* key is hardwired */

    if (!(behavior.type & XkbKB_Permanent)) {
        switch (behavior.type) {
        case XkbKB_Default:
            /* Neither of these should happen in practice, but ignore them
               anyway. */
            if (event->type == ET_KeyPress && !event->key_repeat &&
                key_is_down(keybd, key, KEY_PROCESSED))
                return;
            else if (event->type == ET_KeyRelease &&
                     !key_is_down(keybd, key, KEY_PROCESSED))
                return;
            break;
        case XkbKB_Lock:
            if (event->type == ET_KeyRelease)
                return;
            else if (key_is_down(keybd, key, KEY_PROCESSED))
                event->type = ET_KeyRelease;
            break;
        case XkbKB_RadioGroup:
            ndx = (behavior.data & (~XkbKB_RGAllowNone));
            if (ndx < xkbi->nRadioGroups) {
                XkbRadioGroupPtr rg;

                if (event->type == ET_KeyRelease)
                    return;

                rg = &xkbi->radioGroups[ndx];
                if (rg->currentDown == event->detail.key) {
                    if (behavior.data & XkbKB_RGAllowNone) {
                        event->type = ET_KeyRelease;
                        XkbHandleActions(keybd, keybd, event);
                        rg->currentDown = 0;
                    }
                    return;
                }
                if (rg->currentDown != 0) {
                    int tmpkey = event->detail.key;

                    event->type = ET_KeyRelease;
                    event->detail.key = rg->currentDown;
                    XkbHandleActions(keybd, keybd, event);
                    event->type = ET_KeyPress;
                    event->detail.key = tmpkey;
                }
                rg->currentDown = key;
            }
            else
                ErrorF("[xkb] InternalError! Illegal radio group %d\n", ndx);
            break;
        case XkbKB_Overlay1:
        case XkbKB_Overlay2:
        {
            unsigned which;
            unsigned overlay_active_now;
            unsigned is_keyrelease = (event->type == ET_KeyRelease) ? 1 : 0;
            /* Remembers whether the key was pressed while overlay was down,
             * for when overlay is already released, but the key is not. */
            unsigned key_was_overlaid = 0;

            if (behavior.type == XkbKB_Overlay1)
                which = XkbOverlay1Mask;
            else
                which = XkbOverlay2Mask;
            overlay_active_now = (xkbi->desc->ctrls->enabled_ctrls & which) ? 1 : 0;

            if ((unsigned char)key == key) {
                key_was_overlaid = BitIsOn(xkbi->overlay_perkey_state, key);
                if (!is_keyrelease) {
                    if (overlay_active_now)
                        SetBit(xkbi->overlay_perkey_state, key);
                } else {
                    if (key_was_overlaid)
                        ClearBit(xkbi->overlay_perkey_state, key);
                }
            }

            if ((overlay_active_now || key_was_overlaid) &&
                    (behavior.data >= xkbi->desc->min_key_code) &&
                    (behavior.data <= xkbi->desc->max_key_code)) {
                event->detail.key = behavior.data;
            }
        }
            break;
        default:
            ErrorF("[xkb] unknown key behavior 0x%04x\n", behavior.type);
            break;
        }
    }
    XkbHandleActions(keybd, keybd, event);
    return;
}

void
ProcessKeyboardEvent(InternalEvent *ev, DeviceIntPtr keybd)
{

    KeyClassPtr keyc = keybd->key;
    XkbSrvInfoPtr xkbi = NULL;
    ProcessInputProc backup_proc;
    xkbDeviceInfoPtr xkb_priv = XKBDEVICEINFO(keybd);
    DeviceEvent *event = &ev->device_event;
    int is_press = (event->type == ET_KeyPress);
    int is_release = (event->type == ET_KeyRelease);

    /* We're only interested in key events. */
    if (!is_press && !is_release) {
        UNWRAP_PROCESS_INPUT_PROC(keybd, xkb_priv, backup_proc);
        keybd->public.processInputProc(ev, keybd);
        COND_WRAP_PROCESS_INPUT_PROC(keybd, xkb_priv, backup_proc,
                                     xkbUnwrapProc);
        return;
    }

    xkbi = keyc->xkbInfo;

    /* only pay attention to the master, which aggregates all keyboards and allows use of the statics to track state */
    if (keybd->type == MASTER_KEYBOARD) {
        XkbTrackModifierState(event);

        /* squish presses */
        if (is_press && XkbSquishPressByModifierSide(event)) {
            return;
        }
    }

    /* If AccessX filters are active, then pass it through to
     * AccessXFilter{Press,Release}Event; else, punt to
     * XkbProcessKeyboardEvent.
     *
     * If AXF[PK]E don't intercept anything (which they probably won't),
     * they'll punt through XPKE anyway. */
    if ((xkbi->desc->ctrls->enabled_ctrls & XkbAllFilteredEventsMask)) {
        if (is_press)
            AccessXFilterPressEvent(event, keybd);
        else if (is_release)
            AccessXFilterReleaseEvent(event, keybd);
        return;
    }
    else {
        XkbProcessKeyboardEvent(event, keybd);
    }

    return;
}

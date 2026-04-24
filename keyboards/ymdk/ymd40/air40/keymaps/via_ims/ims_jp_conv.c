/* Copyright 2026 pine_kz (pine_kz@nifty.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
 /*		This file is for the conversion of symbols from US keyboard to JP keylayout.  */

#include "ims_lang.h"
#include "US_JP_extra.h"

#ifdef IMS_LAYOUT_US_AX

bool process_ims_symbol_conv(uint16_t keycode, keyrecord_t *record) {
    uint8_t mods = get_mods();
    bool shifted = (mods & (MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT)));

    if (record->event.pressed) {
        switch (keycode) {
            case KC_SCLN:
                if (shifted) {
                    del_mods(MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT));
                    tap_code16(KC_QUOT);
                    set_mods(mods);
                    return false;
                }
                break;
            case KC_2:
                if (shifted) {
                    del_mods(MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT));
                    tap_code16(KC_LBRC);
                    set_mods(mods);
                    return false;
                }
                break;
            case KC_6:
                if (shifted) {
                    del_mods(MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT));
                    tap_code16(KC_EQL);
                    set_mods(mods);
                    return false;
                }
                break;
        }
    }
    return true;
}

#endif
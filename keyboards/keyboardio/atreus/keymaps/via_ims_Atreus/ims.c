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


#include <string.h>
#include "ims_lang.h"
#include "via.h"

extern void ims_handle_lang(uint16_t index);
extern bool process_ims_symbol_conv(uint16_t keycode, keyrecord_t *record);

/* ===================================================================================
 * [A-1] 型・変数  (共有)
 * =================================================================================== */
static bool ime_on    = false;
static bool alfa_mode = false;
static bool ims_bypass = false;

typedef struct {
    uint8_t row;
    uint8_t col;
    bool    valid;
} pending_key_t;

static pending_key_t pending_lang = {0, 0, false};
static uint16_t      pending_s_key = 0xFFFF;
static uint16_t      combo_timer   = 0;

/* =============================================================================
 * [A-2] 型・変数  (共有）
 * =================================================================================== */
typedef struct {
    uint8_t  mods;
    uint8_t  peak_mods;
    uint16_t base;
    bool     started;
} ims_chord_t;

static ims_chord_t chord = {0, 0, 0, false};

/* 素通しキー追跡 (Mod保持中のリリース漏れ防止) */
static pending_key_t passthrough_key = {0, 0, false};

/* レイヤ移動による alfa トラッキング */
static pending_key_t layer_forced_alfa = {0, 0, false};

static inline void set_ime_on(bool v) {
    ime_on = v;
    if (!v) {
        alfa_mode = false;
        layer_forced_alfa.valid = false;
    }
}

static inline void chord_reset(void) {
    chord.base      = 0;
    chord.peak_mods = 0;
    chord.started   = false;
}

/* =============================================================================
 * [A-3] 送出ヘルパ
 * ============================================================================= */
#ifndef VIA_CUSTOM_START
#define VIA_CUSTOM_START 0x7E00
#endif

static void send_keycode(uint16_t kc) {
    if (kc >= VIA_CUSTOM_START) {
        ims_handle_lang(kc - VIA_CUSTOM_START);
    } else if (kc != KC_NO && kc != KC_TRNS) {
        tap_code16(kc);
    }
}

static void flush_pending(void) {
    if (!pending_lang.valid) return;
    uint16_t kc = dynamic_keymap_get_keycode(L_LANG, pending_lang.row, pending_lang.col);
    pending_lang.valid = false;
    pending_s_key        = 0xFFFF;
    send_keycode(kc);
}

/* =========================================================================
 * B-(1) 送出ヘルパ
 * ========================================================================= */
static void send_with_s_key(uint8_t s_key_layer) {
    if (!pending_lang.valid) return;
    uint16_t kc = dynamic_keymap_get_keycode(s_key_layer, pending_lang.row, pending_lang.col);
    pending_lang.valid = false;
    pending_s_key        = 0xFFFF;
    send_keycode(kc);
}

/* =========================================================================
 * B-a,b : IMS_ALFA_TGL 定義
 * ========================================================================= */
#ifndef IMS_ALFA_TGL_ON
#define IMS_ALFA_TGL_ON  KC_NO
#endif
#ifndef IMS_ALFA_TGL_OFF
#define IMS_ALFA_TGL_OFF KC_NO
#endif

static const uint16_t ims_s_keys[]   = IMS_S_KEYS;
static const uint8_t  ims_s_key_layers[] = IMS_S_KEY_LAYERS;
#define IMS_S_KEY_COUNT (sizeof(ims_s_keys) / sizeof(ims_s_keys[0]))

_Static_assert(
    sizeof(ims_s_keys) / sizeof(ims_s_keys[0])
        == sizeof(ims_s_key_layers) / sizeof(ims_s_key_layers[0]),
    "IMS_S_KEYS と IMS_S_KEY_LAYERS の要素数が一致していません");

typedef enum {
    IMS_MATCH_NONE,    /* マッチなし */
    IMS_MATCH_PASS,    /* マッチ, トリガをホストに流す */
    IMS_MATCH_EAT,     /* マッチ, トリガを握り潰す */
} ims_match_t;

/* =========================================================================
 * ヘルパ
 * ========================================================================= */
static uint8_t s_key_to_layer(uint16_t s_key) {
    for (uint8_t i = 0; i < IMS_S_KEY_COUNT; i++) {
        if (ims_s_keys[i] == s_key) return ims_s_key_layers[i];
    }
    return 0xFF;
}

static uint8_t mod_kc_to_bit(uint16_t kc) {
    switch (kc) {
        case KC_LCTL: return 0x01;
        case KC_LSFT: return 0x02;
        case KC_LALT: return 0x04;
        case KC_LGUI: return 0x08;
        case KC_RCTL: return 0x11;
        case KC_RSFT: return 0x12;
        case KC_RALT: return 0x14;
        case KC_RGUI: return 0x18;
        default:      return 0;
    }
}

static uint16_t mod_bit_to_kc(uint8_t bits) {
    switch (bits) {
        case 0x01: return KC_LCTL;
        case 0x02: return KC_LSFT;
        case 0x04: return KC_LALT;
        case 0x08: return KC_LGUI;
        case 0x11: return KC_RCTL;
        case 0x12: return KC_RSFT;
        case 0x14: return KC_RALT;
        case 0x18: return KC_RGUI;
        default:   return KC_NO;
    }
}

static inline bool is_composed_kc(uint16_t kc) {
#ifdef IS_QK_MODS
    return IS_QK_MODS(kc);
#else
    return (kc >= QK_LCTL && kc <= QK_MODS_MAX);
#endif
}

static inline bool is_custom_kc(uint16_t kc) {
    return (kc >= VIA_CUSTOM_START && kc < (VIA_CUSTOM_START + 0x100));
}

static inline bool is_layer_move_kc(uint16_t kc) {
    return IS_QK_MOMENTARY(kc)
        || IS_QK_TO(kc)
        || IS_QK_TOGGLE_LAYER(kc)
        || IS_QK_ONE_SHOT_LAYER(kc)
        || IS_QK_LAYER_TAP(kc);
}

static uint8_t extract_target_layer(uint16_t kc) {
    if (IS_QK_MOMENTARY(kc))      return kc & 0xFF;
    if (IS_QK_TO(kc))             return kc & 0xFF;
    if (IS_QK_TOGGLE_LAYER(kc))   return kc & 0xFF;
    if (IS_QK_ONE_SHOT_LAYER(kc)) return kc & 0xFF;
    if (IS_QK_LAYER_TAP(kc))      return (kc >> 8) & 0xF;
    return 0xFF;
}

static bool is_lang_layer(uint8_t layer) {
    for (uint8_t i = 0; i < IMS_S_KEY_COUNT; i++) {
        if (ims_s_key_layers[i] == layer) return true;
    }
    return false;
}

/* =========================================================================
 * B-(10) : ALFA_TGL fire (状態更新のみに純粋化)
 * ========================================================================= */
static void alfa_tgl_fire(bool send_key) {
    if (!ime_on) return;

    layer_forced_alfa.valid = false;
    bool was_alfa = alfa_mode;
    alfa_mode = !alfa_mode;

    /* 送出処理はディスパッチャー (Step 2) に譲るため、ここではフラグ更新のみ */
    if (!send_key) return;

    uint16_t kc = was_alfa ? IMS_ALFA_TGL_OFF : IMS_ALFA_TGL_ON;
    if (kc != KC_NO) {
        tap_code16(kc);
    }
}

/* =========================================================================
 * B-(11) : match_other_specials
 * ========================================================================= */
static bool match_other_specials(uint16_t eff) {
    if (eff == IMS_IME_SWITCH) {
        set_ime_on(!ime_on);
        return true;
    }
#ifdef IMS_IME_ON
    if (eff == IMS_IME_ON) {
        if (!ime_on) set_ime_on(true);
        return true;
    }
#endif
#ifdef IMS_IME_OFF
    if (eff == IMS_IME_OFF) {
        if (ime_on) set_ime_on(false);
        return true;
    }
#endif
    if (eff == IMS_RESET) {
        set_ime_on(false);
        pending_lang.valid = false;
        pending_s_key        = 0xFFFF;
        return true;
    }
    if (eff == IMS_BYPASS) {
        ims_bypass = !ims_bypass;
        return true;
    }
    return false;
}

/* =========================================================================
 * B-(12) : try_match_special (ゴミロジックの集約)
 * ========================================================================= */
static ims_match_t try_match_special(uint16_t keycode, keyrecord_t *record) {
    /* --- VIA 合成キー --- */
    if (is_composed_kc(keycode)) {
        if (record->event.pressed) {
            if (keycode == IMS_ALFA_TGL) {
                alfa_tgl_fire(true);
                if (pending_lang.valid) flush_pending();
                return IMS_MATCH_EAT;
            }
            if (match_other_specials(keycode)) {
                if (pending_lang.valid) flush_pending();
                return IMS_MATCH_PASS;
            }
        }
        return IMS_MATCH_NONE;
    }

    /* --- カスタムキーコード --- */
    if (is_custom_kc(keycode)) {
        if (record->event.pressed && keycode == IMS_ALFA_TGL) {
            alfa_tgl_fire(true);
            if (pending_lang.valid) flush_pending();
            return IMS_MATCH_EAT;
        }
        return IMS_MATCH_NONE;
    }

    /* --- モディファイア・キー chord 判定 --- */
    if (IS_MODIFIER_KEYCODE(keycode)) {
        uint8_t bit = mod_kc_to_bit(keycode);
        if (record->event.pressed) {
            chord.mods      |= bit;
            chord.peak_mods |= bit;
            chord.started    = true;
        } else {
            chord.mods &= ~bit;
            if (chord.mods == 0 && chord.started && chord.base == 0) {
                uint16_t eff = mod_bit_to_kc(chord.peak_mods);
                if (eff == IMS_ALFA_TGL) {
                    alfa_tgl_fire(false);
                    chord_reset();
                    if (pending_lang.valid) flush_pending();
                    return IMS_MATCH_PASS;
                }
                if (match_other_specials(eff)) {
                    chord_reset();
                    if (pending_lang.valid) flush_pending();
                    return IMS_MATCH_PASS;
                }
                chord_reset();
            }
        }
        return IMS_MATCH_NONE;
    }

    /* --- 基本 keycode --- */
    if (keycode < 0x0100) {
        if (record->event.pressed) {
            chord.base       = keycode;
            chord.peak_mods |= chord.mods;
            chord.started    = true;
            if (chord.mods == 0) {
                if (keycode == IMS_ALFA_TGL) {
                    alfa_tgl_fire(true);
                    chord_reset();
                    if (pending_lang.valid) flush_pending();
                    return IMS_MATCH_EAT;
                }
                if (match_other_specials(keycode)) {
                    chord_reset();
                    if (pending_lang.valid) flush_pending();
                    passthrough_key.row   = record->event.key.row;
                    passthrough_key.col   = record->event.key.col;
                    passthrough_key.valid = true;
                    return IMS_MATCH_PASS;
                }
                chord_reset();
            }
        } else {
            if (chord.started && chord.base == keycode) {
                if (chord.peak_mods != 0) {
                    uint16_t eff = ((uint16_t)chord.peak_mods << 8) | chord.base;
                    bool was_tracked = false;
                    if (passthrough_key.valid &&
                        passthrough_key.row == record->event.key.row &&
                        passthrough_key.col == record->event.key.col) {
                        passthrough_key.valid = false;
                        was_tracked = true;
                    }

                    if (eff == IMS_ALFA_TGL) {
                        alfa_tgl_fire(false);
                        chord_reset();
                        if (pending_lang.valid) flush_pending();
                        return IMS_MATCH_PASS;
                    }
                    if (match_other_specials(eff)) {
                        chord_reset();
                        if (pending_lang.valid) flush_pending();
                        return IMS_MATCH_PASS;
                    }
                    chord_reset();
                    if (was_tracked) return IMS_MATCH_PASS;
                } else {
                    chord_reset();
                }
            }
        }
        return IMS_MATCH_NONE;
    }

    return IMS_MATCH_NONE;
}

/* =========================================================================
 * B-(13) process_kana
 * ========================================================================= */
static bool process_kana(uint16_t keycode, keyrecord_t *record) {
    /* L_BASE 以外のレイヤが有効なら素通し */
    if (layer_state & ~(layer_state_t)(1UL << L_BASE)) {
        return true;
    }

    /* Mod-passthrough で host に press を送ったキーの release は必ず通す */
    if (!record->event.pressed && passthrough_key.valid &&
        passthrough_key.row == record->event.key.row &&
        passthrough_key.col == record->event.key.col) {
        passthrough_key.valid = false;
        return true;
    }

    uint16_t lang_kc = dynamic_keymap_get_keycode(L_LANG,
                            record->event.key.row, record->event.key.col);

    if (IS_MODIFIER_KEYCODE(keycode)) return true;
    if (get_mods() & (MOD_MASK_CTRL | MOD_MASK_ALT | MOD_MASK_GUI)) {
        if (record->event.pressed) {
            if (pending_lang.valid) flush_pending();
            passthrough_key.row   = record->event.key.row;
            passthrough_key.col   = record->event.key.col;
            passthrough_key.valid = true;
        }
        return true;
    }

    /* 同時押しキー処理 */
    {
        uint8_t s_key_layer = s_key_to_layer(lang_kc);
        if (s_key_layer != 0xFF) {
            if (record->event.pressed) {
                if (pending_lang.valid) send_with_s_key(s_key_layer);
                else {
                    pending_s_key = lang_kc;
                    combo_timer = timer_read();
                }
            } else {
                if (pending_s_key != 0xFFFF) {
                    send_keycode(lang_kc);
                    pending_s_key = 0xFFFF;
                }
            }
            return false;
        }
    }

    if (lang_kc == KC_NO)   return false;
    if (lang_kc == KC_TRNS || lang_kc == KC_BACKSPACE || lang_kc == KC_DELETE) return true;

    if (record->event.pressed) {
        uint8_t s_key_layer = (pending_s_key != 0xFFFF) ? s_key_to_layer(pending_s_key) : 0xFF;
        if (s_key_layer != 0xFF) {
            pending_lang.row   = record->event.key.row;
            pending_lang.col   = record->event.key.col;
            pending_lang.valid = true;
            send_with_s_key(s_key_layer);
        } else if (pending_lang.valid) {
            flush_pending();
            pending_lang.row   = record->event.key.row;
            pending_lang.col   = record->event.key.col;
            pending_lang.valid = true;
            combo_timer = timer_read();
        } else {
            pending_lang.row   = record->event.key.row;
            pending_lang.col   = record->event.key.col;
            pending_lang.valid = true;
            combo_timer = timer_read();
        }
    }
    return false;
}

/* =============================================================================
 * エントリポイント [Bグループ]
 *  process_ims
 * ============================================================================= */
bool process_ims(uint16_t keycode, keyrecord_t *record) {
    if (ims_bypass) {
        (void)try_match_special(keycode, record);
        return true;
    }

    ims_match_t r = try_match_special(keycode, record);
    if (r == IMS_MATCH_PASS) return true;
    if (r == IMS_MATCH_EAT)  return false;

    if (is_composed_kc(keycode)) {
        return process_ims_symbol_conv(keycode, record);
    }

    if (is_layer_move_kc(keycode)) {
        uint8_t target = extract_target_layer(keycode);
        if (is_lang_layer(target)) return false;
        
        bool is_momentary = IS_QK_MOMENTARY(keycode);
        if (record->event.pressed) {
            if (pending_lang.valid) flush_pending();
            if (is_momentary && ime_on && !alfa_mode) {
                alfa_tgl_fire(true);
                layer_forced_alfa.row   = record->event.key.row;
                layer_forced_alfa.col   = record->event.key.col;
                layer_forced_alfa.valid = true;
            }
        } else {
            if (is_momentary && layer_forced_alfa.valid &&
                layer_forced_alfa.row == record->event.key.row &&
                layer_forced_alfa.col == record->event.key.col) {
                layer_forced_alfa.valid = false;
                if (alfa_mode) alfa_tgl_fire(true);
            }
        }
        return true;
    }

    if (!ime_on || alfa_mode) {
        return process_ims_symbol_conv(keycode, record);
    }

    return process_kana(keycode, record);
}

/* =============================================================================
 * エントリポイント [Cグループ] 
 *    ims_matrix_scan
 * ============================================================================= */
void ims_matrix_scan(void) {
    if (!ime_on || alfa_mode) return;
    if (!pending_lang.valid && pending_s_key == 0xFFFF) return;
    if (timer_elapsed(combo_timer) > IMS_COMBO_TIMEOUT) {
        flush_pending();
    }
}

/* =============================================================================
 * エントリポイント [Dグループ]
 *       via_custom_value_command_kb
 * ============================================================================= */
void ims_apply_host_state(bool host_ime_on, bool host_alfa) {
    bool changed = (host_ime_on != ime_on) ||
                   (host_ime_on && (host_alfa != alfa_mode));
    if (!changed) return;

    chord.mods = 0;
    chord_reset();
    passthrough_key.valid   = false;
    pending_lang.valid      = false;
    pending_s_key             = 0xFFFF;
    combo_timer             = 0;
    layer_forced_alfa.valid = false;

    set_ime_on(host_ime_on);
    if (host_ime_on) alfa_mode = host_alfa;
}

#ifdef VIA_ENABLE
#define IMS_VIA_ID_SET_VALUE   0x07
#define IMS_VIA_ID_GET_VALUE   0x08
#define IMS_VIA_ID_UNHANDLED   0xFF
#define IMS_VIA_CHANNEL_ID     0x49
#define IMS_VAL_STATE           0x01
#define IMS_VAL_HELLO           0x02
#define IMS_HID_MAGIC          "IMS1"
#define IMS_HID_PROTO_VER      1

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *cmd_id     = &data[0];
    uint8_t *channel_id = &data[1];
    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    if (*channel_id != IMS_VIA_CHANNEL_ID) {
        *cmd_id = IMS_VIA_ID_UNHANDLED;
        return;
    }

    if (*cmd_id == IMS_VIA_ID_SET_VALUE) {
        switch (*value_id) {
        case IMS_VAL_STATE: {
            bool host_ime_on = (value_data[0] != 0);
            bool host_alfa   = (value_data[1] != 0);
            ims_apply_host_state(host_ime_on, host_alfa);
            break;
        }
        default:
            *cmd_id = IMS_VIA_ID_UNHANDLED;
            return;
        }
    } else if (*cmd_id == IMS_VIA_ID_GET_VALUE) {
        switch (*value_id) {
        case IMS_VAL_HELLO:
            memcpy(&value_data[0], IMS_HID_MAGIC, 4);
            value_data[4] = IMS_HID_PROTO_VER;
            break;
        default:
            *cmd_id = IMS_VIA_ID_UNHANDLED;
            return;
        }
    } else {
        *cmd_id = IMS_VIA_ID_UNHANDLED;
        return;
    }
}
#endif

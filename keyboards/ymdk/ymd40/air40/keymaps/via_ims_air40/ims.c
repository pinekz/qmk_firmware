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
 * 
 */

#include <string.h>
#include "ims_lang.h"
#include "via.h"

extern void ims_handle_lang(uint16_t index);
extern bool process_ims_symbol_conv(uint16_t keycode, keyrecord_t *record);

/* ===================================================================================
 * 1. Definitions (準備定義)
 * =================================================================================== */

typedef struct {
    uint8_t row;
    uint8_t col;
    bool    valid;
} pending_key_t;

typedef struct {
    uint8_t  mods;
    uint8_t  peak_mods;
    uint16_t base;
    bool     started;
} ims_chord_t;

typedef enum {
    IMS_STATE_IDLE,           /* 通常待機状態 */
    IMS_STATE_MOD_HELD,       /* モディファイアキーが押されている状態 */
    IMS_STATE_BASE_PRESSED,  /* ベースキーが押されている状態 */
    IMS_STATE_S_KEY_PENDING   /* s_key（同時押しキー）が入力され、次の入力を待機している状態 */
} ims_state_t;

typedef struct {
    /* 基礎フラグ */
    bool ims_ime_on;
    bool alfa_mode;
    bool ims_bypass;
    ims_state_t state;

    /* キー入力バッファ */
    pending_key_t pending_lang;
    uint16_t      pending_s_key;
    uint16_t      combo_timer;

    /* モディファイア情報 */
    ims_chord_t  chord;

    /* 追跡用バッファ */
    pending_key_t passthrough_key;
    pending_key_t layer_forced_alfa;
} ims_context_t;

/* グローバルコンテキストの初期化 */
static ims_context_t ctx = {
    .ims_ime_on = false,
    .alfa_mode = false,
    .ims_bypass = false,
    .state = IMS_STATE_IDLE,
    .pending_lang = {0, 0, false},
    .pending_s_key = 0xFFFF,
    .combo_timer = 0,
    .chord = {0, 0, 0, false},
    .passthrough_key = {0, 0, false},
    .layer_forced_alfa = {0, 0, false}
};

/* 定数定義 */
#ifndef VIA_CUSTOM_START
#define VIA_CUSTOM_START 0x7E00
#endif

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
    IMS_ACTION_NONE,           /* 何もしない */
    IMS_ACTION_SEND,           /* 普通のキー送信 (tap_code16) */
    IMS_ACTION_SEND_COMPOSITE, /* 記号変換を伴う送信 (process_ims_symbol_conv) */
    IMS_ACTION_PASS,           /* ホストへ素通し (return true) */
    IMS_ACTION_EAT,            /* 処理を握り潰す (return false) */
    IMS_ACTION_ALFA_TOGGLE,   /* 英数/かなトグル実行 */
    IMS_ACTION_LAYER_MOVE,    /* レイヤー移動処理 */
    IMS_ACTION_KANA            /* かな入力エンジンへ渡す */
} ims_action_t;

typedef struct {
    ims_state_t next_state;
    ims_action_t action;
} ims_transition_t;

typedef enum {
    IMS_MATCH_NONE,    /* マッチなし */
    IMS_MATCH_PASS,    /* マッチ, トリガをホストに流す */
    IMS_MATCH_EAT,     /* マッチ, トリガを握り潰す */
} ims_match_t;

/* =============================================================================
 * 2. Helpers  (ヘルパー 関数)
 * ============================================================================= */

/* 2-1. アクション実行ヘルパー */
static void send_keycode(uint16_t kc) {
    if (kc >= VIA_CUSTOM_START) {
        ims_handle_lang(kc - VIA_CUSTOM_START);
    } else if (kc != KC_NO && kc != KC_TRNS) {
        tap_code16(kc);
    }
}

static void send_with_s_key(uint8_t s_key_layer) {
    if (!ctx.pending_lang.valid) return;
    uint16_t kc = dynamic_keymap_get_keycode(s_key_layer, ctx.pending_lang.row, ctx.pending_lang.col);
    ctx.pending_lang.valid = false;
    ctx.pending_s_key        = 0xFFFF;
    send_keycode(kc);
}

static void flush_pending(void) {
    if (!ctx.pending_lang.valid) return;
    uint16_t kc = dynamic_keymap_get_keycode(L_LANG, ctx.pending_lang.row, ctx.pending_lang.col);
    ctx.pending_lang.valid = false;
    ctx.pending_s_key        = 0xFFFF;
    send_keycode(kc);
    ctx.state = IMS_STATE_IDLE;
}

static void alfa_tgl_fire(bool send_key) {
    if (!ctx.ims_ime_on) return;

    ctx.layer_forced_alfa.valid = false;
    bool was_alfa = ctx.alfa_mode;
    ctx.alfa_mode = !ctx.alfa_mode;

    if (!send_key) return;

    uint16_t kc = was_alfa ? IMS_ALFA_TGL_OFF : IMS_ALFA_TGL_ON;
    if (kc != KC_NO) {
        tap_code16(kc);
    }
}

/* 2-2. 状態同期・管理ヘルパー */
static inline void set_ime_on(bool v) {
    ctx.ims_ime_on = v;
    if (!v) {
        ctx.alfa_mode = false;
        ctx.layer_forced_alfa.valid = false;
    }
}

static inline void chord_reset(void) {
    ctx.chord.base      = 0;
    ctx.chord.peak_mods = 0;
    ctx.chord.started   = false;
}

static bool match_other_specials(uint16_t eff) {
    if (eff == IMS_IME_SWITCH) {
        set_ime_on(!ctx.ims_ime_on);
        return true;
    }
#ifdef IMS_IME_ON
    if (eff == IMS_IME_ON) {
        if (!ctx.ims_ime_on) set_ime_on(true);
        return true;
    }
#endif
#ifdef IMS_IME_OFF
    if (eff == IMS_IME_OFF) {
        if (ctx.ims_ime_on) set_ime_on(false);
        return true;
    }
#endif
    if (eff == IMS_RESET) {
        set_ime_on(false);
        ctx.pending_lang.valid = false;
        ctx.pending_s_key        = 0xFFFF;
        ctx.state = IMS_STATE_IDLE;
        return true;
    }
    if (eff == IMS_BYPASS) {
        ctx.ims_bypass = !ctx.ims_bypass;
        return true;
    }
    return false;
}

/* 修正箇所: staticを削除 (ims_lang.hの宣言と一致させる) */
void ims_apply_host_state(bool host_ime_on, bool host_alfa) {
    bool changed = (host_ime_on != ctx.ims_ime_on) ||
                   (host_ime_on && (host_alfa != ctx.alfa_mode));
    if (!changed) return;

    set_ime_on(host_ime_on);
    if (host_ime_on) ctx.alfa_mode = host_alfa;

    if (ctx.state == IMS_STATE_IDLE) {
        ctx.chord.mods = 0;
        chord_reset();
        ctx.passthrough_key.valid   = false;
        ctx.pending_lang.valid      = false;
        ctx.pending_s_key             = 0xFFFF;
        ctx.combo_timer             = 0;
        ctx.layer_forced_alfa.valid = false;
    } else {
        if (!host_ime_on) {
            ctx.layer_forced_alfa.valid = false;
        }
    }
}

/* =============================================================================
 * 3. Single Routing (３つの基本ルート関数)
 * ============================================================================= */

/* 3-1. Modifier属性 */
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

static void update_modifiers(uint16_t kc, bool pressed) {
    if (!IS_MODIFIER_KEYCODE(kc)) return;

    uint8_t bit = mod_kc_to_bit(kc);
    if (pressed) {
        ctx.chord.mods      |= bit;
        ctx.chord.peak_mods |= bit;
        ctx.chord.started    = true;
        ctx.state = IMS_STATE_MOD_HELD;
    } else {
        ctx.chord.mods &= ~bit;
        if (ctx.chord.mods == 0) ctx.state = IMS_STATE_IDLE;
    }
}

/* 3-2. s_key属性 */
static uint8_t s_key_to_layer(uint16_t s_key) {
    for (uint8_t i = 0; i < IMS_S_KEY_COUNT; i++) {
        if (ims_s_keys[i] == s_key) return ims_s_key_layers[i];
    }
    return 0xFF;
}

static bool is_lang_layer(uint8_t layer) {
    for (uint8_t i = 0; i < IMS_S_KEY_COUNT; i++) {
        if (ims_s_key_layers[i] == layer) return true;
    }
    return false;
}

/* 3-3. 文字・記号属性 */
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

/* =============================================================================
 * 4. Combination Routing (基本ルートの組み合わせで判定する関数）
 * ============================================================================= */

/* 4-1. Modifier ＋ 文字・記号 */
static bool match_user_key(uint16_t target_code, keyrecord_t *record) {
    uint16_t event_code = *(uint16_t*)&record->event.key;
    uint8_t target_base = (uint8_t)(target_code & 0xFF); 
    uint8_t event_base = (uint8_t)(event_code & 0xFF); 

    if (target_base != event_base) return false;

    uint8_t target_mods = 0;
    if ((target_code & 0x1F00) != 0) {
        target_mods = (uint8_t)((target_code & 0x1F00) >> 8);
    }

    uint8_t current_mods = get_mods();
    uint8_t event_mods = 0;
    if ((event_code & 0x1F00) != 0) {
        event_mods = (uint8_t)((event_code & 0x1F00) >> 8);
    }

    uint8_t total_actual_mods = current_mods | event_mods;
    return (total_actual_mods & target_mods) == target_mods;
}

static ims_match_t try_match_special(uint16_t keycode, keyrecord_t *record) {
    if (is_composed_kc(keycode)) {
        if (record->event.pressed) {
            if (keycode == IMS_ALFA_TGL) {
                alfa_tgl_fire(true);
                if (ctx.pending_lang.valid) flush_pending();
                return IMS_MATCH_EAT;
            }
            if (match_other_specials(keycode)) {
                if (ctx.pending_lang.valid) flush_pending();
                return IMS_MATCH_PASS;
            }
        }
        return IMS_MATCH_NONE;
    }

    if (is_custom_kc(keycode)) {
        if (record->event.pressed && keycode == IMS_ALFA_TGL) {
            alfa_tgl_fire(true);
            if (ctx.pending_lang.valid) flush_pending();
            return IMS_MATCH_EAT;
        }
        return IMS_MATCH_NONE;
    }

    if (IS_MODIFIER_KEYCODE(keycode)) {
        update_modifiers(keycode, record->event.pressed);
        if (!record->event.pressed && ctx.chord.mods == 0 && ctx.chord.started && ctx.chord.base == 0) {
            uint16_t eff = mod_bit_to_kc(ctx.chord.peak_mods);
            if (eff == IMS_ALFA_TGL) {
                alfa_tgl_fire(false);
                chord_reset();
                if (ctx.pending_lang.valid) flush_pending();
                return IMS_MATCH_PASS;
            }
            if (match_other_specials(eff)) {
                chord_reset();
                if (ctx.pending_lang.valid) flush_pending();
                return IMS_MATCH_PASS;
            }
            chord_reset();
        }
        return IMS_MATCH_NONE;
    }

    if (keycode < 0x1000) {
        if (record->event.pressed) {
            ctx.chord.base       = keycode;
            ctx.chord.peak_mods |= ctx.chord.mods;
            ctx.chord.started    = true;
            ctx.state = IMS_STATE_BASE_PRESSED;
            if (ctx.chord.mods == 0) {
                if (keycode == IMS_ALFA_TGL) {
                    alfa_tgl_fire(true);
                    chord_reset();
                    if (ctx.pending_lang.valid) flush_pending();
                    return IMS_MATCH_EAT;
                }
                if (match_other_specials(keycode)) {
                    chord_reset();
                    if (ctx.pending_lang.valid) flush_pending();
                    ctx.passthrough_key.row   = record->event.key.row;
                    ctx.passthrough_key.col   = record->event.key.col;
                    ctx.passthrough_key.valid = true;
                    return IMS_MATCH_PASS;
                }
                chord_reset();
            }
        } else {
            if (ctx.chord.started && ctx.chord.base == keycode) {
                if (ctx.chord.peak_mods != 0) {
                    uint16_t eff = ((uint16_t)ctx.chord.peak_mods << 8) | ctx.chord.base;
                    bool was_tracked = false;
                    if (ctx.passthrough_key.valid &&
                        ctx.passthrough_key.row == record->event.key.row &&
                        ctx.passthrough_key.col == record->event.key.col) {
                        ctx.passthrough_key.valid = false;
                        was_tracked = true;
                    }

                    if (eff == IMS_ALFA_TGL) {
                        alfa_tgl_fire(false);
                        chord_reset();
                        if (ctx.pending_lang.valid) flush_pending();
                        return IMS_MATCH_PASS;
                    }
                    if (match_other_specials(eff)) {
                        chord_reset();
                        if (ctx.pending_lang.valid) flush_pending();
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

/* 4-2. s_key ＋ 文字・記号 */
bool transition_kana_state(uint16_t lang_kc, keyrecord_t *record) {
    if (layer_state & ~(layer_state_t)(1UL << L_BASE)) {
        return true;
    }

    uint8_t s_key_layer = s_key_to_layer(lang_kc);
    
    if (s_key_layer != 0xFF) {
        if (record->event.pressed) {
            if (ctx.pending_lang.valid) {
                send_with_s_key(s_key_layer);
            } else {
                ctx.pending_s_key = lang_kc;
                ctx.combo_timer = timer_read();
                ctx.state = IMS_STATE_S_KEY_PENDING;
            }
        } else {
            if (ctx.pending_s_key != 0xFFFF) {
                send_keycode(lang_kc);
                ctx.pending_s_key = 0xFFFF;
                ctx.state = IMS_STATE_IDLE;
            }
        }
        return false;
    }

    if (lang_kc == KC_NO)   return false;
    if (lang_kc == KC_TRNS || lang_kc == KC_BACKSPACE || lang_kc == KC_DELETE) return true;

    if (record->event.pressed) {
        uint8_t current_s_key_layer = (ctx.pending_s_key != 0xFFFF) ? s_key_to_layer(ctx.pending_s_key) : 0xFF;
        
        if (current_s_key_layer != 0xFF) {
            ctx.pending_lang.row   = record->event.key.row;
            ctx.pending_lang.col   = record->event.key.col;
            ctx.pending_lang.valid = true;
            send_with_s_key(current_s_key_layer);
        } else if (ctx.pending_lang.valid) {
            flush_pending();
            ctx.pending_lang.row   = record->event.key.row;
            ctx.pending_lang.col   = record->event.key.col;
            ctx.pending_lang.valid = true;
            ctx.combo_timer = timer_read();
        } else {
            ctx.pending_lang.row   = record->event.key.row;
            ctx.pending_lang.col   = record->event.key.col;
            ctx.pending_lang.valid = true;
            ctx.combo_timer = timer_read();
            ctx.state = IMS_STATE_S_KEY_PENDING;
        }
    }
    return false;
}

/* 4-3. レイヤー ＋ 文字・記号 */
ims_transition_t resolve_ims_routing(uint16_t keycode, keyrecord_t *record) {
    ims_transition_t trans = { .next_state = IMS_STATE_IDLE, .action = IMS_ACTION_NONE };

    if (is_composed_kc(keycode)) {
        if (keycode == IMS_ALFA_TGL) {
            trans.action = IMS_ACTION_ALFA_TOGGLE;
        } else {
            trans.action = IMS_ACTION_SEND_COMPOSITE;
        }
        return trans;
    }

    if (is_custom_kc(keycode)) {
        if (keycode == IMS_ALFA_TGL) {
            trans.action = IMS_ACTION_ALFA_TOGGLE;
        }
        return trans;
    }

    if (is_layer_move_kc(keycode)) {
        uint8_t target = extract_target_layer(keycode);
        if (is_lang_layer(target)) {
            trans.action = IMS_ACTION_PASS; 
        } else {
            trans.action = IMS_ACTION_LAYER_MOVE;
        }
        return trans;
    }

    if (match_user_key(IMS_ALFA_TGL, record)) {
        trans.action = IMS_ACTION_ALFA_TOGGLE;
        return trans;
    }
    if (match_user_key(IMS_IME_SWITCH, record)) {
        trans.action = IMS_ACTION_SEND_COMPOSITE;
        return trans;
    }
    if (match_user_key(IMS_IME_ON, record)) {
        trans.action = IMS_ACTION_SEND_COMPOSITE;
        return trans;
    }
    if (match_user_key(IMS_IME_OFF, record)) {
        trans.action = IMS_ACTION_SEND_COMPOSITE;
        return trans;
    }
    if (match_user_key(IMS_BYPASS, record)) {
        trans.action = IMS_ACTION_SEND_COMPOSITE;
        return trans;
    }
    if (match_user_key(IMS_RESET, record)) {
        trans.action = IMS_ACTION_SEND_COMPOSITE;
        return trans;
    }

    if (!ctx.ims_ime_on || ctx.alfa_mode) {
        trans.action = IMS_ACTION_SEND_COMPOSITE;
    } else {
        trans.action = IMS_ACTION_KANA;
    }

    return trans;
}

/* =============================================================================
 * 5. エントリポイントA (process_ims)
 * ============================================================================= */

bool process_ims(uint16_t keycode, keyrecord_t *record) {
    if (ctx.ims_bypass) {
        (void)try_match_special(keycode, record);
        return true;
    }

    ims_match_t r = try_match_special(keycode, record);
    if (r == IMS_MATCH_PASS) return true;
    if (r == IMS_MATCH_EAT)  return false;

    ims_transition_t trans = resolve_ims_routing(keycode, record);

    switch (trans.action) {
        case IMS_ACTION_ALFA_TOGGLE:
            if (record->event.pressed) {
                alfa_tgl_fire(true);
                if (ctx.pending_lang.valid) flush_pending();
                return false; 
            }
            return true;

        case IMS_ACTION_SEND_COMPOSITE:
            return process_ims_symbol_conv(keycode, record);

        case IMS_ACTION_LAYER_MOVE:
            uint8_t target = extract_target_layer(keycode);
            if (is_lang_layer(target)) {
                return false; 
            }
            
            bool is_momentary = IS_QK_MOMENTARY(keycode);
            if (record->event.pressed) {
                if (ctx.pending_lang.valid) flush_pending();
                if (is_momentary && ctx.ims_ime_on && !ctx.alfa_mode) {
                    alfa_tgl_fire(true);
                    ctx.layer_forced_alfa.row   = record->event.key.row;
                    ctx.layer_forced_alfa.col   = record->event.key.col;
                    ctx.layer_forced_alfa.valid = true;
                }
            } else {
                if (is_momentary && ctx.layer_forced_alfa.valid &&
                    ctx.layer_forced_alfa.row == record->event.key.row &&
                    ctx.layer_forced_alfa.col == record->event.key.col) {
                    ctx.layer_forced_alfa.valid = false;
                    if (ctx.alfa_mode) alfa_tgl_fire(true);
                }
            }
            return true;

        case IMS_ACTION_PASS:
            return true;

        case IMS_ACTION_KANA:
            uint16_t lang_kc = dynamic_keymap_get_keycode(L_LANG,
                            record->event.key.row, record->event.key.col);
            if (IS_MODIFIER_KEYCODE(keycode)) return true;
            if (get_mods() & (MOD_MASK_CTRL | MOD_MASK_ALT | MOD_MASK_GUI)) {
                if (record->event.pressed) {
                    if (ctx.pending_lang.valid) flush_pending();
                    ctx.passthrough_key.row   = record->event.key.row;
                    ctx.passthrough_key.col   = record->event.key.col;
                    ctx.passthrough_key.valid = true;
                }
                return true;
            }
            return transition_kana_state(lang_kc, record);

        case IMS_ACTION_SEND:
        default:
            return true;
    }
}

/* =============================================================================
 * 6. エントリポイントB (ims_matrix_scan)
 * ============================================================================= */

void ims_matrix_scan(void) {
    if (!ctx.ims_ime_on || ctx.alfa_mode) return;
    if (!ctx.pending_lang.valid && ctx.pending_s_key == 0xFFFF) return;
    if (timer_elapsed(ctx.combo_timer) > IMS_COMBO_TIMEOUT) {
        flush_pending();
    }
}

/* =============================================================================
 * 7. エントリポイントC (via_custom_value_command_kb)
 * ============================================================================= */

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

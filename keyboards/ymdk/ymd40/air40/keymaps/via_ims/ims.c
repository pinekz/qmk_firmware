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

/* =============================================================================
 * IMS 新アーキテクチャ (解釈A: IMS_ALFA_TGL 方向別送出)
 *
 * 設計方針 (詳細は _ims_design_notes.md §15 参照):
 *
 *   process_ims:
 *     (1-1) try_match_special  - chord 蓄積 + 特殊キー判定 (3値戻り)
 *     (1-2) モード判定          - かな成立 = ime_on && !alfa_mode
 *     (1-3) 英数素通し
 *     (2)  process_kana        - かなモード処理 (封止)
 *
 *   特殊キー5種: IMS_IME_SWITCH / IMS_IME_ON / IMS_IME_OFF / IMS_ALFA_TGL / IMS_RESET
 *
 *   IMS_ALFA_TGL の特殊扱い:
 *     - VIA 合成 / カスタムキーコード / 単独基本キーでマッチ:
 *         トリガ握り潰し (return false) + 方向別キー送出
 *     - 物理 chord (Ctrl press + U press + Ctrl release) でマッチ:
 *         既にホストに流れているので状態変更のみ (return true)
 *
 *   chord 確定:
 *     - 物理 chord     : 構成 mod が全部離れた瞬間に確定
 *     - VIA 合成 chord : press 時点で即確定 (1イベントで届くため)
 *
 *   不変条件: alfa_mode=true なら必ず ime_on=true
 *             → set_ime_on() ヘルパで強制
 * ============================================================================= */

#include "ims_lang.h"
#include "via.h"

extern void ims_handle_lang(uint16_t index);

#ifndef VIA_CUSTOM_START
#define VIA_CUSTOM_START 0x7E00
#endif

#ifndef IMS_IME_ON_SUB
#define IMS_IME_ON_SUB  KC_NO
#endif

#ifndef IMS_ALFA_TGL_ON
#define IMS_ALFA_TGL_ON  KC_NO
#endif
#ifndef IMS_ALFA_TGL_OFF
#define IMS_ALFA_TGL_OFF KC_NO
#endif

/* =========================================================================
 * 定数テーブル
 * ========================================================================= */

static const uint8_t  ims_mod_layers[IMS_MOD_COUNT] = IMS_MOD_LAYERS;
static const uint16_t ims_mod_keys[IMS_MOD_COUNT]   = IMS_MOD_KEYS;

/* =========================================================================
 * 特殊キー判定の戻り値
 * ========================================================================= */

typedef enum {
    IMS_MATCH_NONE,    /* マッチなし : 通常処理に進む */
    IMS_MATCH_PASS,    /* マッチ, トリガキーをホストに流す (return true)  */
    IMS_MATCH_EAT,     /* マッチ, トリガキーを握り潰す   (return false) */
} ims_match_t;

/* =========================================================================
 * 状態変数 (3軸構成)
 * ========================================================================= */

/* --- モード軸 --- */
static bool ime_on    = false;
static bool alfa_mode = false;

/* --- chord 軸 (特殊キー判定用) ---
 * mods      : 保持中の mod mask (QK形式 5bit: LCTL=1, LSFT=2, LALT=4, LGUI=8, R flag=0x10)
 * peak_mods : chord 中に立った mod の和集合 (複数 mod chord の確定用)
 *             mods は release で減算されるため、複数 mod を順に離すと最後の1 bitしか
 *             残らない。peak_mods は press のみで蓄積し release では減算しないため、
 *             「この chord で押された全 mod」を保持できる。
 * base      : 最後に押された基本 keycode (0 = 未設定)
 * started   : chord が育成中か
 */
typedef struct {
    uint8_t  mods;
    uint8_t  peak_mods;
    uint16_t base;
    bool     started;
} ims_chord_t;

static ims_chord_t chord = {0, 0, 0, false};

/* --- Combo 軸 (かな処理用) --- */
typedef struct {
    uint8_t row;
    uint8_t col;
    bool    valid;
} pending_key_t;

static pending_key_t pending_lang = {0, 0, false};
static uint16_t      pending_mod  = 0xFFFF;
static uint16_t      combo_timer  = 0;

/* --- 素通しキー追跡 ---
 * Mod(Ctrl/Alt/GUI) 保持中の passthrough で host に press を送ったキーの
 * (row,col) を記録。Mod を先に離してからキー release が来た場合、
 * release を eat せず host に送る必要がある (さもないと host 側でキーが
 * stuck してオートリピートで暴走する)。
 */
static pending_key_t passthrough_key = {0, 0, false};

/* --- レイヤ移動による alfa 強制トラッキング ---
 * MO 系のキーが kana mode から alfa mode への遷移をトリガしたとき、そのキーの
 * (row,col) を記録。同じキーの release で alfa mode を戻す (IMS_ALFA_TGL_OFF 送出)。
 * これにより「MO(1) 押下中はレイヤ1のキーが Alfa モードとして処理される」
 * (IMS on/off 無関係) を実現する。
 */
static pending_key_t layer_forced_alfa = {0, 0, false};

/* --- 副作用キュー --- */
static bool ime_on_sub_pending = false;

/* =========================================================================
 * ヘルパ
 * ========================================================================= */

/* 不変条件: ime_on=false なら alfa_mode も必ず false。
 * ime_off 遷移時は MO による alfa 強制トラッキングも無効化。 */
static inline void set_ime_on(bool v) {
    ime_on = v;
    if (!v) {
        alfa_mode = false;
        layer_forced_alfa.valid = false;
    }
}

/* 組キー keycode → L_LANGn レイヤ番号 (該当なし 0xFF) */
static uint8_t mod_to_layer(uint16_t mod_key) {
    for (uint8_t i = 0; i < IMS_MOD_COUNT; i++) {
        if (ims_mod_keys[i] == mod_key) return ims_mod_layers[i];
    }
    return 0xFF;
}

/* 純粋 modifier keycode (KC_LCTL 等) → QK形式 mod bit */
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

/* 単独 QK mod bit → modifier keycode 逆引き */
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

/* VIA 合成キーコード判定 */
static inline bool is_composed_kc(uint16_t kc) {
#ifdef IS_QK_MODS
    return IS_QK_MODS(kc);
#else
    return (kc >= QK_LCTL && kc <= QK_MODS_MAX);
#endif
}

/* カスタムキーコード判定 (VIA CUSTOM 範囲) */
static inline bool is_custom_kc(uint16_t kc) {
    return (kc >= VIA_CUSTOM_START && kc < (VIA_CUSTOM_START + 0x100));
}

/* レイヤ移動キー (MO/TO/TG/OSL/LT) 判定 */
static inline bool is_layer_move_kc(uint16_t kc) {
    return IS_QK_MOMENTARY(kc)
        || IS_QK_TO(kc)
        || IS_QK_TOGGLE_LAYER(kc)
        || IS_QK_ONE_SHOT_LAYER(kc)
        || IS_QK_LAYER_TAP(kc);
}

/* レイヤ移動キー → 移動先レイヤ番号 (該当なし 0xFF) */
static uint8_t extract_target_layer(uint16_t kc) {
    if (IS_QK_MOMENTARY(kc))      return kc & 0xFF;
    if (IS_QK_TO(kc))             return kc & 0xFF;
    if (IS_QK_TOGGLE_LAYER(kc))   return kc & 0xFF;
    if (IS_QK_ONE_SHOT_LAYER(kc)) return kc & 0xFF;
    if (IS_QK_LAYER_TAP(kc))      return (kc >> 8) & 0xF;
    return 0xFF;
}

/* 指定レイヤが L_LANGn のいずれかか */
static bool is_lang_layer(uint8_t layer) {
    for (uint8_t i = 0; i < IMS_MOD_COUNT; i++) {
        if (ims_mod_layers[i] == layer) return true;
    }
    return false;
}

/* chord 状態リセット */
static inline void chord_reset(void) {
    chord.base      = 0;
    chord.peak_mods = 0;
    chord.started   = false;
}

/* =========================================================================
 * 送出ヘルパ
 * ========================================================================= */

static void send_keycode(uint16_t kc) {
    if (kc >= VIA_CUSTOM_START) {
        ims_handle_lang(kc - VIA_CUSTOM_START);
    } else if (kc != KC_NO && kc != KC_TRNS) {
        tap_code16(kc);
    }
}

static void send_with_mod(uint8_t mod_layer) {
    if (!pending_lang.valid) return;
    uint16_t kc = dynamic_keymap_get_keycode(mod_layer, pending_lang.row, pending_lang.col);
    pending_lang.valid = false;
    pending_mod        = 0xFFFF;
    send_keycode(kc);
}

static void flush_pending(void) {
    if (!pending_lang.valid) return;
    uint16_t kc = dynamic_keymap_get_keycode(L_LANG, pending_lang.row, pending_lang.col);
    pending_lang.valid = false;
    pending_mod        = 0xFFFF;
    send_keycode(kc);
}

/* =========================================================================
 * タイマ監視 (matrix_scan_user から呼ぶ)
 * ========================================================================= */

void ims_matrix_scan(void) {
    if (!ime_on || alfa_mode) return;
    if (!pending_lang.valid && pending_mod == 0xFFFF) return;
    if (timer_elapsed(combo_timer) > IMS_COMBO_TIMEOUT) {
        flush_pending();
    }
}

/* =========================================================================
 * ALFA_TGL fire : 状態トグル + 方向別送出
 *
 * send_key = true  : 方向別キー送出 (トリガを握り潰すケース)
 * send_key = false : 状態変更のみ (物理 chord でトリガが既にホスト到達済み)
 * ========================================================================= */

static void alfa_tgl_fire(bool send_key) {
    if (!ime_on) {
        return;
    }
    /* alfa 状態が変わる瞬間、MO による alfa 強制トラッキングは無効化する。
     * MO hold 中に手動で IMS_ALFA_TGL が押された場合、MO release 時に
     * 二重トグル (状態が逆転) するのを防ぐ。MO press ハンドラはこの後で
     * 改めて layer_forced_alfa をセットし直すので、MO 経由の正常フローは影響なし。 */
    layer_forced_alfa.valid = false;

    bool was_alfa = alfa_mode;
    alfa_mode = !alfa_mode;
    if (!send_key) return;

    uint16_t kc = was_alfa ? IMS_ALFA_TGL_OFF : IMS_ALFA_TGL_ON;
    if (kc != KC_NO) {
        tap_code16(kc);
    }
}

/* =========================================================================
 * IMS_ALFA_TGL 以外の特殊キーマッチ処理 (状態変更のみ)
 * 戻り値: マッチしたら true
 * ========================================================================= */

static bool match_other_specials(uint16_t eff) {
    if (eff == IMS_IME_SWITCH) {
        if (ime_on) {
            set_ime_on(false);
        } else {
            set_ime_on(true);
            if (IMS_IME_ON_SUB != KC_NO) ime_on_sub_pending = true;
        }
        return true;
    }

#ifdef IMS_IME_ON
    if (eff == IMS_IME_ON) {
        if (!ime_on) {
            set_ime_on(true);
            if (IMS_IME_ON_SUB != KC_NO) ime_on_sub_pending = true;
        }
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
        pending_mod        = 0xFFFF;
        return true;
    }

    return false;
}

/* =========================================================================
 * (1-1) 特殊キー chord 判定
 * ========================================================================= */

static ims_match_t try_match_special(uint16_t keycode, keyrecord_t *record) {
    /* --- VIA 合成キー : press 時点で即判定 --- */
    if (is_composed_kc(keycode)) {
        if (record->event.pressed) {
            if (keycode == IMS_ALFA_TGL) {
                alfa_tgl_fire(true);  /* トリガ握り潰し + 方向別送出 */
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

    /* --- カスタムキーコード : press 時点で即判定 (IMS_ALFA_TGL のみ対象) --- */
    if (is_custom_kc(keycode)) {
        if (record->event.pressed && keycode == IMS_ALFA_TGL) {
            alfa_tgl_fire(true);
            if (pending_lang.valid) flush_pending();
            return IMS_MATCH_EAT;
        }
        /* 他のカスタムキーコードは process_kana 側で ims_handle_lang 経由で処理 */
        return IMS_MATCH_NONE;
    }

    /* --- 純粋 modifier : chord_mods を蓄積/削減 --- */
    if (IS_MODIFIER_KEYCODE(keycode)) {
        uint8_t bit = mod_kc_to_bit(keycode);
        if (record->event.pressed) {
            chord.mods      |= bit;
            chord.peak_mods |= bit;        /* chord 中に立った mod を和集合で蓄積 */
            chord.started    = true;
        } else {
            chord.mods &= ~bit;
            /* 純粋 mod chord (base なし) のみここで確定。
             * base 有り chord は base release 側で確定するため、ここでは触らない。
             * これにより複数 mod の release 順序は chord の意味論に影響しない。 */
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

    /* --- 基本 keycode (0x00-0xFF) : chord_base を更新 --- */
    if (keycode < 0x0100) {
        if (record->event.pressed) {
            chord.base       = keycode;
            chord.peak_mods |= chord.mods;   /* base press 時点で保持中の mod を取り込む */
            chord.started    = true;
            /* chord.mods == 0 の単独基本キーは press 時点で即判定 */
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
                    return IMS_MATCH_PASS;
                }
                chord_reset();
            }
        } else {
            /* base release 時点で chord 確定。
             * peak_mods + chord.base で eff を再構成。mod release 順に依存しない。 */
            if (chord.started && chord.base == keycode) {
                if (chord.peak_mods != 0) {
                    uint16_t eff = ((uint16_t)chord.peak_mods << 8) | chord.base;
                    /* このキーが mod 保持 passthrough で track されていたら解除。
                     * was_tracked を保持し、特殊にマッチしなかった場合でも release は
                     * MATCH_PASS で host に送らないと、process_kana 側で eat されて
                     * host 側でキーが stuck しオートリピートで暴走する (Alt+A 等)。 */
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
                    /* 特殊にマッチしなかったが passthrough で track 済みだった key の
                     * release は必ず host に届ける (stuck 防止)。 */
                    if (was_tracked) return IMS_MATCH_PASS;
                } else {
                    /* peak_mods == 0: 単独 base の release (即判定済み) → 何もしない */
                    chord_reset();
                }
            }
        }
        return IMS_MATCH_NONE;
    }

    /* --- その他 (レイヤキー QK_MOMENTARY 等) : chord 対象外 --- */
    return IMS_MATCH_NONE;
}

/* =========================================================================
 * (2) かなモード処理
 * ========================================================================= */

static bool process_kana(uint16_t keycode, keyrecord_t *record) {
    /* L_BASE 以外のレイヤが有効なら素通し */
    {
        layer_state_t layer_mask = layer_state & ~(layer_state_t)(1UL << L_BASE);
        if (layer_mask) {
            return true;
        }
    }

    /* Mod-passthrough で host に press を送ったキーの release は必ず通す。
     * (Mod を先に離してから key を離すと、この時点で get_mods() は既に 0 に
     * なっており、通常経路では release が eat されて host 側で stuck する) */
    if (!record->event.pressed && passthrough_key.valid &&
        passthrough_key.row == record->event.key.row &&
        passthrough_key.col == record->event.key.col) {
        passthrough_key.valid = false;
        return true;
    }

    uint16_t lang_kc = dynamic_keymap_get_keycode(L_LANG,
                            record->event.key.row, record->event.key.col);
    /* 素通し判定 */
    if (IS_MODIFIER_KEYCODE(keycode)) return true;
    if (get_mods() & (MOD_MASK_CTRL | MOD_MASK_ALT | MOD_MASK_GUI)) {
        if (record->event.pressed) {
            /* 保留中のかなキーを先に送出して順序を整える */
            if (pending_lang.valid) flush_pending();
            /* release 時に passthrough できるよう (row,col) を記録 */
            passthrough_key.row   = record->event.key.row;
            passthrough_key.col   = record->event.key.col;
            passthrough_key.valid = true;
        }
        return true;
    }

    /* 組キー処理 */
    {
        uint8_t mod_layer = mod_to_layer(lang_kc);
        if (mod_layer != 0xFF) {
            if (record->event.pressed) {
                if (pending_lang.valid) {
                    send_with_mod(mod_layer);
                } else {
                    pending_mod = lang_kc;
                    combo_timer = timer_read();
                }
            } else {
                if (pending_mod != 0xFFFF) {
                    send_keycode(lang_kc);
                    pending_mod = 0xFFFF;
                }
            }
            return false;
        }
    }

    /* かなキー処理 */
    if (lang_kc == KC_NO || lang_kc == KC_TRNS) return true;

    if (record->event.pressed) {
        uint8_t mod_layer = (pending_mod != 0xFFFF) ? mod_to_layer(pending_mod) : 0xFF;
        if (mod_layer != 0xFF) {
            pending_lang.row   = record->event.key.row;
            pending_lang.col   = record->event.key.col;
            pending_lang.valid = true;
            send_with_mod(mod_layer);
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

/* =========================================================================
 * ホスト側 IME 監視ソフトからの状態注入
 *
 * SET STATE 到着時:
 *   - 新状態が現状と一致 → 何もしない (idempotent 再通知で入力を壊さない)
 *   - 新状態が現状と相違 → バッファに何が残っていても全クリアし、
 *                           入力待ち状態にして新状態を適用する
 *
 * 全クリア対象: chord (mods/peak/base/started) / passthrough_key / pending_lang /
 *               pending_mod / combo_timer / ime_on_sub_pending / layer_forced_alfa
 * ========================================================================= */
void ims_apply_host_state(bool host_ime_on, bool host_alfa) {
    bool changed = (host_ime_on != ime_on) ||
                   (host_ime_on && (host_alfa != alfa_mode));
    if (!changed) {
        return;
    }

    /* 全バッファクリア */
    chord.mods = 0;
    chord_reset();
    passthrough_key.valid   = false;
    pending_lang.valid      = false;
    pending_mod             = 0xFFFF;
    combo_timer             = 0;
    ime_on_sub_pending      = false;
    layer_forced_alfa.valid = false;

    /* 新状態を適用。set_ime_on(false) は alfa_mode=false に固定するので
     * alfa 代入は set_ime_on の後に行う。 */
    set_ime_on(host_ime_on);
    if (host_ime_on) {
        alfa_mode = host_alfa;
    }
}

/* =========================================================================
 * エントリポイント
 * ========================================================================= */

bool process_ims(uint16_t keycode, keyrecord_t *record) {
    /* (1-1) 特殊キー chord 判定 (3値戻り) */
    ims_match_t r = try_match_special(keycode, record);
    if (r == IMS_MATCH_PASS) {
        return true;
    }
    if (r == IMS_MATCH_EAT) {
        return false;
    }

    /* VIA 合成キーは chord 管理外、常にホストに流す */
    if (is_composed_kc(keycode)) {
        return true;
    }

    /* レイヤ移動キー (MO/TO/TG/OSL/LT) : IME on/off に関係なく一元処理。
     * (1) 移動先が L_LANGn : 無視 (QMK にも渡さない) → eat
     * (2) それ以外 (非 LANG レイヤ):
     *     - MO は momentary: press で kana mode なら alfa 強制 ON
     *                         (IMS_ALFA_TGL_ON を OS 送出 → IME が ASCII モードに)
     *                         release で強制的に元へ戻す
     *     - TO/TG/OSL/LT : 単純に QMK へ委譲。必要なら user が manual で alfa 切替
     *                       (TO/TG は永続なので自動復帰不能。LT は tap/hold 判定
     *                        の都合上、現段階では自動強制しない)
     */
    if (is_layer_move_kc(keycode)) {
        uint8_t target = extract_target_layer(keycode);
        if (is_lang_layer(target)) {
            return false;
        }
        bool is_momentary = IS_QK_MOMENTARY(keycode);
        if (record->event.pressed) {
            if (pending_lang.valid) flush_pending();
            if (is_momentary && ime_on && !alfa_mode) {
                /* 順序重要: alfa_tgl_fire 内で layer_forced_alfa.valid=false にされるので、
                 * 呼び出し後に改めて (row,col) + valid=true をセットする。 */
                alfa_tgl_fire(true);  /* alfa_mode=true + IMS_ALFA_TGL_ON 送出 */
                layer_forced_alfa.row   = record->event.key.row;
                layer_forced_alfa.col   = record->event.key.col;
                layer_forced_alfa.valid = true;
            }
        } else {
            /* release: このキーが alfa を強制していたら戻す。
             * layer_forced_alfa.valid が途中で手動 IMS_ALFA_TGL や IME_OFF により
             * クリアされていた場合は何もしない (二重トグル防止)。 */
            if (is_momentary && layer_forced_alfa.valid &&
                layer_forced_alfa.row == record->event.key.row &&
                layer_forced_alfa.col == record->event.key.col) {
                layer_forced_alfa.valid = false;
                if (alfa_mode) {
                    alfa_tgl_fire(true);  /* alfa_mode=false + IMS_ALFA_TGL_OFF 送出 */
                }
            }
        }
        return true;
    }

    /* (1-2) モード判定 */
    if (!ime_on || alfa_mode) {
        /* (1-3) 英数モード : 素通し */
        return true;
    }

    /* (2) かなモード */
    return process_kana(keycode, record);
}

/* =========================================================================
 * post_process : IMS_IME_ON_SUB の後追い送出
 * ========================================================================= */
void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!ime_on_sub_pending || record->event.pressed) return;

    bool is_trigger = (keycode == IMS_IME_SWITCH);
#ifdef IMS_IME_ON
    is_trigger = is_trigger || (keycode == IMS_IME_ON);
#endif

    if (is_trigger) {
        ime_on_sub_pending = false;
        if (IMS_IME_ON_SUB != KC_NO) {
            tap_code16(IMS_IME_ON_SUB);
        }
    }
}

/* =========================================================================
 * Host IME monitor integration (VIA Custom Value protocol)
 *
 * ホスト PC 常駐の IME 監視ソフトから Raw HID でモード実状態を受信する。
 *
 * VIA の raw_hid_receive は id_custom_set_value / id_custom_get_value を
 * via_custom_value_command_kb() に委譲する。ここで channel_id=IMS_VIA_CHANNEL_ID
 * 宛のリクエストだけを捕まえて処理し、その他は id_unhandled で返す。
 *
 *   HELLO    (host が GET) : キーボードが "IMS1"+version を in-place で返す
 *   STATE    (host が SET) : [3]=ime_on(0/1) [4]=alfa(0/1)
 *   HEARTBEAT(host が SET) : Phase 1 は消費のみ
 *
 * VIA フレームワークが応答 raw_hid_send を自動で行うので、本関数内で
 * raw_hid_send を呼ばないこと (VIA ドキュメント明記)。
 *
 * 前提:
 *   - VIA_ENABLE = yes (本ファーム全体が VIA 前提のため既に満たしている)
 *   - VIA を無効化するユーザーは監視ソフト連携機能を失うが、blind IMS は継続動作
 * ========================================================================= */
#ifdef VIA_ENABLE

/* VIA コマンド ID (via_command_id.h に依存しないよう直定義) */
#define IMS_VIA_ID_SET_VALUE   0x07
#define IMS_VIA_ID_GET_VALUE   0x08
#define IMS_VIA_ID_UNHANDLED   0xFF

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *cmd_id     = &data[0];
    uint8_t *channel_id = &data[1];
    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    /* 担当 channel でなければ未処理を返す (他の channel ハンドラに譲る) */
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
        case IMS_VAL_HEARTBEAT:
            /* Phase 1: 受信して捨てる (将来 host_monitor_alive 用) */
            break;
        default:
            *cmd_id = IMS_VIA_ID_UNHANDLED;
            return;
        }
    } else if (*cmd_id == IMS_VIA_ID_GET_VALUE) {
        switch (*value_id) {
        case IMS_VAL_HELLO:
            value_data[0] = IMS_HID_MAGIC0;
            value_data[1] = IMS_HID_MAGIC1;
            value_data[2] = IMS_HID_MAGIC2;
            value_data[3] = IMS_HID_MAGIC3;
            value_data[4] = IMS_HID_PROTO_VER;
            break;
        default:
            *cmd_id = IMS_VIA_ID_UNHANDLED;
            return;
        }
    } else {
        /* id_custom_save (0x09) ほか: 担当外 */
        *cmd_id = IMS_VIA_ID_UNHANDLED;
        return;
    }
    /* 戻り値は data[] を VIA 側が raw_hid_send で自動送信 */
}

#endif /* VIA_ENABLE */

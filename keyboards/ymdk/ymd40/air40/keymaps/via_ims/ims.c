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
 *  IMS (Internationalized Multi Shift) keyboard module の構成
 *
 *  ---- ファイル構成 ----
 *
 *  ims_lang.h : ユーザ設定 (起動スイッチ / 同時押しキー / トグルキー /
 *               カスタムキーコード / VIA HID 連携定数)
 *  ims.c      : IMS 本体 (キーイベント処理 / タイマ監視 / VIA HID コールバック)
 *  keymap.c   : QMK フックを ims.c に繋ぐ
 *
 *  ---- エントリポイント (ims.c 内) ----
 *
 *   1.  process_ims                    : キーイベント処理                  呼び出し元: process_record_user［keymap.c］                           
 *   2.  ims_matrix_scan                : combo_timeout 経過チェック        呼び出し元: matrix_scan_user［keymap.c］                   
 *   3.  via_custom_value_command_kb    : IME_monitor からのレポート受信    呼び出し元: VIA framework : raw_hid_receive
 *                                       
 *  ---- 関数・定数・変数要素のグルーピング (ims.c 内) ----
 *
 *   [A]    全体で共用する
 *   [B]    エントリポイント 1.  process_ims
 *   [C]    エントリポイント 2.  ims_matrix_scan
 *   [D]    エントリポイント 3.  via_custom_value_command_kb
 *
 *   番号体系: 大文字 (A-1 等)    = グループ
 *            小文字 (a, b, c …) = 定数 / 配列 / 変数
 *            数字   (1, 2, 3 …) = 型 / 関数
 *
 *  ---- 処理の流れ (process_ims) ----
 *
 *  (1-1) try_match_special                                       [B-(12)]
 *        chord (= 修飾キー単体 (L/R の CTL, SFT, ALT, GUI 8種) + 基本キーの組)
 *        を蓄積しながら、特殊キー (下記 5 種) のマッチを 3 値で返す:
 *          IMS_MATCH_NONE  マッチなし → 通常処理へ
 *          IMS_MATCH_PASS  マッチ + トリガをホストに流す (return true)
 *          IMS_MATCH_EAT   マッチ + トリガを握り潰す     (return false)
 *
 *        特殊キー 5 種 (ims_lang.h で定義):
 *          IMS_IME_SWITCH / IMS_IME_ON / IMS_IME_OFF
 *          IMS_ALFA_TGL   (英数⇔かなトグル)
 *          IMS_RESET      (内部状態リセット)
 *
 *        IMS_ALFA_TGL のみ流入経路で扱いが分岐:
 *          - VIA 合成 / カスタムキーコード / 単独基本キー経由
 *              → トリガ受信で、方向別 (かな/英数) キー送出
 *          - 物理 chord (例: Ctrl press → U press → Ctrl release) 経由
 *              → トリガは既にホストに流れている。状態変更のみ (PASS)
 *
 *        モディファイアchord 確定タイミング:
 *          - 物理 chord     : 構成 mod が全部離れた瞬間に確定
 *          - VIA 合成 chord : press 時点で即確定 (1 イベントで届くため)
 *
 *  (1-2) is_composed_kc / レイヤ移動キーの一元処理
 *        VIA 合成キーは chord 管理外、ホストに流す。
 *        MO レイヤキーは IME on/off に関係なく一元処理 (alfa 強制 ON/OFF)。
 *
 *  (1-3) モード判定
 *        かな成立条件 = ime_on && !alfa_mode
 *        不変条件     : alfa_mode=true なら必ず ime_on=true
 *                       → set_ime_on() ヘルパで強制                [A-2-(1)]
 *        かな不成立なら以降をスキップしてホストに流す (英数素通し)
 *
 *  (2)   process_kana  かなモード処理 (封止)                      [B-(13)]
 *
 *        (2-1) L_BASE 以外のレイヤが ON         → 素通し
 *        (2-2) passthrough_key の release       → 必ず通す (stuck 防止)
 *        (2-3) 修飾キー単体                     → 素通し
 *        (2-4) Ctrl/Alt/GUI 保持中の key        → flush_pending +
 *                                                 passthrough_key 記録 + 素通し
 *        (2-5) 同時押しキー (s_key_to_layer ヒット):
 *                press   pending あり → send_with_s_key            [B-(1)]
 *                        pending なし → pending_s_key セット
 *                release pending_s_key があれば送出
 *        (2-6) かなキー:
 *                KC_NO   → eat (素通しすると L_BASE が漏れる)
 *                KC_TRNS → 素通し
 *                その他 press 時:
 *                  pending_s_key 有り → send_with_s_key
 *                  pending_lang 有り  → flush_pending して新規 pending
 *                  なし               → 新規 pending + combo_timer 開始
 *
 *  詳細は各セクション ([A-1] 〜 [A-3] / [B-(1)] 〜 [B-(13)] / [C] / [D]) のコメント参照。
 *
 * ============================================================================= */

#include <string.h>
#include "ims_lang.h"
#include "via.h"

extern void ims_handle_lang(uint16_t index);

/* ===================================================================================
 * [A-1] 型・変数  (process_ims / ims_matrix_scan / via_custom_value_command_kb で共有)
 * =================================================================================== */

static bool ime_on    = false;
static bool alfa_mode = false;
static bool ims_bypass = false;   /* IMS全体バイパス (ローマ字キーボード化) */

/* pending_key_t 構造体 (呼び出し元: A-2) */
typedef struct {
    uint8_t row;
    uint8_t col;
    bool    valid;
} pending_key_t;

static pending_key_t pending_lang = {0, 0, false};  /* 押されたキーの物理位置を示す */

static uint16_t      pending_s_key = 0xFFFF;
static uint16_t      combo_timer   = 0;

/* =============================================================================
 * [A-2] 型・変数  (process_ims / via_custom_value_command_kb で共有）
 * ============================================================================= */

/* A-2-a  : ims_chord_t 構造体   : chord (モディファイア・キー判定用) ---
 *    mods      : 保持中の mod mask (QK形式 5bit: LCTL=1, LSFT=2, LALT=4, LGUI=8, Right flag=0x10)
 *    peak_mods : mod mask の重なり (複数 mod mask 合計値)
 *                mods は release で減算されるため、複数 mod を順に離すと最後の 1bitしか
 *                残らない。peak_mods は press のみで蓄積し release では減算しないため、
 *               「この chord で押された全 mod」を保持できる。
 *    base      : 最後に押された基本 keycode (0 = 未設定)
 *    started   : 何かキーが押された（chord 生成の可能性ができた）ことを示す
 */
typedef struct {
    uint8_t  mods;
    uint8_t  peak_mods;
    uint16_t base;
    bool     started;
} ims_chord_t;

static ims_chord_t chord = {0, 0, 0, false};   /* 押されたキーの修飾状態を示す */

/* A-2-b : 素通しキー追跡
 *    (呼び出し元: try_match_special / process_kana / ims_apply_host_state)
 *    Mod(Ctrl/Alt/GUI) 保持中の passthrough で host に press を送ったキーの(row,col) を記録。
 *    Mod を先に離してからキー release が来た場合、release を eat せず host に送る必要がある。
 *    (さもないと host 側でキーが stuck してオートリピートで暴走する)。
 */
static pending_key_t passthrough_key = {0, 0, false};  /* 素通しキー追跡 */

/* A-2-c : レイヤ移動による alfa トラッキング
 *     (呼び出し元: set_ime_on / alfa_tgl_fire / process_ims / ims_apply_host_state) 
 *     MO 系のキーが kana mode から alfa mode への遷移をトリガしたとき、そのキーの(row,col) を記録。
 *     同じキーの release で alfa mode を戻す (IMS_ALFA_TGL_OFF 送出)。
 *     これにより「MO(1) 押下中はレイヤ1のキーが Alfa モードとして処理される」
 *     (IMS on/off 無関係) を実現する。
 */
static pending_key_t layer_forced_alfa = {0, 0, false};  /* レイヤ移動による alfa トラッキング */

/* A-2-(1) : set_ime_on
 *    (呼び出し元: match_other_specials / ims_apply_host_state) */
/*     不変条件: ime_on=false なら alfa_mode も必ず false。
 *    「IME OFF なのに alfa モードだけ ON」という矛盾状態を作らないため。
 *     ime_off 遷移時は MO による alfa 強制トラッキングも無効化。
 */
static inline void set_ime_on(bool v) {
    ime_on = v;
    if (!v) {
        alfa_mode = false;
        layer_forced_alfa.valid = false;
    }
}

/* A-2-(2) : chord_reset
 *   (呼び出し元: try_match_special / ims_apply_host_state)
 */
static inline void chord_reset(void) {
    chord.base      = 0;
    chord.peak_mods = 0;
    chord.started   = false;
}

/* =============================================================================
 * [A-3] 送出ヘルパ   (process_ims / ims_matrix_scan で共有・汎用)
 * ============================================================================= */

/* A-3-a : VIA_CUSTOM_START
 */
#ifndef VIA_CUSTOM_START
#define VIA_CUSTOM_START 0x7E00
#endif

/* A-3-(1) : send_keycode
 *   (呼び出し元: send_with_s_key / flush_pending)
 *    CUSTOM KEYCODEならば、ims_handle_lang のテーブル参照に回送する。
 *    または、via で Anykey設定した mod付キーを tap16する。: C(KC_SPC) など。
 */
static void send_keycode(uint16_t kc) {
    if (kc >= VIA_CUSTOM_START) {
        ims_handle_lang(kc - VIA_CUSTOM_START);
    } else if (kc != KC_NO && kc != KC_TRNS) {
        tap_code16(kc);
    }
}

/* A-3-(2) : flush_pending     (呼び出し元: 多数)
 */
static void flush_pending(void) {
    if (!pending_lang.valid) return;
    uint16_t kc = dynamic_keymap_get_keycode(L_LANG, pending_lang.row, pending_lang.col);
    pending_lang.valid = false;
    pending_s_key        = 0xFFFF;
    send_keycode(kc);
}

/* =========================================================================
 * B-(1) 送出ヘルパ   (呼び出し元: process_kana)
 * ========================================================================= */

/* B-(1) : send_with_s_key
 */
static void send_with_s_key(uint8_t s_key_layer) {
    if (!pending_lang.valid) return;
    uint16_t kc = dynamic_keymap_get_keycode(s_key_layer, pending_lang.row, pending_lang.col);
    pending_lang.valid = false;
    pending_s_key        = 0xFFFF;
    send_keycode(kc);
}

/* =============================================================================
 * [B-a,b] IMS_ALFA_TGL_ON/OFF  (process_ims 専用)
 * ============================================================================= */

/* B-a (呼び出し元: alfa_tgl_fire) */
#ifndef IMS_ALFA_TGL_ON
#define IMS_ALFA_TGL_ON  KC_NO
#endif
/* B-b (呼び出し元: alfa_tgl_fire) */
#ifndef IMS_ALFA_TGL_OFF
#define IMS_ALFA_TGL_OFF KC_NO
#endif

/* =========================================================================
 * B-c : IMS_S_KEY_COUNT 算出テーブル
 *
 * IMS_S_KEYS / IMS_S_KEY_LAYERS は ims_lang.h でユーザが定義する。
 * 要素数を算出し。両配列の長さが食い違っていればコンパイル時に止まる。
 * ========================================================================= */

static const uint16_t ims_s_keys[]   = IMS_S_KEYS;
static const uint8_t  ims_s_key_layers[] = IMS_S_KEY_LAYERS;
#define IMS_S_KEY_COUNT (sizeof(ims_s_keys) / sizeof(ims_s_keys[0]))

_Static_assert(
    sizeof(ims_s_keys) / sizeof(ims_s_keys[0])
        == sizeof(ims_s_key_layers) / sizeof(ims_s_key_layers[0]),
    "IMS_S_KEYS と IMS_S_KEY_LAYERS の要素数が一致していません");


/* =========================================================================
 *  B-d : モディファイア・キー判定の戻り値
 * ========================================================================= */
/* ims_match_t 列挙型 (呼び出し元: try_match_special / process_ims)
 */
typedef enum {
    IMS_MATCH_NONE,    /* マッチなし : 通常処理に進む */
    IMS_MATCH_PASS,    /* マッチ, トリガキーをホストに流す (return true)  */
    IMS_MATCH_EAT,     /* マッチ, トリガキーを握り潰す   (return false) */
} ims_match_t;

/* =========================================================================
 * ヘルパ
 * ========================================================================= */

/* B-(2) s_key_to_layer
 *    (呼び出し元: process_kana)
 *     同時押しキー keycode → L_LANGn レイヤ番号 (該当なし 0xFF)
 */
static uint8_t s_key_to_layer(uint16_t s_key) {
    for (uint8_t i = 0; i < IMS_S_KEY_COUNT; i++) {
        if (ims_s_keys[i] == s_key) return ims_s_key_layers[i];
    }
    return 0xFF;
}

/* B-(3) mod_kc_to_bit
 *      (呼び出し元: try_match_special) 
 *     modキー (KC_LCTL 等) → QK形式 mod bit
 */
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

/* B-(4) mod_bit_to_kc
 *    (呼び出し元: try_match_special) 
 *     単独 QK mod bit → modifier keycode 逆引き
 */
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

/* B-(5) is_composed_kc
 *    (呼び出し元: try_match_special / process_ims)
 *     VIA 合成キーコード判定
 */
static inline bool is_composed_kc(uint16_t kc) {
#ifdef IS_QK_MODS
    return IS_QK_MODS(kc);
#else
    return (kc >= QK_LCTL && kc <= QK_MODS_MAX);
#endif
}

/* B-(6) is_custom_kc
 *    (呼び出し元: try_match_special)
 *     カスタムキーコード判定 (VIA CUSTOM 範囲)
 */
static inline bool is_custom_kc(uint16_t kc) {
    return (kc >= VIA_CUSTOM_START && kc < (VIA_CUSTOM_START + 0x100));  /* 0x100=256 */
}

/* B-(7) is_layer_move_kc
 *    (呼び出し元: process_ims) 
 *     レイヤ移動キー (MO/TO/TG/OSL/LT) 判定
 */
static inline bool is_layer_move_kc(uint16_t kc) {
    return IS_QK_MOMENTARY(kc)
        || IS_QK_TO(kc)
        || IS_QK_TOGGLE_LAYER(kc)
        || IS_QK_ONE_SHOT_LAYER(kc)
        || IS_QK_LAYER_TAP(kc);
}

/* B-(8) extract_target_layer
 *    (呼び出し元: process_ims) 
 * レイヤ移動キー → 移動先レイヤ番号 (該当なし 0xFF)
 *  */
static uint8_t extract_target_layer(uint16_t kc) {
    if (IS_QK_MOMENTARY(kc))      return kc & 0xFF;
    if (IS_QK_TO(kc))             return kc & 0xFF;
    if (IS_QK_TOGGLE_LAYER(kc))   return kc & 0xFF;
    if (IS_QK_ONE_SHOT_LAYER(kc)) return kc & 0xFF;
    if (IS_QK_LAYER_TAP(kc))      return (kc >> 8) & 0xF;
    return 0xFF;
}

/* B-(9) is_lang_layer
 *    (呼び出し元: process_ims) 
 *     指定レイヤが L_LANGn のいずれかか
 */
static bool is_lang_layer(uint8_t layer) {
    for (uint8_t i = 0; i < IMS_S_KEY_COUNT; i++) {
        if (ims_s_key_layers[i] == layer) return true;
    }
    return false;
}

/* =========================================================================
 * B-(10) : ALFA_TGL fire : 状態トグル + 方向別送出
 *         (呼び出し元: try_match_special / process_ims)
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

/*  */
/* =========================================================================
 * B-(11) : match_other_specials
 *         (呼び出し元: try_match_special)
 *      IMS_ALFA_TGL 以外のモディファイア・キーマッチ処理 (状態変更のみ)
 *      戻り値: マッチしたら true
 * ========================================================================= */

static bool match_other_specials(uint16_t eff) {
    if (eff == IMS_IME_SWITCH) {
        if (ime_on) {
            set_ime_on(false);
        } else {
            set_ime_on(true);
        }
        return true;
    }

#ifdef IMS_IME_ON
    if (eff == IMS_IME_ON) {
        if (!ime_on) {
            set_ime_on(true);
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
 * B-(12) : try_match_special
 *         (呼び出し元: process_ims)
 * (1)  モディファイア・キー chord 判定
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

    /* chord_mods を蓄積 |= bit (ビット OR) または削減 &= ~bit
         mods は QK 形式の 5bit のビットマスクで、各ビットが個別の修飾キーに対応:
         bit   値    修飾キー  
         0    0x01    LCTL
         1    0x02    LSFT
         2    0x04    LALT
         3    0x08    LGUI
         4    0x10    Right flag
     */
    if (IS_MODIFIER_KEYCODE(keycode)) {
        uint8_t bit = mod_kc_to_bit(keycode);
        if (record->event.pressed) {
            chord.mods      |= bit;
            chord.peak_mods |= bit;        /* chord 中に立った mod を和集合で蓄積 */
            chord.started    = true;
        } else {
            chord.mods &= ~bit;            /* release で減算  */
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

/* */
/* =========================================================================
 * B-(13) process_kana
 *     (呼び出し元: process_ims)
 *  (2) かなモード処理
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

    /* 同時押しキー処理 */
    {
        uint8_t s_key_layer = s_key_to_layer(lang_kc);
        if (s_key_layer != 0xFF) {
            if (record->event.pressed) {
                if (pending_lang.valid) {
                    send_with_s_key(s_key_layer);
                } else {
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

    /* かなキー処理
     * KC_NO  : 何もしない (eat)。素通しすると L_BASE のキーが host に流れる。
     * KC_TRNS: 下位レイヤへ素通し (return true)。
     */
    if (lang_kc == KC_NO)   return false;
    if (lang_kc == KC_TRNS) return true;

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

/*  */
/* =========================================================================
 * エントリポイント [Bグループ]
 *  process_ims
 *     (呼び出し元: process_record_user [keymap.c])
 * ========================================================================= */

bool process_ims(uint16_t keycode, keyrecord_t *record) {
    /* バイパス中: 全キー素通し。ただし IMS_BYPASS の chord 検出のため
     * try_match_special だけは呼ぶ (戻り値は無視)。 */
    if (ims_bypass) {
        (void)try_match_special(keycode, record);
        return true;
    }

    /* (1-1) モディファイア・キー chord 判定 (3値戻り) */
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

/* =============================================================================
 * エントリポイント [Cグループ] 
 *    ims_matrix_scan  タイマ監視 
 *   (呼び出し元: matrix_scan_user [keymap.c]) 
 * ========================================================================= */

void ims_matrix_scan(void) {
    if (!ime_on || alfa_mode) return;
    if (!pending_lang.valid && pending_s_key == 0xFFFF) return;
    if (timer_elapsed(combo_timer) > IMS_COMBO_TIMEOUT) {
        flush_pending();
    }
}

/* =============================================================================
 * エントリポイント [Dグループ]
        via_custom_value_command_kb 専用
 * ============================================================================= */

/* D-(1) ims_apply_host_state
     (呼び出し元: via_custom_value_command_kb) 
 * ホスト側 IME 監視ソフトからの状態注入
 *
 * SET STATE 到着時:
 *   - 新状態が現状と一致 → 何もしない (idempotent 再通知で入力を壊さない)
 *   - 新状態が現状と相違 → バッファに何が残っていても全クリアし、
 *                           入力待ち状態にして新状態を適用する
 *
 * 全クリア対象: chord (mods/peak/base/started) / passthrough_key / pending_lang /
 *               pending_s_key / combo_timer / layer_forced_alfa
 */
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
    pending_s_key             = 0xFFFF;
    combo_timer             = 0;
    layer_forced_alfa.valid = false;

    /* 新状態を適用。set_ime_on(false) は alfa_mode=false に固定するので
     * alfa 代入は set_ime_on の後に行う。 */
    set_ime_on(host_ime_on);
    if (host_ime_on) {
        alfa_mode = host_alfa;
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
 *
 * VIA フレームワークが応答 raw_hid_send を自動で行うので、本関数内で
 * raw_hid_send を呼ばないこと (VIA ドキュメント明記)。
 *
 * 前提:
 *   - VIA_ENABLE = yes (本ファーム全体が VIA 前提のため既に満たしている)
 *   - VIA を無効化するユーザーは監視ソフト連携機能を失うが、blind IMS は継続動作
 * ========================================================================= */
#ifdef VIA_ENABLE

/* D-a (呼び出し元: via_custom_value_command_kb) */
/* D-b (呼び出し元: via_custom_value_command_kb) */
/* D-c (呼び出し元: via_custom_value_command_kb) */
/* VIA コマンド ID (via_command_id.h に依存しないよう直定義)
 */
#define IMS_VIA_ID_SET_VALUE   0x07
#define IMS_VIA_ID_GET_VALUE   0x08
#define IMS_VIA_ID_UNHANDLED   0xFF

/* D-(2) via_custom_value_command_kb (呼び出し元: VIA framework [外部]) */
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
        default:
            *cmd_id = IMS_VIA_ID_UNHANDLED;
            return;
        }
    } else if (*cmd_id == IMS_VIA_ID_GET_VALUE) {
        switch (*value_id) {
        case IMS_VAL_HELLO:
            /* "IMS1" 4 バイト + プロトコルバージョン 1 バイト */
            memcpy(&value_data[0], IMS_HID_MAGIC, 4);
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

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
 *  ims_jp_conv.c   英数モード時の記号変換層
 *
 *  動作:
 *    process_ims (英数モード経路) から呼ばれる。VIA から流れてくる keycode
 *    (US-AX 配列ベース) を、OS が認識しているレイアウトに合わせて変換する。
 *
 *  分岐 (config.h の IMS_LAYOUT_* × OS_LAYOUT_* で決定):
 *
 *    IMS_LAYOUT_US_AX × OS_LAYOUT_JP106  : テーブル A 稼働
 *    IMS_LAYOUT_US_AX × OS_LAYOUT_US_AX  : 素通し (旧来の構成)
 *    IMS_LAYOUT_JP106 × OS_LAYOUT_JP106  : 素通し
 *    IMS_LAYOUT_JP106 × OS_LAYOUT_US_AX  : 未実装 (#error)
 *
 *  入力経路の正規化 (テーブル A 稼働時):
 *    Shift+2 で @ を打つケースは 2 通りで来る:
 *      (1) VIA 合成 S(KC_2) : keycode 上位の QK_LSFT bit が立つ
 *      (2) 物理 LSFT + KC_2 : get_mods() の LSFT bit が立つ
 *    両者の OR を「実効 shift」として扱い、素の keycode と組で表を引く。
 *
 *  Shift 以外の Mod の扱い:
 *    Ctrl+2 等のアプリショートカットを壊さないため、shift 以外の mod が
 *    乗っているときは変換せずそのまま流す。
 * ============================================================================= */

#include "ims_lang.h"
#include "US_JP_extra.h"

/* =============================================================================
 *  設定検証
 * ============================================================================= */

#if !defined(IMS_LAYOUT_US_AX) && !defined(IMS_LAYOUT_JP106)
#error "Define IMS_LAYOUT_US_AX or IMS_LAYOUT_JP106 in config.h"
#endif

#if !defined(OS_LAYOUT_US_AX) && !defined(OS_LAYOUT_JP106)
#error "Define OS_LAYOUT_US_AX or OS_LAYOUT_JP106 in config.h"
#endif

#if defined(IMS_LAYOUT_JP106) && defined(OS_LAYOUT_US_AX)
#error "Combination IMS_LAYOUT_JP106 + OS_LAYOUT_US_AX is not yet supported"
#endif

/* =============================================================================
 *  テーブル A : IMS_LAYOUT_US_AX × OS_LAYOUT_JP106
 *      US 物理配列の打鍵を JP106 OS 用 HID に変換
 * ============================================================================= */

#if defined(IMS_LAYOUT_US_AX) && defined(OS_LAYOUT_JP106)

typedef struct {
    uint8_t  us_keycode;   /* 素の keycode (KC_2 等)       */
    bool     us_shifted;   /* US 配列で Shift 同時押しか   */
    uint16_t jp_keycode;   /* JP106 OS に送る HID (S 込み) */
} us_jp_map_t;

static const us_jp_map_t us_jp_map[] = {
    /* --- Shift なし --- */
    { KC_GRV,   false, S(KC_LBRC) },  /* `   →  KC_LBRC + Shift          */
    { KC_LBRC,  false, KC_RBRC    },  /* [   →  KC_RBRC                  */
    { KC_RBRC,  false, KC_BSLS    },  /* ]   →  KC_BSLS                  */
    { KC_BSLS,  false, KC_INT3    },  /* \   →  KC_INT3 (¥ キー)         */
    { KC_QUOT,  false, S(KC_7)    },  /* '   →  KC_7 + Shift             */
    { KC_EQL,   false, S(KC_MINS) },  /* =   →  KC_MINS + Shift          */

    /* --- Shift あり --- */
    { KC_GRV,   true,  S(KC_EQL)  },  /* ~   →  KC_EQL + Shift           */
    { KC_2,     true,  KC_LBRC    },  /* @   →  KC_LBRC                  */
    { KC_6,     true,  KC_EQL     },  /* ^   →  KC_EQL                   */
    { KC_7,     true,  S(KC_6)    },  /* &   →  KC_6 + Shift             */
    { KC_8,     true,  S(KC_QUOT) },  /* *   →  KC_QUOT + Shift          */
    { KC_9,     true,  S(KC_8)    },  /* (   →  KC_8 + Shift             */
    { KC_0,     true,  S(KC_9)    },  /* )   →  KC_9 + Shift             */
    { KC_MINS,  true,  S(KC_INT1) },  /* _   →  KC_INT1 + Shift          */
    { KC_EQL,   true,  S(KC_SCLN) },  /* +   →  KC_SCLN + Shift          */
    { KC_LBRC,  true,  S(KC_RBRC) },  /* {   →  KC_RBRC + Shift          */
    { KC_RBRC,  true,  S(KC_BSLS) },  /* }   →  KC_BSLS + Shift          */
    { KC_BSLS,  true,  S(KC_INT3) },  /* |   →  KC_INT3 + Shift          */
    { KC_SCLN,  true,  KC_QUOT    },  /* :   →  KC_QUOT                  */
    { KC_QUOT,  true,  S(KC_2)    },  /* "   →  KC_2 + Shift             */
};

bool process_ims_symbol_conv(uint16_t keycode, keyrecord_t *record) {
    /* press のみ処理。release は素通し。 */
    if (!record->event.pressed) return true;

    /* 実効 shift = (VIA 合成 shift) OR (物理 shift) */
    uint8_t mods         = get_mods();
    bool    phys_shift   = (mods & (MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT))) != 0;
    bool    synth_shift  = (keycode & QK_LSFT) != 0;
    bool    eff_shift    = phys_shift || synth_shift;

    /* shift 以外の mod が乗っていたら変換しない (Ctrl+2 等のショートカット保護) */
    uint8_t other_mods = mods & ~(MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT));
    if (other_mods) return true;

    /* 素の keycode (下位 8bit) を取り出してテーブル検索 */
    uint8_t base_kc = keycode & 0xFF;

    for (size_t i = 0; i < sizeof(us_jp_map) / sizeof(us_jp_map[0]); i++) {
        if (us_jp_map[i].us_keycode == base_kc &&
            us_jp_map[i].us_shifted == eff_shift) {
            /* 物理 shift を一時退避して送出、復元 */
            del_mods(MOD_BIT(KC_LSFT) | MOD_BIT(KC_RSFT));
            tap_code16(us_jp_map[i].jp_keycode);
            set_mods(mods);
            return false;   /* 元イベント消滅 */
        }
    }
    return true;            /* 変換対象外: QMK に流す */
}

#else  /* 他の有効な象限 (US_AX×US_AX, JP106×JP106): 素通し */

bool process_ims_symbol_conv(uint16_t keycode, keyrecord_t *record) {
    (void)keycode; (void)record;
    return true;
}

#endif

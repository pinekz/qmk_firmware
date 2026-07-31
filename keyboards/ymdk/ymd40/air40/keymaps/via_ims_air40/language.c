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

/*  This file is for each language specific rules.
 *  All custom keycodes make composite pairs for ims.c
 *  (A normal character needs 0 as the second element)
 *
 *  Below example codes are for Japanese KANA input.
 */

#include "ims_lang.h"
#include "US_JP_extra.h"

/* JP layout common keys (レイアウト共通キー)
 * IMEがJP106かな配列を前提とするため、全レイアウトで同じ HIDコードを送る必要があります */

/* 読点・句点 */
#define JA_TEN   S(KC_COMM)     /* 、読点 */
#define JA_MARU  S(KC_DOT)      /* 。句点 */

/* 濁点・半濁点・鉤括弧 */
#define JA_DAKUTEN  KC_LBRC     /* ゛濁点   JP106: @位置 */
#define JA_HANDAKU  KC_RBRC     /* ゜半濁点  JP106: [位置 */
#define JA_KAGL     S(KC_RBRC)  /* 「       JP106: [シフト位置 */

/* かな文字エイリアス（JISかな配列） */
#define JA_A     KC_3      /* あ */
#define JA_I     KC_E      /* い */
#define JA_U     KC_4      /* う */
#define JA_E     KC_5      /* え */
#define JA_O     KC_6      /* お */
#define JA_KA    KC_T      /* か */
#define JA_KI    KC_G      /* き */
#define JA_KU    KC_H      /* く */
#define JA_KE    KC_QUOT   /* け */
#define JA_KO    KC_B      /* こ */
#define JA_SA    KC_X      /* さ */
#define JA_SI    KC_D      /* し */
#define JA_SU    KC_R      /* す */
#define JA_SE    KC_P      /* せ */
#define JA_SO    KC_C      /* そ */
#define JA_TA    KC_Q      /* た */
#define JA_TI    KC_A      /* ち */
#define JA_TU    KC_Z      /* つ */
#define JA_TE    KC_W      /* て */
#define JA_TO    KC_S      /* と */
#define JA_NA    KC_U      /* な */
#define JA_NI    KC_I      /* に */
#define JA_NU    KC_1      /* ぬ */
#define JA_NE    KC_COMM   /* ね */
#define JA_NO    KC_K      /* の */
#define JA_HA    KC_F      /* は */
#define JA_HI    KC_V      /* ひ */
#define JA_HU    KC_2      /* ふ */
#define JA_HE    KC_EQL    /* へ */
#define JA_HO    KC_MINS   /* ほ */
#define JA_MA    KC_J      /* ま */
#define JA_MI    KC_N      /* み */
#define JA_ME    KC_SLASH  /* め */
#define JA_MO    KC_M      /* も */
#define JA_YA    KC_7      /* や */
#define JA_YU    KC_8      /* ゆ */
#define JA_YO    KC_9      /* よ */
#define JA_RA    KC_O      /* ら */
#define JA_RI    KC_L      /* り */
#define JA_RU    KC_DOT    /* る */
#define JA_RE    KC_SCLN   /* れ */
#define JA_WA    KC_0      /* わ */
#define JA_WO    S(KC_0)   /* を */
#define JA_NN    KC_Y      /* ん */

/* 小書き */
#define JA_XA    S(KC_3)   /* ぁ */
#define JA_XI    S(KC_E)   /* ぃ */
#define JA_XU    S(KC_4)   /* ぅ */
#define JA_XE    S(KC_5)   /* ぇ */
#define JA_XO    S(KC_6)   /* ぉ */
#define JA_XYA   S(KC_7)   /* ゃ */
#define JA_XYU   S(KC_8)   /* ゅ */
#define JA_XYO   S(KC_9)   /* ょ */
#define JA_XTU   S(KC_Z)   /* っ */
#define JA_XKA   KC_L      /* ヵ */
#define JA_XKE   KC_QUOT   /* ヶ */
#define JA_XWA   KC_0      /* ゎ */

/* 記号 */
#define JA_NAKA  S(KC_SLASH)   /* ・ 中黒 */


/* かなキーコードから、実際に発行するキーコードへの変換テーブル */
/* インデックスは VIA の CUSTOM(n) の n に対応する 0始まりの連番 */
const uint16_t kana_composite_table[][IMS_MAX_COMBO_LEN] = {
/* If your language needs 3key composite, you can write [n] = {KEY1, KEY2, KEY3}, */
    [0]  = {JA_A,   0},       /* あ  J_A   */
    [1]  = {JA_I,   0},       /* い  J_I   */
    [2]  = {JA_U,   0},       /* う  J_U   */
    [3]  = {JA_E,   0},       /* え  J_E   */
    [4]  = {JA_O,   0},       /* お  J_O   */
    [5]  = {JA_KA,  0},       /* か  J_KA  */
    [6]  = {JA_KI,  0},       /* き  J_KI  */
    [7]  = {JA_KU,  0},       /* く  J_KU  */
    [8]  = {JA_KE,  0},       /* け  J_KE  */
    [9]  = {JA_KO,  0},       /* こ  J_KO  */
    [10] = {JA_SA,  0},       /* さ  J_SA  */
    [11] = {JA_SI,  0},       /* し  J_SI  */
    [12] = {JA_SU,  0},       /* す  J_SU  */
    [13] = {JA_SE,  0},       /* せ  J_SE  */
    [14] = {JA_SO,  0},       /* そ  J_SO  */
    [15] = {JA_TA,  0},       /* た  J_TA  */
    [16] = {JA_TI,  0},       /* ち  J_TI  */
    [17] = {JA_TU,  0},       /* つ  J_TU  */
    [18] = {JA_TE,  0},       /* て  J_TE  */
    [19] = {JA_TO,  0},       /* と  J_TO  */
    [20] = {JA_NA,  0},       /* な  J_NA  */
    [21] = {JA_NI,  0},       /* に  J_NI  */
    [22] = {JA_NU,  0},       /* ぬ  J_NU  */
    [23] = {JA_NE,  0},       /* ね  J_NE  */
    [24] = {JA_NO,  0},       /* の  J_NO  */
    [25] = {JA_HA,  0},       /* は  J_HA  */
    [26] = {JA_HI,  0},       /* ひ  J_HI  */
    [27] = {JA_HU,  0},       /* ふ  J_HU  */
    [28] = {JA_HE,  0},       /* へ  J_HE  */
    [29] = {JA_HO,  0},       /* ほ  J_HO  */
    [30] = {JA_MA,  0},       /* ま  J_MA  */
    [31] = {JA_MI,  0},       /* み  J_MI  */
    [32] = {JA_MU,  0},       /* む  J_MU  */
    [33] = {JA_ME,  0},       /* め  J_ME  */
    [34] = {JA_MO,  0},       /* も  J_MO  */
    [35] = {JA_YA,  0},       /* や  J_YA  */
    [36] = {JA_YU,  0},       /* ゆ  J_YU  */
    [37] = {JA_YO,  0},       /* よ  J_YO  */
    [38] = {JA_RA,  0},       /* ら  J_RA  */
    [39] = {JA_RI,  0},       /* り  J_RI  */
    [40] = {JA_RU,  0},       /* る  J_RU  */
    [41] = {JA_RE,  0},       /* れ  J_RE  */
    [42] = {JA_RO,  0},       /* ろ  J_RO  */
    [43] = {JA_WA,  0},       /* わ  J_WA  */
    [44] = {JA_WO,  0},       /* を  J_WO  */
    [45] = {JA_NN,  0},       /* ん  J_NN  */
    [46] = {JA_KA,  JA_DAKUTEN},  /* が  J_GA  */
    [47] = {JA_KI,  JA_DAKUTEN},  /* ぎ  J_GI  */
    [48] = {JA_KU,  JA_DAKUTEN},  /* ぐ  J_GU  */
    [49] = {JA_KE,  JA_DAKUTEN},  /* げ  J_GE  */
    [50] = {JA_KO,  JA_DAKUTEN},  /* ご  J_GO  */
    [51] = {JA_SA,  JA_DAKUTEN},  /* ざ  J_ZA  */
    [52] = {JA_SI,  JA_DAKUTEN},  /* じ  J_ZI  */
    [53] = {JA_SU,  JA_DAKUTEN},  /* ず  J_ZU  */
    [54] = {JA_SE,  JA_DAKUTEN},  /* ぜ  J_ZE  */
    [55] = {JA_SO,  JA_DAKUTEN},  /* ぞ  J_ZO  */
    [56] = {JA_TA,  JA_DAKUTEN},  /* だ  J_DA  */
    [57] = {JA_TI,  JA_DAKUTEN},  /* ぢ  J_DI  */
    [58] = {JA_TU,  JA_DAKUTEN},  /* づ  J_DU  */
    [59] = {JA_TE,  JA_DAKUTEN},  /* で  J_DE  */
    [60] = {JA_TO,  JA_DAKUTEN},  /* ど  J_DO  */
    [61] = {JA_HA,  JA_DAKUTEN},  /* ば  J_BA  */
    [62] = {JA_HI,  JA_DAKUTEN},  /* び  J_BI  */
    [63] = {JA_HU,  JA_DAKUTEN},  /* ぶ  J_BU  */
    [64] = {JA_HE,  JA_DAKUTEN},  /* べ  J_BE  */
    [65] = {JA_HO,  JA_DAKUTEN},  /* ぼ  J_BO  */
    [66] = {JA_HA,  JA_HANDAKU}, /* ぱ  J_PA  */
    [67] = {JA_HI,  JA_HANDAKU}, /* ぴ  J_PI  */
    [68] = {JA_HU,  JA_HANDAKU}, /* ぷ  J_PU  */
    [69] = {JA_HE,  JA_HANDAKU}, /* ぺ  J_PE  */
    [70] = {JA_HO,  JA_HANDAKU}, /* ぽ  J_PO  */
    [71] = {JA_XA,  0},       /* ぁ  J_XA  */
    [72] = {JA_XI,  0},       /* ぃ  J_XI  */
    [73] = {JA_XU,  0},       /* ぅ  J_XU  */
    [74] = {JA_XE,  0},       /* ぇ  J_XE  */
    [75] = {JA_XO,  0},       /* ぉ  J_XO  */
    [76] = {JA_XYA, 0},       /* ゃ  J_XYA */
    [77] = {JA_XYU, 0},       /* ゅ  J_XYU */
    [78] = {JA_XYO, 0},       /* ょ  J_XYO */
    [79] = {JA_XTU, 0},       /* っ  J_XTU */
    [80] = {JA_CHO, 0},       /* ー  J_CHO */
    [81] = {JA_TEN, 0},       /* 、  J_TEN */
    [82] = {JA_MARU,0},       /* 。  J_MARU*/
    [83] = {JA_NAKA,0},       /* ・  J_NAKA*/
    [84] = {JA_KAGL, 0},       /* 「  J_KAGL*/
    [85] = {JA_KAGR, 0},       /* 」  J_KAGR*/
    [86] = {JA_XKA, 0},       /* ヵ  J_XKA */
    [87] = {JA_XKE, 0},       /* ヶ  J_XKE */
    [88] = {JA_XWA, 0},       /* ゎ  J_XWA */
    [89]  = {JA_U,   JA_DAKUTEN}, /* ゔ  J_VU  */
};

void ims_handle_lang(uint16_t index) {

    if (index < (sizeof(kana_composite_table)/sizeof(kana_composite_table[0]))) {
        for (int i = 0; i < IMS_MAX_COMBO_LEN; i++) {
            uint16_t code = kana_composite_table[index][i];
            if (code != 0) {
                tap_code16(code);
            }
        }
    }
}

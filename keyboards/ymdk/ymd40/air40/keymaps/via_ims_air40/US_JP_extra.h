#ifndef US_JP_EXTRA_H
#define US_JP_EXTRA_H

#include "quantum.h"

/*
 * US_JP_extra.h
 * language.c のかな出力で使う、レイアウト依存 4 シンボルの定義。
 *
 * 分岐軸: OS_LAYOUT_* (OS 認識レイアウト)
 *   tap_code16 で送出する HID は OS がどう解釈するかだけで決まるため、
 *   物理キーボード (IMS_LAYOUT_*) 側の分岐は不要。
 *
 * config.h で OS_LAYOUT_JP106 または OS_LAYOUT_US_AX のいずれかを定義する。
 */

#if defined(OS_LAYOUT_JP106)
/* JP106 OS かなモードでのキー位置と HID の対応:
 *   @ キー  = KC_LBRC  → 濁点 ゛   (language.c JA_DAKUTEN)
 *   [ キー  = KC_RBRC  → 半濁点 ゜ (language.c JA_HANDAKU) / Shift で「(JA_KAGL)
 *   ] キー  = KC_BSLS  → む       / Shift で 」
 *   \_ キー = KC_INT1  → ろ       (Shift 不感応)
 *   ¥ キー  = KC_INT3  → ー
 *
 * 注) む を KC_RBRC にすると [ キー (半濁点) と衝突する。
 *     」 を S(KC_INT1) にすると \_ キー (ろ) が出る (Shift 不感応のため)。
 */
#define JA_MU    KC_BSLS        /* む  JP106: ]位置 */
#define JA_RO    KC_INT1        /* ろ  JP106: \_位置 (右Shiftの左) */
#define JA_CHO   KC_INT3        /* ー  JP106: ¥位置 */
#define JA_KAGR  S(KC_BSLS)     /* 」  JP106: Shift+]位置 */

#elif defined(OS_LAYOUT_US_AX)
#define JA_MU    KC_GRV         /* む  AX: `位置 */
#define JA_RO    KC_NUBS        /* ろ  AX: 0x56 */
#define JA_CHO   KC_BSLS        /* ー  AX: \位置 */
#define JA_KAGR  S(KC_GRV)      /* 」  AX: ~キー */

#else
#error "Define OS_LAYOUT_JP106 or OS_LAYOUT_US_AX in config.h"
#endif

#endif

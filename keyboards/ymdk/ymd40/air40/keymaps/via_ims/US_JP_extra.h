#ifndef US_JP_EXTRA_H
#define US_JP_EXTRA_H

#include "quantum.h"

/*
 * US_JP_extra.h
 * JA_ エイリアス定義ファイル
 * language.c のコンポジットテーブルで使用する視認性エイリアス。
 *
 * config.h で以下のいずれかを定義する：
 *   #define IMS_LAYOUT_JP106   JP106キーボード使用時
 *   #define IMS_LAYOUT_US_AX   US/AXキーボード使用時
 *   未定義の場合はUSレイアウトをデフォルトとする
 */

/* レイアウトで異なる特殊キー
 * IMEがJP106かな配列を前提とするため、AX/USでも同じHIDコードを送る必要があります */
 
#if defined(IMS_LAYOUT_JP106)
#define JA_MU    KC_RBRC        /* む  JP106: ]位置 */
#define JA_RO    KC_INT1        /* ろ  JP106: \位置(右Shift左) */
#define JA_CHO   KC_INT3        /* ー  JP106: ¥位置 */
#define JA_KAGR  S(KC_INT1)     /* 」  JP106: Shift+\位置 */

#elif defined(IMS_LAYOUT_US_AX)
#define JA_MU    KC_GRV         /* む  AX: `位置 */
#define JA_RO    KC_NUBS        /* ろ  AX: 0x56 */
#define JA_CHO   KC_BSLS        /* ー  AX: \位置 */
#define JA_KAGR  S(KC_GRV)      /* 」  AX: ~キー */

#else  /* US */
#define JA_MU    KC_BSLS        /* む  US: \位置 */
#define JA_RO    KC_RO          /* ろ  US: スキャンコード0x73 */
#define JA_CHO   S(KC_MINS)     /* ー  US: Shift+- */
#define JA_KAGR  S(KC_RBRC)     /* 」  US: Shift+] */
#endif



#endif

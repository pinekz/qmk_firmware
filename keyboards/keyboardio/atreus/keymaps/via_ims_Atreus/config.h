#pragma once

/* =========================================================================
 * キーボードレイアウト設定 (2 軸)
 *
 * IMS_LAYOUT_* : 物理キーボードの種別
 * OS_LAYOUT_*  : OS が認識しているキーボードレイアウト
 *
 * 注）
 *   IMS_LAYOUT_JP106 かつ OS_LAYOUT_US_AX  : 未実装 (選択不可)
 * ========================================================================= */

/* --- 物理キーボードの種別 (どちらか一方を有効化) --- */
// #define IMS_LAYOUT_JP106    /* JP106キーボード使用時 */
#define IMS_LAYOUT_US_AX       /* US/AXキーボード使用時 */

/* --- OS が認識しているレイアウト (どちらか一方を有効化) --- */
// #define OS_LAYOUT_US_AX     /* OS が AX レイアウト認識のとき   */
#define OS_LAYOUT_JP106        /* OS が JP106 レイアウト認識のとき */

#define DYNAMIC_KEYMAP_LAYER_COUNT 6

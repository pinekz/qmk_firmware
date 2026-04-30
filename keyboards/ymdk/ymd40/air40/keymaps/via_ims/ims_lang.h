#ifndef IMS_LANG_H
#define IMS_LANG_H

#include "quantum.h"

/* =========================================================================
 * IMS start switch : 起動スイッチ
 *
 * IMEのキー設定に合わせて設定します。3つの型をサポート:
 *
 *   トグル型 (1キーでON/OFF):
 *     IMS_IME_SWITCH のみ定義する
 *     例: Windows JP106 (全角/半角 = KC_LANG5)
 *         Windows AX    (漢字      = KC_RALT)
 *
 *   個別型 (オン/オフが別キー):
 *     IMS_IME_ON / IMS_IME_OFF を定義、IMS_IME_SWITCH はコメントアウト
 *     例: Mac (かな = KC_LNG1 / 英数 = KC_LNG2)
 *
 *   混合型 (IMEで設定に同期):
 *     3つとも定義可（ここで定義せずに、IMEモニタも使わないとモードずれを起こします）
 * ========================================================================= */

#define IMS_IME_SWITCH  KC_RALT
// #define IMS_IME_ON   KC_LNG1
// #define IMS_IME_OFF  KC_LNG2

/* =========================================================================
 * Simultaneous Press keys (同時押しキーの指定)
 *
 * MODキーを増やす場合は以下の箇所を同時に変更する:
 *   1. IMS_CODE_MODn を追加定義
 *   2. IMS_MOD_KEYS / IMS_MOD_LAYERS に対応定数エントリを追加
 *   3. enum layer_names に L_LANGn を追加
 * ========================================================================= */

#define IMS_CODE_MOD1   KC_INT4  /* MODキーに変換キーを指定した   */
#define IMS_CODE_MOD2   KC_INT5  /* MODキーに無変換キーを指定した */

#define IMS_MOD_KEYS    { IMS_CODE_MOD1, IMS_CODE_MOD2 }   /* MODキーの配列   */
#define IMS_MOD_LAYERS  { L_LANG1, L_LANG2 }               /* MODキーの配列ﾆ対応するレイヤ   */

#define IMS_COMBO_TIMEOUT 80      /* Simultaneous Press 同時押しタイマー(mSec) */
#define IMS_MAX_COMBO_LEN 2       /* language.cでの composite文字数　例: は + ゜→ ぱ */

/* =========================================================================
 * Layer definition (moved from keymap.c)
 * L_BASE は常時ON。IMEオン時は L_LANG* を参照するが実レイヤ遷移はしない
 * ========================================================================= */

enum layer_names {
    L_BASE = 0,
    L_BASE1,
    L_BASE2,
    L_LANG,
    L_LANG1,
    L_LANG2
};

/* =========================================================================
 * 英数・かなトグル (解釈A: 方向別送出方式)
 *
 * IMS_ALFA_TGL     : ユーザーが押すトリガキー
 *                     カスタムキーコード(VIA 合成; Anykey) またはトグルキーの場合、
 *                     実際の送信をせず、下記のオンオフキーに変換してPCに送出する。
 *                     物理キーの同時押し (Ctrl + U 等) の場合では、同時押しの状態決定が遅れる為、
 *                     物理キーの監視をして、IMS は内部の状態変更のみ行う (二重送出を防ぐ)。
 *
 * IMS_ALFA_TGL_ON  : かな → 英数遷移時 (alfa_mode false→true) に IMSがPCに送るキー
 * IMS_ALFA_TGL_OFF : 英数 → かな遷移時 (alfa_mode true→false) に IMSがPCに送るキー
 * 
 * --- 設定例 ---
 *
 * １キーで双方向トグル（Japanist10):
 *   #define IMS_ALFA_TGL       LCTL(KC_Q)   : IME設定の英数かなトグルキー 
 *   #define IMS_ALFA_TGL_ON    LCTL(KC_Q)
 *   #define IMS_ALFA_TGL_OFF   LCTL(KC_Q)
 *
 * オン・オフ別キー (MS-IME・MAC):
 *   #define IMS_ALFA_TGL       LCTL(KC_U)    <- ユーザーはこのキーを押す
 *   #define IMS_ALFA_TGL_ON    LCTL(KC_U)    <- IME設定の英数変換キー 
 *   #define IMS_ALFA_TGL_OFF   LCTL(KC_SPC)  <- IME設定のひらがなキー 
 *
 * トリガにカスタムキーコードを使う場合:
 *   #define IMS_ALFA_TGL       J_ALFA_TGL    : It needs to be add in enum custom_keycodes
 *   #define IMS_ALFA_TGL_ON    LCTL(KC_U)
 *   #define IMS_ALFA_TGL_OFF   LCTL(KC_SPC)
 *   (VIA でこのカスタムキーコードを物理キー位置に配置する)
 * ========================================================================= */

#define IMS_ALFA_TGL        LCTL(KC_0)
#define IMS_ALFA_TGL_ON     LCTL(KC_0)
#define IMS_ALFA_TGL_OFF    LCTL(KC_SPC)

/* =========================================================================
 * IMS Reset : IMS内部状態リセット
 *
 * IMS内部状態 (ime_on / alfa_mode / 保留バッファ) をクリア。
 * 押したキー自体はホストにも流れる (Ctrl+Alt+Q or Q 単独)。
 * ========================================================================= */

#define IMS_RESET       LCTL(LALT(KC_Q))

/* =========================================================================
 * Custom keycodes (VIA Protocol 12 : QK_KB_0 (0x7E00) instead of SAFE_RANGE)
 *
 * IMS_ALFA_TGL 用のカスタムキーコードを追加する場合、
 * J_KANA_END の後に追加する:
 *   enum custom_keycodes {
 *       J_A = QK_KB_0,
 *       ...
 *       J_KANA_END,
 *       J_ALFA_TGL,   <- 追加
 *   };
 * ========================================================================= */

enum custom_keycodes {
    J_A = QK_KB_0,						// VIA_CUSTOM_START : QK_KB_0 = 0x7E00
    J_I, J_U, J_E, J_O,
    J_KA, J_KI, J_KU, J_KE, J_KO,
    J_SA, J_SI, J_SU, J_SE, J_SO,
    J_TA, J_TI, J_TU, J_TE, J_TO,
    J_NA, J_NI, J_NU, J_NE, J_NO,
    J_HA, J_HI, J_HU, J_HE, J_HO,
    J_MA, J_MI, J_MU, J_ME, J_MO,
    J_YA, J_YU, J_YO,
    J_RA, J_RI, J_RU, J_RE, J_RO,
    J_WA, J_WO, J_NN,
    J_GA, J_GI, J_GU, J_GE, J_GO,
    J_ZA, J_ZI, J_ZU, J_ZE, J_ZO,
    J_DA, J_DI, J_DU, J_DE, J_DO,
    J_BA, J_BI, J_BU, J_BE, J_BO,
    J_PA, J_PI, J_PU, J_PE, J_PO,
    J_XA, J_XI, J_XU, J_XE, J_XO,
    J_XYA, J_XYU, J_XYO,
    J_XTU,
    J_CHO, J_TEN, J_MARU, J_NAKA, J_KAGL, J_KAGR,
    J_XKA, J_XKE, J_XWA,
    J_KANA_END
};

/* =========================================================================
 * Host IME monitor connection (VIA Custom Value protocol)
 *
 * ホスト常駐の IME 監視ソフトとの疎通は、VIA の Custom Value メカニズム
 * (id_custom_set_value = 0x07 / id_custom_get_value = 0x08) に相乗りする。
 * 独立 Raw HID コマンド ID を作ると VIA プロトコル (0x01-0x15) と衝突する
 * ため、channel_id=IMS_VIA_CHANNEL_ID 配下の value_id で多重化する。
 *
 *   GET  channel=IMS_VIA_CHANNEL_ID  value=IMS_VAL_HELLO  ->  "IMS1"+verion で応答
 *   SET  channel=IMS_VIA_CHANNEL_ID  value=IMS_VAL_STATE  +  [ime_on][alfa]
 *
 * 監視ソフト非接続/非対応時は via_custom_value_command_kb が呼ばれない、
 * または他 channel として id_unhandled を返すだけなので、blind での IMS動作は
 * 常に保証される。
 *
 * VIA 無効時 (VIA_ENABLE=no) は ims.c 末尾の HID ブロックがコンパイルから
 * 外れるため、こちらも blind IMS のみで完全動作する。
 
 * VIA Custom Value の channel ID
 * VIA が予約する 0x00〜0x05 (backlight/rgblight/rgb_matrix/led_matrix/audio) を避け、
 * IMSの 'I' (0x49) をマーカーとして使用 (raw HIDでは一文字づつ順に送出する為) 
* ========================================================================= */

#define IMS_VIA_CHANNEL_ID      0x49

/* IMS channel 内の value_id */
#define IMS_VAL_STATE           0x01   /* SET: [3]=ime_on [4]=alfa          */
#define IMS_VAL_HELLO           0x02   /* GET: 応答=[3..6]="IMS1" [7]=ver   */

#define IMS_HID_MAGIC          "IMS1"  /* HELLO 応答の識別子 (4 bytes, NUL 終端は送らない) */
#define IMS_HID_PROTO_VER      1       /* プロトコル仕様変更時に増やす */

/* ims.c 内の HID コールバック (via_custom_value_command_kb) から呼ぶ
 * IME状態の注入 API。ヘッダでは宣言のみ。 */
void ims_apply_host_state(bool host_ime_on, bool host_alfa);

#endif

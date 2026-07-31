# IMS (国際化マルチシフト)キーボード
 ver. 2.0 :2026/07/26 QMK 0.33.0
(ver. 1.0 :2026/06/26 QMK 0.27.0)

Procedure from constant settings in the source files, through firmware compilation and installation, to key configuration in Via.

## 1. Fork/download of target files; setting up the compile/install environment

This is written as the procedure for adding the IMS module on top of an existing keyboard's firmware source code.
For details on related resources, the easiest and most reliable approach is to ask an AI like Google Gemini or Copilot in your browser to explain.

**(1) Fork the qmk_firmware directory from GitHub and download the entire source code locally.**

(Because of version changes, the entire source-code base needs to be consistent in version.)

**(2) Set up the required software environment**

- **MSYS**            (for compiling QMK firmware; also as a console during debugging)
- **QMK Tool**        (for installing QMK firmware onto the keyboard)
- **Rust development environment** (for compiling/debugging the Windows version of IME_Monitor)
    - On Windows 11 25H2, the executable `IME_monitor/target/release/IME_monitor_for_ims.exe` is already compiled, so you can use it just by registering it in Startup.
- **GitHub Desktop**  (for committing the completed firmware and pushing it to your forked GitHub repository)
- **Source code editor** (VS Code or similar; there are lighter options, but you'll have trouble without Japanese input support)

**(3) In the local `qmk_firmware` source tree, find your target keyboard under the `keyboards` directory, create your own directory inside the lowest-level `keymap` directory, and work there.**

## 2. Source code copy and target files

Copy/download the files from the `via_ims` directory in my repository.

<https://github.com/pinekz/qmk_firmware/tree/IMS-module/keyboards/ymdk/ymd40/air40/keymaps/via_ims>

**(1) Files to use as-is**

- `ims_jp_conv.c`
- `ims.c`
- `language.c`
- `US_JP_extra.h`

**(2) Files to modify settings in**

- `config.h`
- `rules.mk`
- `keymap.c`
- `ims_lang.h`

(`keyboard.json` lives in the parent maker directory and specifies which RGB animations are used. The define/undef approach in `config.h` or `rules.mk` did not work on recent QMK, so I compile by replacing unwanted animations with `false`.)

**(3) Keyboard-specific files to load into Via's Design tab (including the custom-key menu)**

- `*layout*.json` (note: `*keymap.json` is your personal save of the key arrangement you set in Via.)

**(4) Files to commit to git as a personal record**

- `readme.md`

## 3. Configuration

### 3-1. config.h

**(1) Specify the physical keyboard you use and the OS language environment.** (Comment out the one you are not using with `//`.)

(I use a US-layout keyboard with a JP106 OS environment, so my configuration example looks like this.)

```c
// #define IMS_LAYOUT_JP106    /* When using a JP106 keyboard */
#define IMS_LAYOUT_US_AX       /* When using a US/AX keyboard */
// #define OS_LAYOUT_US_AX     /* When the OS recognizes an AX layout   */
#define OS_LAYOUT_JP106        /* When the OS recognizes a JP106 layout */
```

**(2) Specify the number of layers to use.**

(I have a small 40 % keyboard with symbols/numbers on layer 2 and Function keys / RGB controls on layer 3. Plus the 3 base layers used by IMS, that gives 6 in total.
However, if you increase the number of chord keys through the settings, the layer count will also go up to 7 or more.
That can put pressure on the firmware-size budget, so countermeasures like reducing RGB animations may become necessary.)

```c
#define DYNAMIC_KEYMAP_LAYER_COUNT 6
```

### 3-2. rules.mk

The Via-enable directive is obvious, but I recommend also declaring LTO for size optimization.

```makefile
VIA_ENABLE=yes
LTO_ENABLE=yes
```

### 3-3. keymap.c

**(1)** Copy one from the parent maker directory or the `default` keymap to use as a base.

**(2)** At the top, include the IMS-related files.

```c
#include "ims_lang.h"
#include "ims_jp_conv.c"
#include "language.c"
#include "ims.c"
```

**(3)** Keep the layer names consistent. `enum layer_names` is normally written in this file, but in IMS it has been moved to 3-4-(3), so take care that it does not become inconsistent with `PROGMEM keymaps[]`.

(Layer names: `L_BASE` `L_BASE1` `L_BASE2` `L_LANG` `L_LANG1` `L_LANG2`)

**(4)** The keymap itself can be empty.

Since you will configure it in Via, filling it with `KC_NO` (or `_______,`) here is fine.

**(5)** At the bottom, place the QMK hooks for your custom program: `process_record_user` (key-input hook) and `matrix_scan_user` (timer operations).

(Depending on the maker, the file may include other functions as sample code; anything related to layer operations or other key operations interferes with IMS, so it is easier to just delete them.)

### 3-4. ims_lang.h

Unless something is commented out, all of these are required. (For function keys used by the IME, specify the corresponding key.)

**(1) IMS activation switch (the key choice must match your IME settings)**

As I wrote in the source file, if you use the zenkaku/hankaku (full-/half-width) key, specifying `KC_GRV` is the safe choice.
QMK is basically self-contained at the keyboard level, so its sphere of influence is limited and it is free to change conventions; but Microsoft does not follow along, and even Via still uses the old keycodes.
(Recently the macOS-style split-key designation seems to be in fashion, so I made that usable too, but I don't have the hardware to confirm whether `KC_LNG1,2` actually works.)

```c
#define IMS_IME_SWITCH  KC_GRV
// #define IMS_IME_ON   KC_LNG1
// #define IMS_IME_OFF  KC_LNG2
```

**(2) Chord keys (free key choice)**

It looks like awkward syntax, but what's happening here is not variable assignment but constant declaration, which is why it has to be written this way (according to the AI). Specifying chord keys also requires defining the corresponding layers.
Adding more chord keys is the mechanism for adding more layer surfaces and configurable keys.

```c
#define IMS_CODE_S_KEY1   KC_INT4  /* Chord key 1: the henkan (conversion) key      */
#define IMS_CODE_S_KEY2   KC_INT5  /* Chord key 2: the muhenkan (non-conversion) key */

#define IMS_S_KEYS        { IMS_CODE_S_KEY1, IMS_CODE_S_KEY2 }  /* Array of chord keys           */
#define IMS_S_KEY_LAYERS  { L_LANG1, L_LANG2 }                  /* Array of corresponding layers */
```

**(3) Layer definition**

As stated in 3-3-(3), this must match what is written in `keymap.c`.

**(4) Alphanumeric / Kana toggle (the key choice must match your IME settings)**

Japanist10 toggles with a single key, but MS-IME apparently has the quirk of being effective or ineffective depending on state. I solved this by sending — separately, in both directions — a "hiragana" key (assigned to Ctrl+Space on the MS-IME side) and a "full-width alphanumeric" key (assigned to Ctrl+0 on the MS-IME side), in addition to the toggle key.
If your IME can toggle with a single key, specify the same keycode for all three.
Also, I have decided that half-width alphanumeric/symbols are input with the IME off and only full-width alphanumeric/symbols are input with the IME on, and implemented it that way.
(The usage is: when you want to enter a full-width alphanumeric/symbol while in the middle of kana input, you toggle with this key.)

```c
#define IMS_ALFA_TGL        LCTL(KC_13)
#define IMS_ALFA_TGL_ON     LCTL(KC_13)
#define IMS_ALFA_TGL_OFF    LSFT(KC_13)
```

> **Note:** Electron is a framework for building desktop apps using web technologies (HTML/CSS/JavaScript). Well-known examples include VS Code, Slack, Discord, Microsoft Teams, GitHub Desktop, LM Studio, and so on. Because the core is a browser, the JavaScript layer calls `event.preventDefault()` on browser shortcuts such as Ctrl+0 (zoom reset), Ctrl+W (close tab), Ctrl+Space (completion), etc.
> (Visible collisions are the relatively benign case; apps like LM Studio block them outright.)
> This is not an IMS-specific problem — it is a general phenomenon where keystroke combinations directed at the IME get nullified. So I recommend specifying keys that are not normally used (such as F13–F24) for IMS, and leaving them as ANYKEY in Via.

**(5) IMS Bypass: a global bypass switch for IMS (free key choice)**

Toggles into ordinary keyboard mode, where you can use romaji input or JIS-kana input as on a regular keyboard.
(This is easy on a keyboard that physically has both henkan/muhenkan keys and a spacebar; on a 40 % keyboard like mine, you will need to reassign the spacebar to henkan/muhenkan and so on.)

```c
#define IMS_BYPASS      LCTL(LSFT(KC_X))  // Meaning Ctrl+Shift+X.
```

**(6) IMS Reset: reset IMS internal state (free key choice)**

Originally I used this key because, without notifications from the IME monitor, moving between windows would cause mode drift. With `IME_monitor` running, I consider it no longer necessary.

```c
#define IMS_RESET       LCTL(LALT(KC_Q))  // Meaning Ctrl+Alt+Q.
```

**(7) enum custom_keycodes**

If the IME receives an alphanumeric/symbol key and you want it to produce some special character output, you can extend the output set by adding entries here and in `language.c`. (Though personally I can't think of a use case.)

**(8) Timing settings / composite character count**

For the chord timer, people developing IMEs or QMK-based input often cite a figure around 200 msec, but Google Gemini insisted, after analyzing the QMK source code, that around 50 msec is sufficient, so I left it around that level.
In my use it has been no problem at all.
(That said, I decided I wouldn't support 3-key chords, so I haven't looked into this deeply; if you're developing for those, it will likely matter. This is separate from the composite-character count below.)

```c
#define IMS_COMBO_TIMEOUT 80      /* Chord timer (mSec) */
```

In Japanese (a clear sound + dakuten/handakuten) or in Western languages (umlauts and the like), the composition is 2 characters. But Vietnamese has a great many characters where a vowel diacritic and a tone mark overlap (e.g. ế, ộ).
According to the AI, the current state requires 3–4 keystrokes such as e + e + f, and Gemini agreed that internationalization is something to keep in mind. Claude was much harsher about it.

```c
#define IMS_MAX_COMBO_LEN 2
```

**(9) Host IME monitor connection (VIA Custom Value protocol)**

This part was completed entirely by leaving it to the AI.
When the keyboard is connected, it responds to a query from IMS_Monitor with the signature `IMS1`, and gets registered as a target keyboard for notifications.
The notification carries information corresponding to IME on/off and alphanumeric/kana toggle — only 2 × 2 worth of information.
That is, the IME side distinguishes 6 states (3 kana variants + 2 alphanumeric variants + direct input with IME off), but these are converted into the above information for transmission.

> If you want to try a similar development on Mac, you will need to build it against the Mac API. Unfortunately I am not inclined to buy a Mac and cannot verify on real hardware, so I have not supported it.
> (Even on Windows, the AI initially tried 2–3 times based on outdated information, and it didn't work.)

### 3-5. layout.json (Via registration)

After the firmware is installed on the keyboard, this is what you need to open the keyboard in the Via web app.

> **Note:** JSON files do not accept comments. Also make sure the whole thing is wrapped in `{}` and watch for trailing commas. Basic info such as USB IDs is read from `keyboard.json` and filled in.
> (Since this is Via-specific, item names may subtly differ from current QMK, so be careful.)

```jsonc
"name": "YMD40 air40 ims",             // (anything appropriate)
"vendorId": "0x45D4",                  // Maker's USB-IF registration info (no faking)
"productId": "0x0911",                 // Same as above
"matrix": { "cols": 12, "rows": 4 },   // Column count, row count
"menus": ["qmk_rgb_matrix"],           // Listed by the maker as a function in keyboard.json
"firmwareVersion": 0,                  // (anything appropriate)
"layouts": {
    "labels": [ ["SpaceBar", "2Ux2", "2Ux1_C", "2Ux1_R", "2Ux1_L", "All_1U"] ],
                                       // Via's selection menu expands according to the above.
    "keymap": [
      [ {"c":"#cccccc"}, "0,0", {"c":"#ffffff"}, "0,1", "0,2", "0,3", "0,4", "0,5",
        {"c":"#aaaaaa"}, "0,6", "0,7", "0,8", "0,9", "0,10", {"c":"#cccccc"}, "0,11" ],

        // For each row: color directive opens with {"c":"#cccccc"}, key position is "0,0", and so on.
        // For the bottom row, corresponding to "labels", up to 5 alternatives can be listed as below.
        // xyz describe relative display positions in the Via UI: x = column offset, y = row offset, w = key width.

      [ "3,0", "3,1", "3,2", "3,3", {"w":2},"3,4\n\n\n0,0", {"w":2},"3,7\n\n\n0,0", "3,8",
        "3,9", "3,10", "3,11" ],
      [ {"x":4, "y":0.5}, "3,4\n\n\n0,1", {"w":2}, "3,5\n\n\n0,1", "3,7\n\n\n0,1" ],
      [ {"x":4, "y":1.5}, "3,4\n\n\n0,2", "3,5\n\n\n0,2", {"w":2}, "3,7\n\n\n0,2" ],
      [ {"x":4, "y":2.5, "w":2}, "3,4\n\n\n0,3", "3,6\n\n\n0,3", "3,7\n\n\n0,3" ],
      [ {"x":4, "y":3.5}, "3,4\n\n\n0,4", "3,5\n\n\n0,4", "3,6\n\n\n0,4", "3,7\n\n\n0,4" ]
    ]
"customKeycodes": [
  {"name": "A_",  "title": "J_A", "shortName": "A_"},

    // (omitted) Via cannot write Unicode kana such as "あ" directly, but it can hold up to 256 custom keycodes.
    // Remap can handle Unicode kana, but as of now (April 2026) it can only hold around 30.
    // Also, this is unrelated to language.c, so the order here is simply the display order in Via.
    // If unsure about any code, ask an AI in the form: "What is ~ in the ~ file of QMK?"
    // Supporting your own staggered or split keyboard will likely require fairly intricate descriptions.
    // (You could also accept it and settle for a rectangular grid layout.)
```

## 4. Compile and install

**(1) Compile**

During QMK setup, `qmk_firmware` is already registered as QMK's root directory.
If compilation fails for lack of space, you can free up space by setting unwanted RGB_matrix entries to `false` in the parent directory's `keyboard.json`.

```bash
$ qmk clean
$ qmk compile -kb ymdk/ymd40/air40 -km via_ims    # The trailing token is your target source-code directory.
```

**(2) Install**

- Using the QMK Tool app is the simplest path.
- Most keyboards have a reset switch, and resetting them causes QMK Tool to recognize them in write mode. For keyboards without a reset switch, or where the switch is hidden inside the case and out of reach, you can use the "bootmagic" trick: hold down the top-left key (position 0,0) while inserting the USB cable.
- If the keyboard isn't recognized due to a missing win-usb driver, you can install one with the tool "zadig."

> **Note:** Since `keymap.c` is empty, none of the keys will respond at this stage. (Only the RGB will be glowing, I imagine.)

## 5. Key configuration and saving in Via

**(1) Load layout.json**

Go to <https://usevia.app/>, open the Design tab from the settings in the top menu, and load your prepared `layout.json`.
Use the toggle switches etc. to match the keyboard layout you actually use.

**(2) Authenticate the keyboard and proceed to "Keyboard Configuration" in the top-left of the menu.**

- In Via, configuration is a two-step process: click the target key, then click an icon from the row that appears below.
  There is no "register" button — changes take effect immediately.
- Layer switching offers 6 selections (0–5) in the upper-left.
- For layer-movement keys, `MO(n)` makes the layer-n keys available only while held, whereas `TO(n)` jumps to layer n and stays there. (There are many other complex layer operations, but I only use these two.)
- The save/load buttons in the lower-left let you save your `keymap.json` for your own use.


End

/* =============================================================================
 * IME monitor for IMS-capable QMK keyboards
 *
 * 動作:
 *   (1) Windows 側の WinEvent / RawInput / Timer をトリガに IME 実状態を取得
 *   (2) QMK Raw HID エンドポイント (UsagePage=0xFF60 / Usage=0x61) を列挙
 *   (3) 各機に VIA Custom Value GET (channel=0x49, value=HELLO) を送信
 *       "IMS1" マジックで応答した機だけを IMS 対応として登録
 *   (4) IME 状態が変化したら、登録済み全機に VIA Custom Value SET (STATE) を broadcast
 *
 * 送信情報は ime_on(0/1) と alfa(0/1) の 2bit のみ。
 * ひらがな/カタカナ/全角半角の区別はキーボード側に送らない。
 *
 * VIA プロトコルに相乗りする理由:
 *   VIA 有効キーボードでは raw_hid_receive が via.c で定義済み (非 weak) のため、
 *   独自コマンド ID を直接 raw_hid で投げると衝突する。VIA の Custom Value 機構
 *   (id_custom_set_value=0x07 / id_custom_get_value=0x08) に相乗りすれば安全。
 * ============================================================================= */

/* release build では console window を出さない。
 * debug build (cargo run) では従来通り console にログを出す。 */
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]


use std::sync::mpsc::{sync_channel, RecvTimeoutError, SyncSender};
use std::time::{Duration, Instant};

use hidapi::{HidApi, HidDevice};
use once_cell::sync::OnceCell;
use windows::core::w;
use windows::Win32::{
    Devices::HumanInterfaceDevice::{HID_USAGE_GENERIC_KEYBOARD, HID_USAGE_PAGE_GENERIC},
    Foundation::*,
    System::LibraryLoader::GetModuleHandleW,
    UI::{
        Accessibility::*,
        Input::{Ime::ImmGetDefaultIMEWnd, *},
        WindowsAndMessaging::*,
    },
};

/* =============================================================================
 * QMK / VIA / IMS protocol constants (ims_lang.h と同期)
 * ============================================================================= */
const QMK_RAW_USAGE_PAGE: u16 = 0xFF60;
const QMK_RAW_USAGE:      u16 = 0x0061;

/* VIA Custom Value command IDs */
const VIA_ID_CUSTOM_SET_VALUE: u8 = 0x07;
const VIA_ID_CUSTOM_GET_VALUE: u8 = 0x08;
const VIA_ID_UNHANDLED:        u8 = 0xFF;

/* IMS が使う channel_id と value_id */
const IMS_VIA_CHANNEL_ID:  u8 = 0x49;   /* 'I' */
const IMS_VAL_STATE:       u8 = 0x01;
const IMS_VAL_HELLO:       u8 = 0x02;
#[allow(dead_code)]
const IMS_VAL_HEARTBEAT:   u8 = 0x03;

const MAGIC: &[u8; 4] = b"IMS1";

const RAW_PAYLOAD_LEN: usize = 32;

/* =============================================================================
 * IME state types
 * ============================================================================= */
static SENDER: OnceCell<SyncSender<()>> = OnceCell::new();

const IMC_GETOPENSTATUS:     usize = 0x0005;
const IMC_GETCONVERSIONMODE: usize = 0x0001;
const IME_CMODE_NATIVE:      u32   = 0x0001;
const IME_CMODE_KATAKANA:    u32   = 0x0002;
const IME_CMODE_FULLSHAPE:   u32   = 0x0008;

/// IME の詳細状態 (ログ表示用)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ImeState {
    Off,           // 直接入力
    Hiragana,      // ひらがな
    FullKatakana,  // 全角カタカナ
    HalfKatakana,  // 半角カタカナ
    FullAlpha,     // 全角英数
    HalfAlpha,     // 半角英数（IMEオン）
}

/// キーボード側へ送る最小情報 (2bit)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Ime2 {
    ime_on: bool,
    alfa:   bool, // ime_on=false のとき意味なし
}

impl ImeState {
    fn to_ime2(self) -> Ime2 {
        match self {
            ImeState::Off =>
                Ime2 { ime_on: false, alfa: false },
            ImeState::Hiragana
            | ImeState::FullKatakana
            | ImeState::HalfKatakana =>
                Ime2 { ime_on: true, alfa: false },
            ImeState::FullAlpha | ImeState::HalfAlpha =>
                Ime2 { ime_on: true, alfa: true },
        }
    }
}

fn hwnd_valid(h: HWND) -> bool { h.0 != 0 }

fn get_ime_state() -> Option<ImeState> {
    unsafe {
        let foreground_hwnd = GetForegroundWindow();
        if !hwnd_valid(foreground_hwnd) { return None; }

        let thread_id = GetWindowThreadProcessId(foreground_hwnd, None);
        let mut gui_info = GUITHREADINFO {
            cbSize: std::mem::size_of::<GUITHREADINFO>() as u32,
            ..Default::default()
        };
        let target_hwnd = if GetGUIThreadInfo(thread_id, &mut gui_info).is_ok()
            && hwnd_valid(gui_info.hwndFocus)
        {
            gui_info.hwndFocus
        } else {
            foreground_hwnd
        };

        let ime_wnd = ImmGetDefaultIMEWnd(target_hwnd);
        if !hwnd_valid(ime_wnd) { return None; }

        let mut open: usize = 0;
        if SendMessageTimeoutW(
            ime_wnd, WM_IME_CONTROL,
            WPARAM(IMC_GETOPENSTATUS), LPARAM(0),
            SMTO_NORMAL | SMTO_ABORTIFHUNG, 100,
            Some(&mut open),
        ).0 == 0 { return None; }

        if open == 0 { return Some(ImeState::Off); }

        let mut conv: usize = 0;
        if SendMessageTimeoutW(
            ime_wnd, WM_IME_CONTROL,
            WPARAM(IMC_GETCONVERSIONMODE), LPARAM(0),
            SMTO_NORMAL | SMTO_ABORTIFHUNG, 100,
            Some(&mut conv),
        ).0 == 0 { return None; }

        let c = conv as u32;
        let state = if c & IME_CMODE_NATIVE != 0 {
            if c & IME_CMODE_KATAKANA != 0 {
                if c & IME_CMODE_FULLSHAPE != 0 { ImeState::FullKatakana }
                else                            { ImeState::HalfKatakana }
            } else {
                ImeState::Hiragana
            }
        } else {
            if c & IME_CMODE_FULLSHAPE != 0 { ImeState::FullAlpha }
            else                            { ImeState::HalfAlpha }
        };

        Some(state)
    }
}

/* =============================================================================
 * Windows event hooks (既存ロジックの保持)
 * ============================================================================= */
extern "system" fn win_event_proc(
    _: HWINEVENTHOOK, event: u32, _: HWND,
    _: i32, _: i32, _: u32, _: u32,
) {
    if event == EVENT_SYSTEM_FOREGROUND {
        if let Some(s) = SENDER.get() { let _ = s.try_send(()); }
    }
}

extern "system" fn wndproc(_hwnd: HWND, msg: u32, _wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    unsafe {
        match msg {
            WM_INPUT => {
                let mut size = 0u32;
                GetRawInputData(
                    HRAWINPUT(lparam.0 as _), RID_INPUT, None,
                    &mut size,
                    std::mem::size_of::<RAWINPUTHEADER>() as u32,
                );
                let mut buf = vec![0u8; size as usize];
                if GetRawInputData(
                    HRAWINPUT(lparam.0 as _), RID_INPUT,
                    Some(buf.as_mut_ptr() as _), &mut size,
                    std::mem::size_of::<RAWINPUTHEADER>() as u32,
                ) == size {
                    let raw = &*(buf.as_ptr() as *const RAWINPUT);
                    if raw.header.dwType == RIM_TYPEKEYBOARD.0
                        && raw.data.keyboard.Message == WM_KEYDOWN
                    {
                        if let Some(s) = SENDER.get() { let _ = s.try_send(()); }
                    }
                }
                LRESULT(0)
            }
            WM_TIMER => {
                if let Some(s) = SENDER.get() { let _ = s.try_send(()); }
                LRESULT(0)
            }
            WM_DESTROY => { PostQuitMessage(0); LRESULT(0) }
            _ => DefWindowProcW(_hwnd, msg, _wparam, lparam),
        }
    }
}

fn ui_loop() -> Result<(), Box<dyn std::error::Error>> {
    unsafe {
        let hook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            None, Some(win_event_proc), 0, 0, WINEVENT_OUTOFCONTEXT,
        );

        let hinstance = GetModuleHandleW(None)?;
        let class_name = w!("ImeMonitorWindow");

        let wc = WNDCLASSW {
            lpfnWndProc: Some(wndproc),
            hInstance: hinstance.into(),
            lpszClassName: class_name,
            ..Default::default()
        };
        RegisterClassW(&wc);

        let hwnd = CreateWindowExW(
            WINDOW_EX_STYLE::default(),
            class_name,
            w!("monitor"),
            WINDOW_STYLE::default(),
            0, 0, 0, 0,
            HWND_MESSAGE,
            None,
            hinstance,
            None,
        );
        if !hwnd_valid(hwnd) {
            return Err("CreateWindowExW failed".into());
        }

        let rid = RAWINPUTDEVICE {
            usUsagePage: HID_USAGE_PAGE_GENERIC,
            usUsage: HID_USAGE_GENERIC_KEYBOARD,
            dwFlags: RIDEV_INPUTSINK,
            hwndTarget: hwnd,
        };
        RegisterRawInputDevices(&[rid], std::mem::size_of::<RAWINPUTDEVICE>() as u32)?;

        SetTimer(hwnd, 1, 300, None); // 300ms マウス操作対策

        let mut msg = MSG::default();
        while GetMessageW(&mut msg, None, 0, 0).as_bool() {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        let _ = KillTimer(hwnd, 1);
        if hook.0 != 0 { UnhookWinEvent(hook); }
    }
    Ok(())
}

/* =============================================================================
 * HID Raw (VIA Custom Value) : 発見と broadcast
 * ============================================================================= */
struct Keyboard {
    dev:  HidDevice,
    path: String,
}

/// VIA Custom Value リクエストを Report ID 0 付きで 33byte バッファに組み立てる
/// (Windows HID write は先頭に Report ID を要求するため)
fn build_via_request(via_cmd: u8, value_id: u8, payload: &[u8]) -> [u8; RAW_PAYLOAD_LEN + 1] {
    let mut buf = [0u8; RAW_PAYLOAD_LEN + 1];
    buf[0] = 0x00; // Report ID
    buf[1] = via_cmd;
    buf[2] = IMS_VIA_CHANNEL_ID;
    buf[3] = value_id;
    for (i, b) in payload.iter().enumerate() {
        if 4 + i >= buf.len() { break; }
        buf[4 + i] = *b;
    }
    buf
}

/// 接続中の QMK Raw HID エンドポイントを全て列挙し、HELLO GET を投げて
/// マジック "IMS1" で応答した機だけを返す。
fn discover_keyboards(api: &mut HidApi) -> Vec<Keyboard> {
    let _ = api.refresh_devices();

    // DeviceInfo の所有物を先に vec に退避 (api への借用と open_path の衝突回避)
    let candidates: Vec<(std::ffi::CString, u16, u16, String)> = api
        .device_list()
        .filter(|d| d.usage_page() == QMK_RAW_USAGE_PAGE && d.usage() == QMK_RAW_USAGE)
        .map(|d| (
            d.path().to_owned(),
            d.vendor_id(),
            d.product_id(),
            d.path().to_string_lossy().into_owned(),
        ))
        .collect();

    let mut result = Vec::new();
    for (path_c, vid, pid, path_s) in candidates {
        let dev = match api.open_path(&path_c) {
            Ok(d) => d,
            Err(e) => {
                eprintln!("[hid] open failed vid={:04X} pid={:04X}: {}", vid, pid, e);
                continue;
            }
        };

        // GET channel=0x49 value=HELLO
        let hello = build_via_request(VIA_ID_CUSTOM_GET_VALUE, IMS_VAL_HELLO, &[]);
        if let Err(e) = dev.write(&hello) {
            eprintln!("[hid] hello write failed vid={:04X} pid={:04X}: {}", vid, pid, e);
            continue;
        }

        // 応答待ち (最大 300ms)
        // 期待する応答: data[0]=0x08, data[1]=0x49, data[2]=0x02, data[3..7]="IMS1", data[7]=ver
        // IMS 非対応機は data[0]=0xFF (id_unhandled) または無応答
        let mut ack_ok = false;
        let deadline = Instant::now() + Duration::from_millis(300);
        let mut rbuf = [0u8; RAW_PAYLOAD_LEN];
        while Instant::now() < deadline {
            match dev.read_timeout(&mut rbuf, 50) {
                Ok(n) if n >= 8 => {
                    if rbuf[0] == VIA_ID_CUSTOM_GET_VALUE
                        && rbuf[1] == IMS_VIA_CHANNEL_ID
                        && rbuf[2] == IMS_VAL_HELLO
                        && &rbuf[3..7] == MAGIC
                    {
                        let ver = rbuf[7];
                        println!(
                            "[hid] IMS keyboard registered: vid={:04X} pid={:04X} ver={} ({})",
                            vid, pid, ver, path_s
                        );
                        ack_ok = true;
                        break;
                    }
                    if rbuf[0] == VIA_ID_UNHANDLED {
                        // VIA 機だが IMS 非対応 — 静かにスキップ
                        break;
                    }
                    // 別コマンドの応答が混在している可能性 — 続行
                }
                Ok(_) => { /* 短すぎパケット無視 */ }
                Err(_) => { /* timeout 等、ループ継続 */ }
            }
        }

        if ack_ok {
            result.push(Keyboard { dev, path: path_s });
        }
    }
    result
}

/// 登録済み全キーボードに IME 実状態を broadcast。
/// 書き込みに失敗したデバイスはリストから除外 (ホットプラグ対応)。
fn broadcast_state(keyboards: &mut Vec<Keyboard>, s: Ime2) {
    let payload = [s.ime_on as u8, s.alfa as u8];
    let out = build_via_request(VIA_ID_CUSTOM_SET_VALUE, IMS_VAL_STATE, &payload);

    let mut dropped = 0usize;
    keyboards.retain(|kb| match kb.dev.write(&out) {
        Ok(_) => {
            // VIA は SET に対しても応答を返すので、受信バッファを drain
            // (放置しても後段で上書きされるが、念のため)
            let mut dump = [0u8; RAW_PAYLOAD_LEN];
            let _ = kb.dev.read_timeout(&mut dump, 10);
            true
        }
        Err(e) => {
            eprintln!("[hid] write failed on {}: {} (drop)", kb.path, e);
            dropped += 1;
            false
        }
    });

    if dropped > 0 {
        eprintln!("[hid] {} device(s) dropped, will rediscover", dropped);
    }
}

/* =============================================================================
 * main
 * ============================================================================= */
fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("IME state monitor started. Press Ctrl+C to exit.");

    let (sender, receiver) = sync_channel(1);
    SENDER.set(sender).unwrap();

    std::thread::spawn(move || {
        let mut api = match HidApi::new() {
            Ok(a) => a,
            Err(e) => {
                eprintln!("[hid] HidApi init failed: {}", e);
                return;
            }
        };

        let mut keyboards = discover_keyboards(&mut api);
        if keyboards.is_empty() {
            println!("[hid] No IMS-capable keyboard found at startup. Will keep trying.");
        }
        let mut last_rediscover = Instant::now();

        let mut last_state: Option<ImeState> = None;
        let mut last_ime2:  Option<Ime2>     = None;

        loop {
            let tick = receiver.recv_timeout(Duration::from_secs(5));
            match tick {
                Err(RecvTimeoutError::Disconnected) => return,
                Ok(()) | Err(RecvTimeoutError::Timeout) => {
                    // キーイベント/フォーカス変化後、IME が状態を反映するまで
                    // の短い猶予 (現行実装と同じ 100ms)。
                    std::thread::sleep(Duration::from_millis(100));

                    if let Some(cur) = get_ime_state() {
                        if Some(cur) != last_state {
                            let i2 = cur.to_ime2();
                            println!(
                                "IME: {:?}  -> send ime_on={} alfa={}",
                                cur, i2.ime_on as u8, i2.alfa as u8
                            );
                            last_state = Some(cur);

                            // 送信トリガは Ime2 単位 (ひらがな↔カタカナの
                            // 内部遷移ではキーボードに送らない)。
                            if Some(i2) != last_ime2 {
                                last_ime2 = Some(i2);
                                if keyboards.is_empty() {
                                    keyboards = discover_keyboards(&mut api);
                                    last_rediscover = Instant::now();
                                }
                                if !keyboards.is_empty() {
                                    broadcast_state(&mut keyboards, i2);
                                }
                            }
                        }
                    }

                    // 定期的な re-enumerate (ホットプラグ対応)
                    if keyboards.is_empty()
                        && last_rediscover.elapsed() > Duration::from_secs(3)
                    {
                        keyboards = discover_keyboards(&mut api);
                        last_rediscover = Instant::now();
                    } else if last_rediscover.elapsed() > Duration::from_secs(30) {
                        // 30秒ごとに新規接続機もチェック
                        let existing_paths: std::collections::HashSet<String> =
                            keyboards.iter().map(|k| k.path.clone()).collect();
                        let mut newly = discover_keyboards(&mut api);
                        newly.retain(|k| !existing_paths.contains(&k.path));
                        for nk in newly {
                            keyboards.push(nk);
                        }
                        last_rediscover = Instant::now();
                    }
                }
            }
        }
    });

    ui_loop()?;
    Ok(())
}

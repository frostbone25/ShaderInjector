#pragma once

#include <string>
#include <vector>

namespace Keycodes
{
	struct KeycodeOption
	{
		int value = 0;
		std::string name;
	};

	//mouse buttons
	static int Keycodes_LeftMouse = 1; //0x01 Left mouse button
	static int Keycodes_RightMouse = 2; //0x02 Right mouse button
	static int Keycodes_ControlBreak = 3; //0x03 Control - break processing
	static int Keycodes_MiddleMouse = 4; //0x04 Middle mouse button(three - button mouse)
	static int Keycodes_X1MouseButton = 5; //0x05 X1 mouse button
	static int Keycodes_X2MouseButton = 6; //0x06 X2 mouse button

	//top number row
	static int Keycodes_0 = 48; //0x30 0 key
	static int Keycodes_1 = 49; //0x31 1 key
	static int Keycodes_2 = 50; //0x32 2 key
	static int Keycodes_3 = 51; //0x33 3 key
	static int Keycodes_4 = 52; //0x34 4 key
	static int Keycodes_5 = 53; //0x35 5 key
	static int Keycodes_6 = 54; //0x36 6 key
	static int Keycodes_7 = 55; //0x37 7 key
	static int Keycodes_8 = 56; //0x38 8 key
	static int Keycodes_9 = 57; //0x39 9 key

	//numpad
	static int Keycodes_Numpad0 = 96; //0x60 Numeric keypad 0 key
	static int Keycodes_Numpad1 = 97; //0x61 Numeric keypad 1 key
	static int Keycodes_Numpad2 = 98; //0x62 Numeric keypad 2 key
	static int Keycodes_Numpad3 = 99; //0x63 Numeric keypad 3 key
	static int Keycodes_Numpad4 = 100; //0x64 Numeric keypad 4 key
	static int Keycodes_Numpad5 = 101; //0x65 Numeric keypad 5 key
	static int Keycodes_Numpad6 = 102; //0x66 Numeric keypad 6 key
	static int Keycodes_Numpad7 = 103; //0x67 Numeric keypad 7 key
	static int Keycodes_Numpad8 = 104; //0x68 Numeric keypad 8 key
	static int Keycodes_Numpad9 = 105; //0x69 Numeric keypad 9 key

	//alphabetical
	static int Keycodes_A = 65; //0x41 A key
	static int Keycodes_B = 66; //0x42 B key
	static int Keycodes_C = 67; //0x43 C key
	static int Keycodes_D = 68; //0x44 D key
	static int Keycodes_E = 69; //0x45 E key
	static int Keycodes_F = 70; //0x46 F key
	static int Keycodes_G = 71; //0x47 G key
	static int Keycodes_H = 72; //0x48 H key
	static int Keycodes_I = 73; //0x49 I key
	static int Keycodes_J = 74; //0x4A J key
	static int Keycodes_K = 75; //0x4B K key
	static int Keycodes_L = 76; //0x4C L key
	static int Keycodes_M = 77; //0x4D M key
	static int Keycodes_N = 78; //0x4E N key
	static int Keycodes_O = 79; //0x4F O key
	static int Keycodes_P = 80; //0x50 P key
	static int Keycodes_Q = 81; //0x51 Q key
	static int Keycodes_R = 82; //0x52 R key
	static int Keycodes_S = 83; //0x53 S key
	static int Keycodes_T = 84; //0x54 T key
	static int Keycodes_U = 85; //0x55 U key
	static int Keycodes_V = 86; //0x56 V key
	static int Keycodes_W = 87; //0x57 W key
	static int Keycodes_X = 88; //0x58 X key
	static int Keycodes_Y = 89; //0x59 Y key
	static int Keycodes_Z = 90; //0x5A Z key

	//function keys
	static int Keycodes_F1 = 112; //0x70 F1 key
	static int Keycodes_F2 = 113; //0x71 F2 key
	static int Keycodes_F3 = 114; //0x72 F3 key
	static int Keycodes_F4 = 115; //0x73 F4 key
	static int Keycodes_F5 = 116; //0x74 F5 key
	static int Keycodes_F6 = 117; //0x75 F6 key
	static int Keycodes_F7 = 118; //0x76 F7 key
	static int Keycodes_F8 = 119; //0x77 F8 key
	static int Keycodes_F9 = 120; //0x78 F9 key
	static int Keycodes_F10 = 121; //0x79 F10 key
	static int Keycodes_F11 = 122; //0x7A F11 key
	static int Keycodes_F12 = 123; //0x7B F12 key
	static int Keycodes_F13 = 124; //0x7C F13 key
	static int Keycodes_F14 = 125; //0x7D F14 key
	static int Keycodes_F15 = 126; //0x7E F15 key
	static int Keycodes_F16 = 127; //0x7F F16 key
	static int Keycodes_F17 = 128; //0x80 F17 key
	static int Keycodes_F18 = 129; //0x81 F18 key
	static int Keycodes_F19 = 130; //0x82 F19 key
	static int Keycodes_F20 = 131; //0x83 F20 key
	static int Keycodes_F21 = 132; //0x84 F21 key
	static int Keycodes_F22 = 133; //0x85 F22 key
	static int Keycodes_F23 = 134; //0x86 F23 key
	static int Keycodes_F24 = 135; //0x87 F24 key

	//other keyboard keys
	static int Keycodes_Multiply = 106; //0x6A Multiply key
	static int Keycodes_Add = 107; //0x6B Add key
	static int Keycodes_Separator = 108; //0x6C Separator key
	static int Keycodes_Subtract = 109; //0x6D Subtract key
	static int Keycodes_Decimal = 110; //0x6E Decimal key
	static int Keycodes_Divide = 111; //0x6F Divide key
	static int Keycodes_Backspace = 8; //0x08 BACKSPACE key
	static int Keycodes_Tab = 9; //0x09 TAB key
	static int Keycodes_Clear = 12; //0x0C CLEAR key
	static int Keycodes_Enter = 13; //0x0D ENTER key
	static int Keycodes_Shift = 16; //0x10 SHIFT key
	static int Keycodes_Ctrl = 17; //0x11 CTRL key
	static int Keycodes_Alt = 18; //0x12 ALT key
	static int Keycodes_Pause = 19; //0x13 PAUSE key
	static int Keycodes_CapsLock = 20; //0x14 CAPS LOCK key
	static int Keycodes_Escape = 27; //0x1B ESC key
	static int Keycodes_Space = 32; //0x20 SPACEBAR
	static int Keycodes_PageUp = 33; //0x21 PAGE UP key
	static int Keycodes_PageDown = 34; //0x22 PAGE DOWN key
	static int Keycodes_End = 35; //0x23 END key
	static int Keycodes_Home = 36; //0x24 HOME key
	static int Keycodes_LeftArrow = 37; //0x25 LEFT ARROW key
	static int Keycodes_UpArrow = 38; //0x26 UP ARROW key
	static int Keycodes_RightArrow = 39; //0x27 RIGHT ARROW key
	static int Keycodes_DownArrow = 40; //0x28 DOWN ARROW key
	static int Keycodes_Select = 41; //0x29 SELECT key
	static int Keycodes_Print = 42; //0x2A PRINT key
	static int Keycodes_Execute = 43; //0x2B EXECUTE key
	static int Keycodes_PrintScreen = 44; //0x2C PRINT SCREEN key
	static int Keycodes_Insert = 45; //0x2D INS key
	static int Keycodes_Delete = 46; //0x2E DEL key
	static int Keycodes_Help = 47; //0x2F HELP key
	static int Keycodes_LeftWindows = 91; //0x5B Left Windows key(Natural keyboard)
	static int Keycodes_RightWindows = 92; //0x5C Right Windows key(Natural keyboard)
	static int Keycodes_Applications = 93; //0x5D Applications key(Natural keyboard)
	static int Keycodes_Sleep = 95; //0x5F Computer Sleep key
	static int Keycodes_NumLock = 144; //0x90 NUM LOCK key
	static int Keycodes_ScrollLock = 145; //0x91 SCROLL LOCK key
	static int Keycodes_LeftShift = 160; //0xA0 Left SHIFT key
	static int Keycodes_RightShift = 161; //0xA1 Right SHIFT key
	static int Keycodes_LeftControl = 162; //0xA2 Left CONTROL key
	static int Keycodes_RightControl = 163; //0xA3 Right CONTROL key
	static int Keycodes_LeftAlt = 164; //0xA4 Left ALT key
	static int Keycodes_RightAlt = 165; //0xA5 Right ALT key
	static int Keycodes_BrowserBack = 166; //0xA6 Browser Back key
	static int Keycodes_BrowserForward = 167; //0xA7 Browser Forward key
	static int Keycodes_BrowserRefresh = 168; //0xA8 Browser Refresh key
	static int Keycodes_BrowserStop = 169; //0xA9 Browser Stop key
	static int Keycodes_BrowserSearch = 170; //0xAA	Browser Search key
	static int Keycodes_BrowserFavorites = 171; //0xAB Browser Favorites key
	static int Keycodes_BrowserStartAndHome = 172; //0xAC Browser Start and Home key
	static int Keycodes_VolumeMute = 173; //0xAD Volume Mute key
	static int Keycodes_VolumeDown = 174; //0xAE Volume Down key
	static int Keycodes_VolumeUp = 175; //0xAF Volume Up key
	static int Keycodes_NextTrack = 176; //0xB0 Next Track key
	static int Keycodes_PreviousTrack = 177; //0xB1 Previous Track key
	static int Keycodes_StopMedia = 178; //0xB2 Stop Media key
	static int Keycodes_PlayPauseMedia = 179; //0xB3 Play / Pause Media key
	static int Keycodes_StartMail = 180; //0xB4 Start Mail key
	static int Keycodes_SelectMedia = 181; //0xB5 Select Media key
	static int Keycodes_StartApplication1 = 182; //0xB6 Start Application 1 key
	static int Keycodes_StartApplication2 = 183; //0xB7 Start Application 2 key
	static int Keycodes_MiscCharacters = 186; //0xBA Used for miscellaneous characters; it can vary by keyboard.For the US standard keyboard, the ';:' key
	static int Keycodes_Packet = 231; //0xE7 Used to pass Unicode characters as if they were keystrokes.The VK_PACKET key is the low word of a 32 - bit Virtual Key value used for non - keyboard input methods.For more information, see Remark in KEYBDINPUT, SendInput, WM_KEYDOWN, and WM_KEYUP
	static int Keycodes_Attn = 246; //0xF6 Attn key
	static int Keycodes_CrSel = 247; //0xF7 CrSel key
	static int Keycodes_ExSel = 248; //0xF8 ExSel key
	static int Keycodes_EraseEOF = 249; //0xF9 Erase EOF key
	static int Keycodes_Play = 250; //0xFA Play key
	static int Keycodes_Zoom = 251; //0xFB Zoom key
	static int Keycodes_PA1 = 253; //0xFD PA1 key
	//static int Keycodes_Clear = 254; //0xFE Clear key

	//oem stuff
	static int Keycodes_OEM_Plus = 187; //0xBB For any country / region, the '+' key
	static int Keycodes_OEM_Comma = 188; //0xBC For any country / region, the ',' key
	static int Keycodes_OEM_Minus = 189; //0xBD For any country / region, the '-' key
	static int Keycodes_OEM_Period = 190; //0xBE For any country / region, the '.' key
	static int Keycodes_OEM_2 = 191; //0xBF Used for miscellaneous characters; it can vary by keyboard.For the US standard keyboard, the '/?' key
	static int Keycodes_OEM_3 = 192; //0xC0 Used for miscellaneous characters; it can vary by keyboard.For the US standard keyboard, the '`~' key
	static int Keycodes_OEM_4 = 219; //0xDB Used for miscellaneous characters; it can vary by keyboard.For the US standard keyboard, the '[{' key
	static int Keycodes_OEM_5 = 220; //0xDC Used for miscellaneous characters; it can vary by keyboard.For the US standard keyboard, the '\|' key
	static int Keycodes_OEM_6 = 221; //0xDD Used for miscellaneous characters; it can vary by keyboard.For the US standard keyboard, the ']}' key
	static int Keycodes_OEM_7 = 222; //0xDE Used for miscellaneous characters; it can vary by keyboard.For the US standard keyboard, the 'single-quote/double-quote' key
	static int Keycodes_OEM_8 = 223; //0xDF Used for miscellaneous characters; it can vary by keyboard.
	static int Keycodes_OEM_102 = 226; //0xE2 The <> keys on the US standard keyboard, or the \\ | key on the non - US 102 - key keyboard

	//ime stuff
	static int Keycodes_IME_KanaHanguel = 21; //0x15 (IME Kana mode) (IME Hanguel mode(maintained for compatibility; use VK_HANGUL)) (IME Hangul mode)
	static int Keycodes_IME_On = 22; //0x16 IME On
	static int Keycodes_IME_Junja = 23; //0x17 IME Junja mode
	static int Keycodes_IME_Final = 24; //0x18 IME final mode
	static int Keycodes_IME_HanjaKanji = 25; //0x19 (IME Hanja mode) (IME Kanji mode)
	static int Keycodes_IME_Off = 26; //0x1A IME Off
	static int Keycodes_IME_Convert = 28; //0x1C IME convert
	static int Keycodes_IME_NonConvert = 29; //0x1D IME nonconvert
	static int Keycodes_IME_Accept = 30; //0x1E IME accept
	static int Keycodes_IME_ModeChangeRequest = 31; //0x1F IME mode change request
	static int Keycodes_IME_Process = 229; //0xE5 IME PROCESS key

	std::string KeycodeToString(int keycode);
	const std::vector<KeycodeOption>& SelectableKeycodes();
}

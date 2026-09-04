#include "Keycodes.h"

#include <utility>

namespace Keycodes
{
	std::string KeycodeToString(int keycode)
	{
		switch (keycode)
		{
			//mouse buttons
			case 1:   return "Left Mouse";
			case 2:   return "Right Mouse";
			case 3:   return "Control-Break";
			case 4:   return "Middle Mouse";
			case 5:   return "X1 Mouse";
			case 6:   return "X2 Mouse";

			//top number row
			case 48:  return "0";
			case 49:  return "1";
			case 50:  return "2";
			case 51:  return "3";
			case 52:  return "4";
			case 53:  return "5";
			case 54:  return "6";
			case 55:  return "7";
			case 56:  return "8";
			case 57:  return "9";

			//numpad
			case 96:  return "Numpad 0";
			case 97:  return "Numpad 1";
			case 98:  return "Numpad 2";
			case 99:  return "Numpad 3";
			case 100: return "Numpad 4";
			case 101: return "Numpad 5";
			case 102: return "Numpad 6";
			case 103: return "Numpad 7";
			case 104: return "Numpad 8";
			case 105: return "Numpad 9";
			case 106: return "Numpad *";
			case 107: return "Numpad +";
			case 108: return "Numpad Separator";
			case 109: return "Numpad -";
			case 110: return "Numpad .";
			case 111: return "Numpad /";

			//alphabet
			case 65:  return "A";
			case 66:  return "B";
			case 67:  return "C";
			case 68:  return "D";
			case 69:  return "E";
			case 70:  return "F";
			case 71:  return "G";
			case 72:  return "H";
			case 73:  return "I";
			case 74:  return "J";
			case 75:  return "K";
			case 76:  return "L";
			case 77:  return "M";
			case 78:  return "N";
			case 79:  return "O";
			case 80:  return "P";
			case 81:  return "Q";
			case 82:  return "R";
			case 83:  return "S";
			case 84:  return "T";
			case 85:  return "U";
			case 86:  return "V";
			case 87:  return "W";
			case 88:  return "X";
			case 89:  return "Y";
			case 90:  return "Z";

			//function keys
			case 112: return "F1";
			case 113: return "F2";
			case 114: return "F3";
			case 115: return "F4";
			case 116: return "F5";
			case 117: return "F6";
			case 118: return "F7";
			case 119: return "F8";
			case 120: return "F9";
			case 121: return "F10";
			case 122: return "F11";
			case 123: return "F12";
			case 124: return "F13";
			case 125: return "F14";
			case 126: return "F15";
			case 127: return "F16";
			case 128: return "F17";
			case 129: return "F18";
			case 130: return "F19";
			case 131: return "F20";
			case 132: return "F21";
			case 133: return "F22";
			case 134: return "F23";
			case 135: return "F24";

			//control / navigation keys
			case 8:   return "Backspace";
			case 9:   return "Tab";
			case 12:  return "Clear";
			case 13:  return "Enter";
			case 16:  return "Shift";
			case 17:  return "Ctrl";
			case 18:  return "Alt";
			case 19:  return "Pause";
			case 20:  return "Caps Lock";
			case 27:  return "Escape";
			case 32:  return "Space";
			case 33:  return "Page Up";
			case 34:  return "Page Down";
			case 35:  return "End";
			case 36:  return "Home";
			case 37:  return "Left Arrow";
			case 38:  return "Up Arrow";
			case 39:  return "Right Arrow";
			case 40:  return "Down Arrow";
			case 41:  return "Select";
			case 42:  return "Print";
			case 43:  return "Execute";
			case 44:  return "Print Screen";
			case 45:  return "Insert";
			case 46:  return "Delete";
			case 47:  return "Help";
			case 91:  return "Left Windows";
			case 92:  return "Right Windows";
			case 93:  return "Applications";
			case 95:  return "Sleep";
			case 144: return "Num Lock";
			case 145: return "Scroll Lock";
			case 160: return "Left Shift";
			case 161: return "Right Shift";
			case 162: return "Left Ctrl";
			case 163: return "Right Ctrl";
			case 164: return "Left Alt";
			case 165: return "Right Alt";

			//browser / media keys
			case 166: return "Browser Back";
			case 167: return "Browser Forward";
			case 168: return "Browser Refresh";
			case 169: return "Browser Stop";
			case 170: return "Browser Search";
			case 171: return "Browser Favorites";
			case 172: return "Browser Home";
			case 173: return "Volume Mute";
			case 174: return "Volume Down";
			case 175: return "Volume Up";
			case 176: return "Next Track";
			case 177: return "Previous Track";
			case 178: return "Stop Media";
			case 179: return "Play/Pause";
			case 180: return "Start Mail";
			case 181: return "Select Media";
			case 182: return "App 1";
			case 183: return "App 2";

			//OEM keys
			case 186: return "OEM ;:";
			case 187: return "OEM +";
			case 188: return "OEM ,";
			case 189: return "OEM -";
			case 190: return "OEM .";
			case 191: return "OEM /?";
			case 192: return "OEM `~";
			case 219: return "OEM [{";
			case 220: return "OEM \\|";
			case 221: return "OEM ]}";
			case 222: return "OEM '\"";
			case 223: return "OEM 8";
			case 226: return "OEM 102";

			//IME keys
			case 21:  return "IME Kana/Hangul";
			case 22:  return "IME On";
			case 23:  return "IME Junja";
			case 24:  return "IME Final";
			case 25:  return "IME Hanja/Kanji";
			case 26:  return "IME Off";
			case 28:  return "IME Convert";
			case 29:  return "IME NonConvert";
			case 30:  return "IME Accept";
			case 31:  return "IME Mode Change";
			case 229: return "IME Process";

			//Misc
			case 231: return "Packet";
			case 246: return "Attn";
			case 247: return "CrSel";
			case 248: return "ExSel";
			case 249: return "Erase EOF";
			case 250: return "Play";
			case 251: return "Zoom";
			case 253: return "PA1";
			case 254: return "Clear (FE)";

			default:  return "Unknown";
		}
	}

	const std::vector<KeycodeOption>& SelectableKeycodes()
	{
		static const std::vector<KeycodeOption> keycodes = []
		{
			std::vector<KeycodeOption> result;
			result.reserve(160);

			// The input hook consumes keyboard messages. Mouse virtual keys are omitted
			// so every option shown here is actionable through that same path.
			for (int value = 8; value <= 254; ++value)
			{
				std::string name = KeycodeToString(value);
				if (name != "Unknown")
					result.push_back({ value, std::move(name) });
			}

			return result;
		}();

		return keycodes;
	}
}

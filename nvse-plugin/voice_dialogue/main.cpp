// Speak Up Courier - Voice Controlled Dialog for Fallout: New Vegas (xNVSE plugin)
//
// Every frame: if the dialogue menu is open, read the topic lines on screen and
// send them to the speech helper. By default the helper listens the whole time the
// menu is open and answers whenever the player pauses; push-to-talk is optional.
// The helper answers with the option index, and we click that topic for the player.

#include "nvse/PluginAPI.h"
#include "nvse/GameAPI.h"
#include "nvse/GameTiles.h"
#include "nvse/GameUI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/Utilities.h"

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include <string>
#include <vector>

#include "helper_process.h"
#include "json_lite.h"

IDebugLog gLog("VoiceDialogue.log");

namespace
{
	// --- Game structures not (yet) in xNVSE. Offsets from JIP LN NVSE's GameUI.h. ---

	struct TopicListItem
	{
		Tile	*tile;			// 00
		int		item;			// 04
		bool	isFiltered;		// 08
	};

	struct TopicListBox		// ListBox<int>, 0x30
	{
		void					*vtbl;		// 00
		tList<TopicListItem>	list;		// 04
		Tile					*parentTile;// 0C
	};

	inline Menu *GetDialogMenu() { return *(Menu **)0x11D9510; }
	inline TopicListBox *GetTopicList(Menu *dialogMenu) { return (TopicListBox *)((UInt8 *)dialogMenu + 0x40); }
	inline TESObjectREFR *GetSpeaker(Menu *dialogMenu) { return *(TESObjectREFR **)((UInt8 *)dialogMenu + 0x80); }
	// BGSVoiceType keeps its editor ID in game (JIP LN: String editorID at +0x1C).
	inline const char *VoiceTypeName(BGSVoiceType *voice) { return voice ? *(const char **)((UInt8 *)voice + 0x1C) : nullptr; }

	// --- Settings (Data\NVSE\Plugins\VoiceDialogue\VoiceDialogue.ini) ---

	struct Settings
	{
		bool		autoListen = true;		// ListenMode=auto (default) or push
		int			pushToTalkKey = VK_RCONTROL;
		int			toggleKey = VK_F10;		// switches auto / push in game
		std::string	helperExe = "helper\\vdm_helper.exe";
		std::string	model = "model";
		std::string	missMessage = "Say that again?";
		std::string	missSound;		// sound form editor ID, empty for none
		std::string	hmMale = "sounds\\hm_male.wav";
		std::string	hmFemale = "sounds\\hm_female.wav";
		std::string	hmRobotMale = "sounds\\hm_robot_male.wav";
		std::string	hmRobotFemale = "sounds\\hm_robot_female.wav";
		bool		showHeard = false;
		bool		hmSound = true;			// play the NPC's "hm?" on a miss
		bool		showMissMessage = true;		// show MissMessage on a miss
		int			clickDelayMs = 250;		// highlight the chosen line this long before clicking it
	} g_settings;

	std::string g_modDir;		// ...\Data\NVSE\Plugins\VoiceDialogue
	std::string g_mcmIni;		// ...\Data\config\SpeakUpCourier\settings.ini, written by MCM Extender
	FILETIME g_mcmIniTime = {};
	DWORD g_mcmCheckAt = 0;
	HelperProcess g_helper;
	NVSEConsoleInterface *g_console = nullptr;

	std::vector<std::string> g_sentOptions;	// what the helper currently has
	std::vector<Tile *> g_optionTiles;		// matching tiles, same order
	void *g_sentLine = nullptr;				// NPC line (topic info) the options belong to
	UInt32 g_optionsVersion = 0;			// bumps whenever the on-screen list changes
	UInt32 g_clickedVersion = (UInt32)-1;	// options version we already answered
	bool g_listening = false;
	bool g_keyWasDown = false;
	bool g_toggleWasDown = false;
	int g_pendingIndex = -1;				// chosen line, highlighted and waiting to be clicked
	DWORD g_pendingAt = 0;

	std::string ReadIni(const char *key, const std::string &fallback, const std::string &path, const char *section = "General")
	{
		char buf[512];
		GetPrivateProfileStringA(section, key, fallback.c_str(), buf, sizeof(buf), path.c_str());
		return buf;
	}

	void LoadSettings()
	{
		std::string ini = g_modDir + "\\VoiceDialogue.ini";
		g_settings.autoListen = _stricmp(ReadIni("ListenMode", "auto", ini).c_str(), "push") != 0;
		std::string key = ReadIni("PushToTalkKey", "0xA3", ini);
		g_settings.pushToTalkKey = (int)strtol(key.c_str(), nullptr, 0);
		g_settings.toggleKey = (int)strtol(ReadIni("ToggleKey", "0x79", ini).c_str(), nullptr, 0);
		g_settings.hmMale = ReadIni("HmMale", g_settings.hmMale, ini);
		g_settings.hmFemale = ReadIni("HmFemale", g_settings.hmFemale, ini);
		g_settings.hmRobotMale = ReadIni("HmRobotMale", g_settings.hmRobotMale, ini);
		g_settings.hmRobotFemale = ReadIni("HmRobotFemale", g_settings.hmRobotFemale, ini);
		g_settings.helperExe = ReadIni("HelperExe", g_settings.helperExe, ini);
		g_settings.model = ReadIni("Model", g_settings.model, ini);
		g_settings.missMessage = ReadIni("MissMessage", g_settings.missMessage, ini);
		g_settings.missSound = ReadIni("MissSound", "", ini);
		g_settings.clickDelayMs = GetPrivateProfileIntA("General", "ClickDelayMs", 250, ini.c_str());
		g_settings.showHeard = GetPrivateProfileIntA("General", "ShowHeard", 0, ini.c_str()) != 0;
	}

	// MCM keybinds are DirectInput scan codes (256+ = mouse buttons); we poll Windows virtual keys.
	int ScanCodeToVirtualKey(int dik)
	{
		static const int mouse[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
		if (dik >= 256)
			return dik - 256 < 5 ? mouse[dik - 256] : 0;
		UINT scan = (dik & 0x80) ? (0xE000 | (dik & 0x7F)) : (UINT)dik;	// 0x80 bit = extended key
		return (int)MapVirtualKeyA(scan, MAPVK_VSC_TO_VK_EX);
	}

	// MCM Extender writes numbers as "1" or "250.0000", so read them as text.
	bool ReadMcmNumber(const char *key, double &value)
	{
		char buf[64];
		if (!GetPrivateProfileStringA("General", key, "", buf, sizeof(buf), g_mcmIni.c_str()) || !*buf)
			return false;
		value = atof(buf);
		return true;
	}

	// Settings from the MCM menu override VoiceDialogue.ini.
	void LoadMcmSettings()
	{
		double v;
		if (ReadMcmNumber("iListenMode", v)) g_settings.autoListen = v == 0;
		if (ReadMcmNumber("iPushToTalkKey", v) && ScanCodeToVirtualKey((int)v)) g_settings.pushToTalkKey = ScanCodeToVirtualKey((int)v);
		if (ReadMcmNumber("iToggleKey", v) && ScanCodeToVirtualKey((int)v)) g_settings.toggleKey = ScanCodeToVirtualKey((int)v);
		if (ReadMcmNumber("iClickDelayMs", v)) g_settings.clickDelayMs = (int)v;
		if (ReadMcmNumber("bHmSound", v)) g_settings.hmSound = v != 0;
		if (ReadMcmNumber("bMissMessage", v)) g_settings.showMissMessage = v != 0;
		if (ReadMcmNumber("bShowHeard", v)) g_settings.showHeard = v != 0;
	}

	// MCM Extender saves its ini when the pause menu closes; pick changes up within a second.
	bool McmIniChanged()
	{
		WIN32_FILE_ATTRIBUTE_DATA attr;
		if (!GetFileAttributesExA(g_mcmIni.c_str(), GetFileExInfoStandard, &attr))
			return false;
		if (CompareFileTime(&attr.ftLastWriteTime, &g_mcmIniTime) == 0)
			return false;
		g_mcmIniTime = attr.ftLastWriteTime;
		return true;
	}

	void StartHelper()
	{
		std::string exe = g_modDir + "\\" + g_settings.helperExe;
		std::string args = "--model \"" + g_modDir + "\\" + g_settings.model + "\"";
		if (g_helper.Start(exe, args, g_modDir))
			_MESSAGE("helper started: %s", exe.c_str());
		else
			_ERROR("could not start helper %s (error %lu)", exe.c_str(), GetLastError());
	}

	void ReadOptions(Menu *menu, std::vector<std::string> &texts, std::vector<Tile *> &tiles)
	{
		TopicListBox *topics = GetTopicList(menu);
		for (auto node = &topics->list.m_listHead; node; node = node->next)
		{
			TopicListItem *item = node->data;
			if (!item || !item->tile || item->isFiltered)
				continue;
			Tile::Value *str = item->tile->GetValue(Tile::kTileValue_string);
			if (!str || !str->str || !*str->str)
				continue;
			texts.push_back(json::FromGameText(str->str));
			tiles.push_back(item->tile);
		}
	}

	void ClickOption(Menu *menu, size_t index)
	{
		if (index >= g_optionTiles.size())
			return;
		Tile *tile = g_optionTiles[index];
		Tile::Value *id = tile->GetValue(Tile::kTileValue_id);
		// Same call the game makes for a mouse click on that topic.
		menu->HandleClick(id ? (UInt32)(SInt32)id->num : (UInt32)-1, tile);
	}

	// The NPC's "hm?": a file for its voice type from [Sounds] if listed, else male/female.
	void PlayHm(Menu *menu)
	{
		TESObjectREFR *speaker = GetSpeaker(menu);
		TESForm *base = speaker ? speaker->baseForm : nullptr;
		if (!base || (base->typeID != kFormType_TESNPC && base->typeID != kFormType_TESCreature))
			return;
		TESActorBaseData &data = ((TESActorBase *)base)->baseData;
		if (!g_settings.hmSound)
			return;
		std::string file;
		if (const char *voice = VoiceTypeName(data.voiceType))
		{
			// The game's own sound for this voice (ED-E's beep, Rex's whine), played from
			// the game's archives at the speaker, so no game files are shipped.
			std::string form = ReadIni(voice, "", g_modDir + "\\VoiceDialogue.ini", "SoundForms");
			if (!form.empty() && g_console)
			{
				_MESSAGE("miss: voice type %s, playing game sound %s", voice, form.c_str());
				g_console->RunScriptLine2(("PlaySound3D " + form).c_str(), speaker, true);
				return;
			}
			file = ReadIni(voice, "", g_modDir + "\\VoiceDialogue.ini", "Sounds");
		}
		if (file.empty())
		{
			// Robot voices (Mr. Handy, Protectron, ...) get the robotic "hm?".
			std::string voice = VoiceTypeName(data.voiceType) ? VoiceTypeName(data.voiceType) : "";
			for (char &c : voice) c = (char)tolower((unsigned char)c);
			bool robot = voice.find("robot") != std::string::npos;
			if (robot)
				file = data.IsFemale() ? g_settings.hmRobotFemale : g_settings.hmRobotMale;
			else
				file = data.IsFemale() ? g_settings.hmFemale : g_settings.hmMale;
		}
		if (file == "none")
			return;
		_MESSAGE("miss: voice type %s, playing %s", VoiceTypeName(data.voiceType) ? VoiceTypeName(data.voiceType) : "?", file.c_str());
		std::string path = g_modDir + "\\" + file;
		if (!PlaySoundA(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
			_ERROR("could not play %s", path.c_str());
	}

	void ShowMiss(const std::string &heard)
	{
		if (!g_settings.showMissMessage)
			return;
		std::string msg = g_settings.missMessage;
		if (g_settings.showHeard && !heard.empty())
			msg += " (heard \"" + heard + "\")";
		QueueUIMessage(msg.c_str(), 0, nullptr,
			g_settings.missSound.empty() ? nullptr : g_settings.missSound.c_str(), 2.0f, false);
	}

	bool KeyDown(int key)
	{
		if (!(GetAsyncKeyState(key) & 0x8000))
			return false;
		// Only while the game window has focus.
		DWORD pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &pid);
		return pid == GetCurrentProcessId();
	}

	void HandleHelperLine(const std::string &line, Menu *menu)
	{
		std::string event = json::GetString(line, "event");
		if (event != "options_set" && event != "listening")
			_MESSAGE("helper: %s", line.c_str());
		// Ignore stale answers: the menu closed or moved on while the player was talking,
		// or we already picked an option from this list.
		bool current = menu && (UInt32)json::GetInt(line, "v", -1) == g_optionsVersion
			&& g_clickedVersion != g_optionsVersion;
		if (event == "result")
		{
			int index = json::GetInt(line, "index", -1);
			if (current && index >= 0 && (size_t)index < g_optionTiles.size())
			{
				g_clickedVersion = g_optionsVersion;
				// Light the line up the way the mouse does, then click it a moment later,
				// so it looks like the player picked it.
				// ListBox::SetSelectedTile (vtable slot 0, JIP LN) is what keyboard and
				// controller navigation use, so the menu draws its own highlight box.
				TopicListBox *topics = GetTopicList(menu);
				ThisStdCall<bool>((*(UInt32 **)topics)[0], topics, g_optionTiles[index]);
				g_pendingIndex = index;
				g_pendingAt = GetTickCount() + g_settings.clickDelayMs;
			}
		}
		else if (event == "no_match")
		{
			if (current)
			{
				PlayHm(menu);
				ShowMiss(json::GetString(line, "heard"));
			}
		}
		else if (event == "error")
			_ERROR("helper: %s", json::GetString(line, "message").c_str());
		else if (event == "ready")
			_MESSAGE("helper ready");
	}

	void Tick()
	{
		if (!g_helper.IsRunning())
			return;
		Menu *menu = GetDialogMenu();

		if ((int)(GetTickCount() - g_mcmCheckAt) >= 0)
		{
			g_mcmCheckAt = GetTickCount() + 1000;
			if (McmIniChanged())
			{
				bool wasAuto = g_settings.autoListen;
				LoadSettings();
				LoadMcmSettings();
				if (g_listening && wasAuto != g_settings.autoListen)
				{
					g_helper.Send(wasAuto ? "{\"cmd\":\"auto_off\"}" : "{\"cmd\":\"cancel\"}");
					g_listening = false;
				}
				_MESSAGE("MCM settings loaded: %s, push key 0x%X, toggle key 0x%X",
					g_settings.autoListen ? "auto listen" : "push-to-talk", g_settings.pushToTalkKey, g_settings.toggleKey);
			}
		}

		bool toggle = KeyDown(g_settings.toggleKey);
		if (toggle && !g_toggleWasDown)
		{
			if (g_listening)
				g_helper.Send(g_settings.autoListen ? "{\"cmd\":\"auto_off\"}" : "{\"cmd\":\"cancel\"}");
			g_listening = false;
			g_settings.autoListen = !g_settings.autoListen;
			WritePrivateProfileStringA("General", "ListenMode", g_settings.autoListen ? "auto" : "push",
				(g_modDir + "\\VoiceDialogue.ini").c_str());
			// Keep the MCM page in step. Only once the player has used the MCM, so we don't
			// create the file and its folder for people who never open it.
			if (GetFileAttributesA(g_mcmIni.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				WritePrivateProfileStringA("General", "iListenMode", g_settings.autoListen ? "0" : "1", g_mcmIni.c_str());
				McmIniChanged();	// our own write, don't reload
			}
			QueueUIMessage(g_settings.autoListen ? "Speak Up Courier: auto listen in conversations" : "Speak Up Courier: push-to-talk",
				0, nullptr, nullptr, 2.0f, false);
		}
		g_toggleWasDown = toggle;

		if (!menu)
		{
			if (g_listening)
				g_helper.Send(g_settings.autoListen ? "{\"cmd\":\"auto_off\"}" : "{\"cmd\":\"cancel\"}");
			g_listening = false;
			if (!g_sentOptions.empty())
			{
				g_sentOptions.clear();
				g_optionTiles.clear();
				g_sentLine = nullptr;
				g_pendingIndex = -1;
				g_optionsVersion++;
			}
		}
		else
		{
			std::vector<std::string> texts;
			std::vector<Tile *> tiles;
			ReadOptions(menu, texts, tiles);
			// The NPC's current line counts too: Doc's personality test asks several
			// questions in a row with the very same answers.
			void *line = *(void **)((UInt8 *)menu + 0x78);	// DialogMenu::info078 (JIP LN)
			if (texts != g_sentOptions || line != g_sentLine)
			{
				g_sentOptions = std::move(texts);
				g_sentLine = line;
				g_optionsVersion++;
				g_helper.Send(json::SetOptions(g_sentOptions, g_optionsVersion));
				_MESSAGE("options v%u: %u lines", g_optionsVersion, (UInt32)g_sentOptions.size());
			}
			g_optionTiles = std::move(tiles);	// tiles can be rebuilt with the same text

			if (g_pendingIndex >= 0 && (int)(GetTickCount() - g_pendingAt) >= 0)
			{
				// Only if the list is still the one the player answered.
				if (g_clickedVersion == g_optionsVersion)
				{
					_MESSAGE("click option %d (list v%u)", g_pendingIndex, g_optionsVersion);
					ClickOption(menu, (size_t)g_pendingIndex);
					// Treat whatever is on screen next as a new list, even if it reads the
					// same (Doc's test reuses the same four answers question after question).
					g_sentOptions.clear();
				}
				g_pendingIndex = -1;
			}

			if (g_settings.autoListen)
			{
				// Listen while there is something to answer; pause between lists.
				bool want = !g_sentOptions.empty();
				if (want != g_listening)
				{
					g_helper.Send(want ? "{\"cmd\":\"auto_on\"}" : "{\"cmd\":\"auto_off\"}");
					g_listening = want;
				}
			}

			bool down = !g_settings.autoListen && KeyDown(g_settings.pushToTalkKey);
			if (down && !g_keyWasDown && !g_sentOptions.empty())
			{
				g_helper.Send("{\"cmd\":\"start\"}");
				g_listening = true;
			}
			else if (!down && g_keyWasDown && g_listening)
			{
				g_helper.Send("{\"cmd\":\"stop\"}");
				g_listening = false;
			}
			g_keyWasDown = down;
		}

		std::string line;
		while (g_helper.Poll(line))
			HandleHelperLine(line, menu);
	}

	void MessageHandler(NVSEMessagingInterface::Message *msg)
	{
		switch (msg->type)
		{
		case NVSEMessagingInterface::kMessage_DeferredInit:
			StartHelper();
			break;
		case NVSEMessagingInterface::kMessage_MainGameLoop:
			Tick();
			break;
		case NVSEMessagingInterface::kMessage_ExitGame:
		case NVSEMessagingInterface::kMessage_ExitGame_Console:
			g_helper.Stop();
			break;
		}
	}

	std::string ModDirFromDll()
	{
		HMODULE self = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&ModDirFromDll, &self);
		char path[MAX_PATH];
		GetModuleFileNameA(self, path, MAX_PATH);
		std::string dir = path;
		return dir.substr(0, dir.find_last_of("\\/")) + "\\VoiceDialogue";
	}
}

extern "C"
{
	bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info)
	{
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "VoiceDialogue";
		info->version = 3;

		if (nvse->isEditor)
			return false;
		if (nvse->runtimeVersion < RUNTIME_VERSION_1_4_0_525)
		{
			_ERROR("unsupported game version %08X", nvse->runtimeVersion);
			return false;
		}
		return true;
	}

	bool NVSEPlugin_Load(NVSEInterface *nvse)
	{
		g_modDir = ModDirFromDll();
		// VoiceDialogue folder -> Plugins -> NVSE -> Data
		std::string data = g_modDir.substr(0, g_modDir.rfind('\\'));
		data = data.substr(0, data.rfind('\\'));
		data = data.substr(0, data.rfind('\\'));
		g_mcmIni = data + "\\config\\SpeakUpCourier\\settings.ini";
		LoadSettings();
		if (McmIniChanged())
			LoadMcmSettings();
		g_console = (NVSEConsoleInterface *)nvse->QueryInterface(kInterface_Console);
		auto messaging = (NVSEMessagingInterface *)nvse->QueryInterface(kInterface_Messaging);
		messaging->RegisterListener(nvse->GetPluginHandle(), "NVSE", MessageHandler);
		_MESSAGE("loaded, mod folder %s, %s", g_modDir.c_str(), g_settings.autoListen ? "auto listen" : "push-to-talk");
		return true;
	}
}

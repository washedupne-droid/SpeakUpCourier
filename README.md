# Speak Up Courier

Speak your dialogue choices out loud in **Fallout: New Vegas**. When dialogue options are on screen, say the line you want, and the mod highlights and selects it.

Nexus Mods page: https://www.nexusmods.com/newvegas/mods/<MOD ID>

This repository holds the full source of the two compiled parts of the mod:

| Part | What it is | Source |
|---|---|---|
| `voice_dialogue.dll` | xNVSE plugin (C++, 32-bit). It reads the options from the dialogue menu, starts the speech helper, and selects the option the helper matched. | [`nvse-plugin/voice_dialogue`](nvse-plugin/voice_dialogue) |
| `vdm_helper.exe` | Speech helper (Python, 64-bit, packed with PyInstaller). It listens to the microphone with the offline [Vosk](https://alphacephei.com/vosk/) engine and fuzzy-matches what you said against the options. | [`speech-core`](speech-core) |

The helper is a separate process because New Vegas is 32-bit and Vosk only ships 64-bit builds. The plugin starts it hidden, talks to it over stdin/stdout pipes (one JSON object per line), and puts it in a Windows job object, so it closes when the game exits or crashes. The helper makes no network connections. Speech recognition runs entirely on the player's PC.

## Building

### Plugin (`voice_dialogue.dll`)
1. Install Visual Studio 2026 (or 2022) with the **Desktop development with C++** workload.
2. Clone xNVSE: `git clone https://github.com/xNVSE/NVSE.git xNVSE`
3. Copy `nvse-plugin/voice_dialogue` into the `xNVSE` folder, next to `nvse_plugin_example`.
4. Open `xNVSE/voice_dialogue/voice_dialogue.sln`, choose **Release | Win32**, and build. You can also run `build_plugin.bat`.
5. Output: `xNVSE/voice_dialogue/Release/voice_dialogue.dll`

### Speech helper (`vdm_helper.exe`)
1. Install 64-bit Python 3.11.
2. In `speech-core`, run `pip install -r requirements.txt`.
3. Run `build_helper.bat`. It runs:
   ```
   python -m PyInstaller --noconfirm --clean --noupx --onedir --collect-all vosk --name vdm_helper vdm_helper.py
   ```
4. Output: `speech-core/dist/vdm_helper/` (the exe plus its `_internal` folder)

Tests for the matcher: `python -m unittest discover -s tests` (run in `speech-core`).

### Packaging the mod
```
Data/NVSE/Plugins/voice_dialogue.dll
Data/NVSE/Plugins/VoiceDialogue/VoiceDialogue.ini     <- nvse-plugin/game-files/
Data/NVSE/Plugins/VoiceDialogue/helper/               <- speech-core/dist/vdm_helper/*
Data/NVSE/Plugins/VoiceDialogue/model/                <- vosk-model-small-en-us-0.15 contents
Data/NVSE/Plugins/VoiceDialogue/sounds/               <- "hm?" .wav files
Data/MCM/SpeakUpCourier.json                          <- nvse-plugin/game-files/MCM/
```
Get the speech model from https://alphacephei.com/vosk/models (`vosk-model-small-en-us-0.15`, Apache 2.0).

## Credits
- Vosk and the US English model by Alpha Cephei Inc. (Apache 2.0)
- xNVSE by the xNVSE team. Dialogue menu layout research from JIP LN NVSE by jazzisparis.
- See [THIRD-PARTY-LICENSES.txt](THIRD-PARTY-LICENSES.txt) for all bundled libraries.

By Jonin1433.

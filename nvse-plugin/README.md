# Speak Up Courier: New Vegas plugin

This is an xNVSE plugin. While a dialogue menu is open, it reads the topic lines on screen and sends them to the speech helper. When you release the push-to-talk key, it clicks the option the helper matched.

## What it does
- It starts the hidden speech helper when the game reaches the main menu. Windows closes the helper automatically if the game exits or crashes.
- It reads options straight from the dialogue menu, so it works with any mod's dialogue.
- By default it listens the whole time options are on screen: just say one and it gets picked when you pause. Set `ListenMode=push` in the ini to only listen while **Right Ctrl** (configurable) is held.
- On a miss it shows "Say that again?" and you can still click as normal. The NPC also gives a short "hm?" that matches their voice type.
- It ignores a late answer if the options changed while you were talking.

## Build (once)
1. Install **Visual Studio 2026 Community** with the **Desktop development with C++** workload. xNVSE's projects use the v145 toolset that comes with VS 2026. VS 2022 also works if you right-click the project and choose **Retarget Projects** to v143.
2. Get the xNVSE source with `git clone https://github.com/xNVSE/NVSE.git D:\GameDev\xNVSE`, or use GitHub's "Download ZIP" and unzip it to `D:\GameDev\xNVSE`.
3. Copy the `voice_dialogue` folder into `D:\GameDev\xNVSE\`, next to `nvse_plugin_example`.
4. Open `D:\GameDev\xNVSE\voice_dialogue\voice_dialogue.sln`, pick **Release | Win32** at the top, and use **Build > Build Solution**.
   The output is `voice_dialogue\Release\voice_dialogue.dll`.
5. Build the speech helper by double-clicking `speech-core\build_helper.bat`. The output is `speech-core\dist\vdm_helper\`.

## Install into the game
You need xNVSE installed already. Put the files in your Fallout New Vegas folder like this:

```
Data\NVSE\Plugins\voice_dialogue.dll
Data\NVSE\Plugins\VoiceDialogue\VoiceDialogue.ini        (from game-files\)
Data\NVSE\Plugins\VoiceDialogue\helper\vdm_helper.exe    (everything in dist\vdm_helper\)
Data\NVSE\Plugins\VoiceDialogue\model\...                (everything inside vosk-model-small-en-us-0.15\)
```

## Test
1. Launch through xNVSE, load a save and talk to anyone.
2. Read an option out loud. In push mode, hold **Right Ctrl** while you talk.
3. If nothing happens, check `VoiceDialogue.log` in the game folder. It says whether the helper started and lists any errors.

## Settings (`VoiceDialogue.ini`)
`ListenMode` (`auto` or `push`), `PushToTalkKey` (Windows key code, default `0xA3` = Right Ctrl), `MissMessage`, `MissSound` (a sound editor ID), and `ShowHeard` (adds what it heard to the miss message, which is handy while testing).

## Auto listening and speakers
With headphones, auto mode only hears you. With speakers, the mic can pick up the NPC's voice. It only clicks a line that closely matches an option, so this is unlikely, but use push mode if it happens.

## Credits
xNVSE (headers and plugin template) by the xNVSE team. Dialogue menu layout from JIP LN NVSE by jazzisparis.

# Speak Up Courier: speech core

This is a small background helper that listens to the mic while push-to-talk is held. It works out which dialogue option the player said and tells the game plugin.
It doesn't depend on any game, so every adapter (NVSE, FOSE, F4SE, SKSE, MWSE) talks to it the same way.

## Files
- `vdm_helper.py`: the helper process (mic, Vosk, and the JSON protocol)
- `matcher.py`: cleans option text (`[Speech 50]`, `(Lie)`) and picks the best match
- `tests/test_matcher.py`: matching tests (`python -m unittest discover -s tests`)

## Try it on your PC (Windows, 64-bit Python 3.10 or newer)
1. Install the requirements with `pip install -r requirements.txt`.
2. Download `vosk-model-small-en-us-0.15.zip` from https://alphacephei.com/vosk/models and unzip it next to this file.
3. Run `python vdm_helper.py --model vosk-model-small-en-us-0.15`, then type these lines, pressing Enter after each:
   ```
   {"cmd":"set_options","options":["Who are you?","[Speech 25] I'd rather not say.","Goodbye."]}
   {"cmd":"start"}
   ```
   Say a line, then type `{"cmd":"stop"}`. You should get a `result` back.

## Protocol (one JSON object per line)
The game sends commands on stdin:

| cmd | fields | meaning |
|---|---|---|
| `set_options` | `options: [str]`, `v: int` | the dialogue lines currently on screen, as raw text; `v` is echoed on answers |
| `start` | | push-to-talk pressed |
| `stop` | | push-to-talk released, so recognize and reply |
| `cancel` | | menu closed while listening |
| `auto_on` | | listen continuously, answer at every pause (no start/stop needed) |
| `auto_off` | | stop continuous listening |
| `ping` | | health check |
| `quit` | | shut down |

The helper replies on stdout:

| event | fields | game should |
|---|---|---|
| `ready` | | start sending commands |
| `listening` | | show a mic icon |
| `result` | `index`, `option`, `heard`, `score`, `v` | select option `index` (0-based), if `v` is still current |
| `no_match` | `heard`, `reason`, `v` | play the NPC "hm?" sound and show "say again" (clicking still works) |
| `error` | `message` | log it |

What players can say:
- the line itself, or just its first few words, even if a word or two is misheard
- a tag word on its own, like "speech", "barter" or "lie", when only one option has it

## Shipping it
- Build the exe with `build_helper.bat`. It is a console program, but the plugin starts it with `CREATE_NO_WINDOW`, so no window ever appears. Do not use `--noconsole`, because that breaks the stdin/stdout pipes.
- The helper has to be 64-bit, because Vosk only ships `win_amd64` builds. New Vegas is 32-bit, which is why this runs as a separate process.

## Credits and licenses
- Vosk speech recognition, Apache 2.0, by Alpha Cephei: https://github.com/alphacep/vosk-api
- vosk-model-small-en-us-0.15 is listed as Apache 2.0 on the Vosk models page.
- RapidFuzz, MIT. sounddevice, MIT (bundles PortAudio, MIT).

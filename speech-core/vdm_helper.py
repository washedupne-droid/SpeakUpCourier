"""Voice Dialogue Mod background helper.

The game plugin starts this as a hidden process and talks to it with one JSON
object per line over stdin/stdout (see README.md for the protocol).

    vdm_helper.py --model PATH                     # normal mode, used by the plugin
    vdm_helper.py --model PATH --wav F --options "A|B|C"   # one-off test on a recording
"""

import argparse
import json
import queue
import sys
import threading
import wave

import matcher

SAMPLE_RATE = 16000


_emit_lock = threading.Lock()


def emit(**event):
    # Auto mode reports from the audio thread, so writes need a lock.
    with _emit_lock:
        sys.stdout.write(json.dumps(event) + "\n")
        sys.stdout.flush()


class Recognizer:
    def __init__(self, model_path, use_grammar=True):
        import vosk

        vosk.SetLogLevel(-1)
        self.vosk = vosk
        self.model = vosk.Model(model_path)
        self.use_grammar = use_grammar

    def new_session(self, options):
        if self.use_grammar and options:
            grammar = json.dumps(matcher.grammar_for(options))
            return self.vosk.KaldiRecognizer(self.model, SAMPLE_RATE, grammar)
        return self.vosk.KaldiRecognizer(self.model, SAMPLE_RATE)

    @staticmethod
    def text_of(rec, final=True):
        raw = rec.FinalResult() if final else rec.Result()
        return json.loads(raw).get("text", "").replace("[unk]", "").strip()


# In auto mode, ignore utterances shorter than this when nothing matched, so
# coughs and stray words don't pop up "say that again" every few seconds.
AUTO_MISS_MIN_WORDS = 2


class Helper:
    """Either push-to-talk (start/stop around the player's speech) or auto mode
    (listen the whole time the options are on screen, answer at each pause)."""

    def __init__(self, recognizer, device=None):
        self.rec = recognizer
        self.device = device
        self.options = []
        self.version = 0  # echoed back so the game can drop answers for old options
        self.auto = False
        self.session = None
        self.audio = queue.Queue()
        self.stream = None
        self.lock = threading.Lock()
        threading.Thread(target=self._feed, daemon=True).start()

    def _open_stream(self):
        import sounddevice as sd

        def callback(indata, frames, time_info, status):
            self.audio.put(bytes(indata))

        self.stream = sd.RawInputStream(
            samplerate=SAMPLE_RATE, blocksize=4000, dtype="int16",
            channels=1, device=self.device, callback=callback,
        )

    def _feed(self):
        while True:
            chunk = self.audio.get()
            with self.lock:
                if self.session is None:
                    continue
                # True means Vosk heard a pause, i.e. the end of a sentence.
                if self.session.AcceptWaveform(chunk) and self.auto:
                    self.report(self.rec.text_of(self.session, final=False), auto=True)

    def handle(self, msg):
        cmd = msg.get("cmd")
        if cmd == "ping":
            emit(event="pong")
        elif cmd == "set_options":
            with self.lock:
                self.options = list(msg.get("options", []))
                self.version = msg.get("v", 0)
                if self.auto:
                    self.session = self.rec.new_session(self.options)
            emit(event="options_set", count=len(self.options))
        elif cmd in ("start", "auto_on"):
            if self.stream is None:
                self._open_stream()
            with self.lock:
                self.auto = cmd == "auto_on"
                self.session = self.rec.new_session(self.options)
            if not self.stream.active:
                self.stream.start()
            emit(event="listening")
        elif cmd == "stop":
            if self.session is None:
                emit(event="error", message="stop without start")
                return
            self.stream.stop()
            with self.lock:
                while not self.audio.empty():
                    self.session.AcceptWaveform(self.audio.get_nowait())
                session, self.session = self.session, None
            self.report(self.rec.text_of(session))
        elif cmd in ("cancel", "auto_off"):
            if self.stream is not None:
                self.stream.stop()
            with self.lock:
                self.session = None
                self.auto = False
            emit(event="cancelled")
        elif cmd == "quit":
            return False
        else:
            emit(event="error", message=f"unknown cmd {cmd!r}")
        return True

    def report(self, heard, auto=False):
        m = matcher.match(heard, self.options)
        if m.index is None:
            if auto and len(m.heard.split()) < AUTO_MISS_MIN_WORDS:
                return
            emit(event="no_match", heard=m.heard, score=round(m.score, 1), reason=m.reason, v=self.version)
        else:
            emit(event="result", index=m.index, option=self.options[m.index],
                 heard=m.heard, score=round(m.score, 1), reason=m.reason, v=self.version)

    def run(self):
        emit(event="ready")
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                if self.handle(json.loads(line)) is False:
                    break
            except Exception as e:  # never die on one bad message; the game is waiting
                emit(event="error", message=str(e))


def transcribe_wav(recognizer, path, options):
    with wave.open(path, "rb") as wf:
        if wf.getnchannels() != 1 or wf.getframerate() != SAMPLE_RATE or wf.getsampwidth() != 2:
            raise SystemExit("WAV must be 16 kHz, mono, 16-bit")
        session = recognizer.new_session(options)
        while data := wf.readframes(4000):
            session.AcceptWaveform(data)
    return recognizer.text_of(session)


def main():
    # The game plugin sends UTF-8; Windows pipes default to the ANSI code page.
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="folder of an unpacked Vosk model")
    ap.add_argument("--device", default=None, help="microphone name or index (default: system mic)")
    ap.add_argument("--no-grammar", action="store_true", help="listen for any words, not just the options")
    ap.add_argument("--wav", help="test mode: recognize this file instead of the mic")
    ap.add_argument("--options", help='test mode: options separated by "|"')
    args = ap.parse_args()

    rec = Recognizer(args.model, use_grammar=not args.no_grammar)
    device = int(args.device) if args.device and args.device.isdigit() else args.device

    if args.wav:
        options = args.options.split("|") if args.options else []
        helper = Helper(rec)
        helper.options = options
        helper.report(transcribe_wav(rec, args.wav, options))
        return
    Helper(rec, device).run()


if __name__ == "__main__":
    main()

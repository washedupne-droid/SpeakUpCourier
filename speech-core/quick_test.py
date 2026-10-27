"""Mic test without the game: press Enter, say an option, press Enter again."""

import json
import os
import queue
import time

import sounddevice as sd

import matcher
from vdm_helper import SAMPLE_RATE, Recognizer

OPTIONS = [
    "Who are you?",
    "Where am I?",
    "[Speech 25] I'd rather not talk about myself right now.",
    "(Lie) I remember everything.",
    "Goodbye.",
]
MODEL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vosk-model-small-en-us-0.15")


def main():
    print("Loading speech model...")
    rec = Recognizer(MODEL)
    print("\nOptions on screen:")
    for i, o in enumerate(OPTIONS, 1):
        print(f"  {i}. {o}")
    print('\nTry reading a line, part of one, or "lie". Type q then Enter to quit.')

    audio = queue.Queue()
    stream = sd.RawInputStream(samplerate=SAMPLE_RATE, blocksize=4000, dtype="int16",
                               channels=1, callback=lambda d, f, t, s: audio.put(bytes(d)))
    while True:
        if input("\nPress Enter and speak... ").strip().lower() == "q":
            break
        session = rec.new_session(OPTIONS)
        stream.start()
        input("  listening, press Enter when done ")
        released = time.perf_counter()
        stream.stop()
        while not audio.empty():
            session.AcceptWaveform(audio.get_nowait())
        heard = rec.text_of(session)
        m = matcher.match(heard, OPTIONS)
        ms = (time.perf_counter() - released) * 1000
        print(f"  heard:  {heard!r}")
        if m.index is None:
            print(f"  result: NO MATCH ({m.reason}), the NPC would say 'hm?'")
        else:
            print(f"  result: {m.index + 1}. {OPTIONS[m.index]}  (score {m.score:.0f})")
        print(f"  time after you stopped: {ms:.0f} ms")


if __name__ == "__main__":
    main()

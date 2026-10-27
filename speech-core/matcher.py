"""Match what the player said against the dialogue options on screen.

Game-agnostic: every game adapter sends the raw option strings it sees and
gets back an index (or None when we aren't confident).
"""

import re
from dataclasses import dataclass, field

from rapidfuzz import fuzz

# Bracketed skill/perk tags such as "[Speech 50]", "[Succeeded]", "(Lie)", "<Persuade>".
_TAG_RE = re.compile(r"\[([^\]]*)\]|\(([^)]*)\)|<([^>]*)>")
_NON_WORD_RE = re.compile(r"[^a-z0-9' ]+")
_SPACE_RE = re.compile(r"\s+")


# A match must reach this score (0-100) and beat the runner-up by MARGIN.
MIN_SCORE = 70
MARGIN = 8


_ONES = "zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen".split()
_TENS = "_ _ twenty thirty forty fifty sixty seventy eighty ninety".split()


def number_words(n):
    """2500 -> "two thousand five hundred", the way Vosk writes numbers."""
    if n < 20:
        return _ONES[n]
    if n < 100:
        return _TENS[n // 10] + ("" if n % 10 == 0 else " " + _ONES[n % 10])
    if n < 1000:
        rest = n % 100
        return _ONES[n // 100] + " hundred" + ("" if rest == 0 else " " + number_words(rest))
    if n < 1000000:
        rest = n % 1000
        return number_words(n // 1000) + " thousand" + ("" if rest == 0 else " " + number_words(rest))
    return str(n)


_DIGITS_RE = re.compile(r"\d+")


def normalize(text):
    text = text.lower().replace("’", "'")
    text = _DIGITS_RE.sub(lambda m: " " + number_words(int(m.group())) + " ", text)
    text = _NON_WORD_RE.sub(" ", text)
    return _SPACE_RE.sub(" ", text).strip()


@dataclass
class Option:
    raw: str
    spoken: str  # the line with tags stripped, normalized
    tags: list = field(default_factory=list)  # e.g. ["speech", "lie"]


def parse_option(raw):
    tags = []
    for m in _TAG_RE.finditer(raw):
        inner = next(g for g in m.groups() if g is not None)
        # "[Speech 50]" -> "speech"; "[Succeeded]" carries no useful keyword.
        word = normalize(re.sub(r"\d+(\s*/\s*\d+)?", "", inner))
        if word and word not in ("succeeded", "failed"):
            tags.append(word)
    spoken = normalize(_TAG_RE.sub(" ", raw))
    return Option(raw=raw, spoken=spoken, tags=tags)


@dataclass
class Match:
    index: object  # int, or None when nothing was confident enough
    score: float
    heard: str
    reason: str


def _score(heard, spoken):
    if not spoken:
        return 0.0
    # Full line read out, or just the start of it ("i don't think so" for a long line).
    full = fuzz.token_sort_ratio(heard, spoken)
    prefix = spoken[: max(len(heard) + 4, 1)]
    start = fuzz.ratio(heard, prefix) if len(heard.split()) >= 3 else 0
    partial = fuzz.partial_ratio(heard, spoken) if len(heard) >= 12 else 0
    # Recognizers often split or join words ("good bye" vs "goodbye").
    squashed = fuzz.ratio(heard.replace(" ", ""), spoken.replace(" ", ""))
    return max(full, start, partial * 0.95, squashed)


def _pick_same_text(heard, options, indices):
    """Options with identical text: a tag word the player said decides, else the
    untagged one, else the first (the game often lists a line twice under
    different conditions). None when only tagged variants differ, like Lie/Truth."""
    words = set(heard.split())
    tagged = [i for i in indices if words & set(" ".join(options[i].tags).split())]
    if len(tagged) == 1:
        return tagged[0]
    plain = [i for i in indices if not options[i].tags]
    if plain:
        return plain[0]
    if len({tuple(options[i].tags) for i in indices}) == 1:
        return indices[0]
    return None


def match(heard_raw, raw_options):
    heard = normalize(heard_raw)
    options = [parse_option(o) for o in raw_options]
    if not heard or not options:
        return Match(None, 0, heard, "nothing heard")

    # The line read out exactly (Vosk's grammar makes this common) beats everything,
    # Options are only ever picked by what they say, never by position.
    exact = [i for i, o in enumerate(options) if o.spoken == heard]
    if exact:
        pick = _pick_same_text(heard, options, exact)
        return Match(pick, 100, heard, "exact" if pick is not None else "ambiguous")

    # A single tag word ("speech", "lie") picks the only option carrying it.
    tagged = [i for i, o in enumerate(options) if heard in o.tags]
    if len(tagged) == 1:
        return Match(tagged[0], 100, heard, "tag")

    # Just the opening words, and several lines open that way ("tell me about the...").
    starts = {o.spoken for o in options if o.spoken.startswith(heard + " ")}
    if len(starts) > 1:
        return Match(None, 0, heard, "ambiguous")

    scores = sorted(((_score(heard, o.spoken), i) for i, o in enumerate(options)), reverse=True)
    best, best_i = scores[0]
    runner_up = scores[1][0] if len(scores) > 1 else 0
    if best < MIN_SCORE:
        return Match(None, best, heard, "low score")
    if best - runner_up < MARGIN:
        # Lines that contain each other ("agree" / "strongly agree" / "strongly
        # disagree") tie on the loose scores, so break the tie on the exact text.
        close = [i for s, i in scores if best - s < MARGIN]
        # The same line offered twice, or as "<Lie> line" / "<Truth> line".
        if len({options[i].spoken for i in close}) == 1:
            pick = _pick_same_text(heard, options, close)
            if pick is not None:
                return Match(pick, best, heard, "text")
            return Match(None, best, heard, "ambiguous")
        strict = sorted(((fuzz.ratio(heard, options[i].spoken), i) for i in close), reverse=True)
        if strict[0][0] >= MIN_SCORE and strict[0][0] - strict[1][0] >= MARGIN:
            return Match(strict[0][1], best, heard, "text")
        return Match(None, best, heard, "ambiguous")
    return Match(best_i, best, heard, "text")


def grammar_for(raw_options):
    """Vocabulary for Vosk so it only listens for words that can matter right now."""
    # Whole lines bias Vosk toward reading an option out; single words still let
    # partial reads through.
    phrases = set()
    words = set()
    for o in map(parse_option, raw_options):
        if o.spoken:
            phrases.add(o.spoken)
        words.update(o.spoken.split())
        for t in o.tags:
            words.update(t.split())
    # "[unk]" lets Vosk say "something else" instead of forcing a wrong word.
    return sorted(phrases) + sorted(words) + ["[unk]"]

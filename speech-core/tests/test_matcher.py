import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import matcher  # noqa: E402

# Lines in the style of New Vegas dialogue menus.
DOC_MITCHELL = [
    "Who are you?",
    "Where am I?",
    "[Speech 25] I'd rather not talk about myself right now.",
    "(Lie) I remember everything.",
    "Goodbye.",
]
BENNY = [
    "[Speech 75] You're right. Let's put this behind us.",
    "[Barter 50] How much would it cost to forget this?",
    "I'm going to kill you.",
    "Why did you shoot me?",
]


class ParseTests(unittest.TestCase):
    def test_strips_tags(self):
        o = matcher.parse_option("[SPEECH 50/50] You don't want to do that.")
        self.assertEqual(o.spoken, "you don't want to do that")
        self.assertEqual(o.tags, ["speech"])

    def test_succeeded_is_not_a_tag(self):
        self.assertEqual(matcher.parse_option("[Succeeded] Fine.").tags, [])

    def test_curly_apostrophe(self):
        self.assertEqual(matcher.parse_option("I’m fine.").spoken, "i'm fine")


class MatchTests(unittest.TestCase):
    def assertPicks(self, heard, options, index):
        m = matcher.match(heard, options)
        self.assertEqual(m.index, index, f"{heard!r} -> {m}")

    def test_exact_reads(self):
        self.assertPicks("who are you", DOC_MITCHELL, 0)
        self.assertPicks("where am i", DOC_MITCHELL, 1)
        self.assertPicks("goodbye", DOC_MITCHELL, 4)

    def test_tag_not_spoken(self):
        self.assertPicks("i'd rather not talk about myself right now", DOC_MITCHELL, 2)
        self.assertPicks("i remember everything", DOC_MITCHELL, 3)

    def test_misheard_words(self):
        # Typical recognizer slips: dropped/merged/swapped words.
        self.assertPicks("i rather not talk about my self right now", DOC_MITCHELL, 2)
        self.assertPicks("how much would cost to forget this", BENNY, 1)
        self.assertPicks("why'd you shoot me", BENNY, 3)
        self.assertPicks("good bye", DOC_MITCHELL, 4)

    def test_start_of_long_line(self):
        self.assertPicks("you're right let's put", BENNY, 0)

    def test_position_never_picks(self):
        self.assertPicks("option two", DOC_MITCHELL, None)
        self.assertPicks("third", DOC_MITCHELL, None)
        self.assertPicks("last", DOC_MITCHELL, None)

    def test_tag_word(self):
        self.assertPicks("barter", BENNY, 1)
        self.assertPicks("lie", DOC_MITCHELL, 3)

    def test_rejects_unrelated_speech(self):
        self.assertPicks("pass me the nuka cola", BENNY, None)
        self.assertPicks("", BENNY, None)

    def test_ambiguous_is_rejected(self):
        opts = ["Tell me about the NCR.", "Tell me about the Legion."]
        self.assertPicks("tell me about the legion", opts, 1)
        self.assertIsNone(matcher.match("tell me about the", opts).index)


class GrammarTests(unittest.TestCase):
    def test_contains_words_and_unk(self):
        g = matcher.grammar_for(BENNY)
        self.assertIn("shoot", g)
        self.assertIn("barter", g)
        self.assertIn("why did you shoot me", g)
        self.assertEqual(g[-1], "[unk]")
        self.assertNotIn("75", g)


if __name__ == "__main__":
    unittest.main()


class LikertTest(unittest.TestCase):
    OPTIONS = ["Strongly agree.", "Agree.", "Disagree.", "Strongly disagree."]

    def test_each_line(self):
        for i, said in enumerate(["strongly agree", "agree", "disagree", "strongly disagree"]):
            self.assertEqual(matcher.match(said, self.OPTIONS).index, i, said)

    def test_small_mishear(self):
        self.assertEqual(matcher.match("strongly disagreed", self.OPTIONS).index, 3)



class GameDataTest(unittest.TestCase):
    """Cases found by scanning every player response set in FNV + DLC."""

    def test_spoken_numbers(self):
        opts = ["Just 1.", "I can spare 2.", "I can spare 4.", "Just take all of it."]
        self.assertEqual(matcher.match("i can spare two", opts).index, 1)
        self.assertEqual(matcher.match("i can spare four", opts).index, 2)

    def test_option_that_is_a_number_word(self):
        opts = ["One.", "Three.", "Five.", "Ten."]
        self.assertEqual(matcher.match("three", opts).index, 1)

    def test_lie_variant_needs_the_tag_word(self):
        opts = ["I did all I could to save him.", "<Lie> I did all I could to save him."]
        self.assertEqual(matcher.match("i did all i could to save him", opts).index, 0)
        self.assertEqual(matcher.match("lie i did all i could to save him", opts).index, 1)

    def test_shared_opening_is_ambiguous(self):
        opts = ["Tell me about the Fiends.", "Tell me about the Great Khans.", "Goodbye."]
        self.assertIsNone(matcher.match("tell me about the", opts).index)

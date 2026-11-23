"""Synthesis architecture follows the described sound, not a preset palette."""
import unittest
from scripts import dice_timbres


def voice(family, decay=40, cut=-10, res=0, tone=0, grit=0):
    return dict(character=dict(family=family, tone=tone, grit=grit), decay=decay, cut=cut, res=res)


class DiceTimbresTest(unittest.TestCase):
    def test_sound_families_have_explicit_distinct_architectures(self):
        sub = dice_timbres.build(voice("sub"))
        reese = dice_timbres.build(voice("reese"))
        guitar = dice_timbres.build(voice("guitar"))
        self.assertEqual((4, [4, 4]), (sub["type"], sub["osc"]))
        self.assertEqual([0, 0], reese["osc"])
        self.assertGreater(reese["params"][3], 64)
        self.assertEqual((8, 0), (guitar["type"], guitar["machine"]))

    def test_machine_and_decay_are_built_from_the_recipe(self):
        a = dice_timbres.build(voice("kick909", decay=16))
        b = dice_timbres.build(voice("kick909", decay=90))
        self.assertEqual((3, 9), (a["type"], a["machine"]))
        self.assertGreater(b["params"][0], a["params"][0])
        acid = dice_timbres.build(voice("acid"))
        self.assertGreater(acid["resonance"], 50)
        self.assertGreater(acid["fenv"], 20)

    def test_noise_accents_and_hats_have_distinct_bodies_and_decay(self):
        rim = dice_timbres.build(voice("noise", decay=127, tone=2))
        self.assertEqual((3, 13), (rim["type"], rim["machine"]))
        self.assertEqual(3, dice_timbres.build(voice("noise", cut=1))["filter"])
        self.assertLess(rim["params"][2], 10)  # RS9 stick noise
        for decay in (0, 64, 127):
            closed = dice_timbres.build(voice("hat", decay=decay))
            opened = dice_timbres.build(voice("openhat", decay=decay))
            self.assertLess(closed["params"][3], 32)  # HH BASIC's first decay range
            self.assertGreater(opened["params"][3], closed["params"][3])
        snare = dice_timbres.build(voice("snare", tone=2))
        self.assertEqual((2, 1), (snare["type"], snare["machine"]))
        self.assertLess(snare["params"][5], 40)  # SD BASIC feedback also feeds its noise burst

    def test_bright_tonal_fm_keeps_its_fundamental(self):
        for family in ("bell", "fmkeys"):
            tone = dice_timbres.build(voice(family, cut=1))
            self.assertEqual(1, tone["filter"])
        self.assertEqual(3, dice_timbres.build(voice("hat", cut=1))["filter"])

    def test_tone_and_grit_change_parameters_without_changing_family(self):
        soft = dice_timbres.build(voice("pad", tone=-2, grit=0))
        hard = dice_timbres.build(voice("pad", tone=2, grit=2))
        self.assertEqual(soft["osc"], hard["osc"])
        self.assertGreater(hard["cutoff"], soft["cutoff"])
        self.assertGreater(hard["drive"], soft["drive"])
        self.assertGreater(soft["env"][0], 0)

    def test_chord_samples_receive_polyphony_headroom(self):
        for family in ("stab","organ","epiano","fmkeys","pad","guitar"):
            single = voice(family)
            one = dice_timbres.build(single)
            single["character"]["chord"] = "triad"
            triad = dice_timbres.build(single)
            single["character"]["chord"] = "seventh"
            seventh = dice_timbres.build(single)
            self.assertLessEqual(triad["level"],one["level"]*0.6)
            self.assertLessEqual(seventh["level"],one["level"]*0.5)
            self.assertLess(seventh["level"],triad["level"])

    def test_default_stab_is_a_soft_short_chord_not_a_bright_key_lead(self):
        stab = dice_timbres.build(voice("stab",cut=0,res=100))
        self.assertIn(2,stab["osc"])
        self.assertLessEqual(stab["cutoff"],76)
        self.assertLessEqual(stab["fenv"],12)
        self.assertLessEqual(stab["resonance"],24)
        self.assertGreater(stab["env"][0],0)
        self.assertLessEqual(stab["env"][3],32)

    def test_out_of_range_controls_are_bounded_and_unknown_family_rejected(self):
        result = dice_timbres.build(voice("acid", decay=255, cut=-128, res=255, tone=9, grit=9))
        for key in ("cutoff", "resonance", "drive", "level"):
            self.assertTrue(0 <= result[key] <= 127)
        self.assertTrue(all(0 <= n <= 127 for n in result["params"] + result["env"]))
        with self.assertRaises(KeyError):
            dice_timbres.build(voice("unknown"))


if __name__ == "__main__":
    unittest.main()

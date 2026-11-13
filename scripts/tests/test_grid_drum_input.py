"""Exercise production GRID drum input with real A/B chords and captured lane drawing."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    depth = 0
    for end in range(source.index("{", start), len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise ValueError(signature)


class GridDrumInputTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        folder = Path(cls.tmp.name)
        (folder / "hw").mkdir()
        (folder / "Arduino.h").write_text("#pragma once\n#include <cstdint>\nusing TickType_t=uint32_t;\n")
        (folder / "hw/lgfx_config.h").write_text("""#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace lgfx { struct IFont {}; struct FixedBMPfont:IFont {
constexpr FixedBMPfont(const uint8_t*,const uint8_t*,int,int,int){} }; }
namespace fonts { inline const lgfx::IFont AsciiFont8x16{}; }
struct LGFX {};
struct LGFX_Sprite {
struct Rect {int x,y,w,h;uint16_t color;}; std::vector<Rect> rects;
struct Text {std::string value;int x,y;}; std::vector<Text> texts;
void fillRect(int x,int y,int w,int h,uint16_t c){rects.push_back({x,y,w,h,c});}
void drawRect(int x,int y,int w,int h,uint16_t c){rects.push_back({x,y,w,h,c});}
void drawFastHLine(int,int,int,uint16_t){} void drawFastVLine(int,int,int,uint16_t){}
void setTextColor(uint16_t){} void drawString(const char* t,int x,int y){texts.push_back({t,x,y});}
};
""")
        source = (ROOT / "src/ui/grid_screen.cpp").read_text()
        names = ["setEdit", "setRec", "setPerf", "onPatternChange", "snapCell", "cancelCell", "writeStep", "toggleLane", "setNote",
                 "previewNote", "editTurn", "onInput", "onATap", "onBack", "onTouch",
                 "enterDegree", "drawOverview", "drawMask", "drawOverviewFocus", "focusFieldSelect",
                 "drawInspector", "inspectorTouch"]
        names += [name for name in ("editLane", "finishLaneEdit") if "void GridScreen::" + name + "(" in source]
        methods = "\n".join(method(source, "void GridScreen::" + name + "(") for name in names)
        methods = method(source, "bool fieldText(") + "\n" + methods
        methods += "\n" + "\n".join(method(source, signature) for signature in (
            "bool GridScreen::hit(", "bool GridScreen::trackKey(", "int GridScreen::rowsFor(",
            "uint8_t GridScreen::degreeNote("))
        fixture = (ROOT / "scripts/tests/grid_drum_input_host.cpp").read_text().replace("/* METHODS */", methods)
        header = (ROOT / "src/ui/grid_screen.h").read_text()
        inline = "\n".join(method(header, signature) for signature in (
            "bool padShown(", "GridOverviewLayout overviewLayout(", "bool inspectorShown(", "int focusField("))
        fixture = fixture.replace("/* UI INLINE */", inline)
        (folder / "test.cpp").write_text(fixture)
        cls.binary = folder / "test"
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I" + str(folder), "-I" + str(ROOT / "src"),
                        "-I" + str(ROOT / "lib/core/src"), str(folder / "test.cpp"),
                        str(ROOT / "src/ui/theme.cpp"), str(ROOT / "lib/core/src/fx_info.cpp"),
                        str(ROOT / "lib/core/src/scale.cpp"), str(ROOT / "lib/core/src/automation.cpp"),
                        "-o", str(cls.binary)], check=True)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_hold_moves_one_added_hit_and_previews(self): self.run_case("hold")
    def test_a_tap_adds_last_selected_lane(self): self.run_case("tap")
    def test_a_tap_keeps_encoder_edit_open(self): self.run_case("edit")
    def test_b_cancels_only_the_current_lane_gesture(self): self.run_case("cancel")
    def test_lane_limits_and_no_change_gestures_preserve_undo(self): self.run_case("edges")
    def test_overview_marks_selected_lane_in_every_theme(self): self.run_case("render")
    def test_melodic_editing_is_preserved(self): self.run_case("melodic")
    def test_focus_only_expands_during_edit_in_both_banks(self): self.run_case("focus")
    def test_physical_track_buttons_keep_note_and_lane_input(self): self.run_case("buttons")
    def test_note_edit_uses_compact_inspector_without_mini_keyboard(self): self.run_case("footer")
    def test_focus_fields_and_six_fx_slots_share_one_cancel_snapshot(self): self.run_case("fields")
    def test_touch_headers_and_other_steps_follow_expanded_columns(self): self.run_case("touch")
    def test_all_columns_fit_and_hit_the_drawn_track_in_every_bank(self): self.run_case("bounds")
    def test_pattern_and_live_modes_reset_hidden_overview_fields(self): self.run_case("transitions")
    def test_shift_turn_selects_all_fields_without_writing_notes_or_fx(self): self.run_case("shift-fields")
    def test_shift_field_navigation_preserves_undo_and_a_shift_coarse_edit(self): self.run_case("shift-undo")
    def test_shift_field_navigation_on_drums_and_existing_detail_controls(self): self.run_case("shift-regression")


if __name__ == "__main__":
    unittest.main()

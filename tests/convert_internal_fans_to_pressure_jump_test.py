import tempfile
import unittest
import json
from pathlib import Path

from tools.convert_internal_fans_to_pressure_jump import convert
from tools.scale_pressure_jump_fans import scale_case


FAN_OPTIONS = """FoamFile { format ascii; }
options
{
internal_test_fan
{
 type fanMomentumSource;
 selectionMode cellZone;
 cellZone internal_test_fan;
 faceZone internal_test_fan_faces;
 flowDir (-1 0 0);
 thickness 0.01;
 fanCurve
 {
  type table;
  outOfBounds clamp;
  values
  (
   (0 50)
   (0.02 0)
  );
 }
}
heater { type scalarSemiImplicitSource; }
}
"""


class PressureJumpConversionTest(unittest.TestCase):
    def test_conversion_preserves_nonfan_options_and_backs_up_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            fluid = case / "constant" / "fluid"
            system = case / "system"
            fluid.mkdir(parents=True)
            system.mkdir()
            for name in ("fvOptions", "fvOptions.fullFan", "fvOptions.flowOnly"):
                (fluid / name).write_text(FAN_OPTIONS, encoding="utf-8")

            self.assertEqual(convert(case), ["internal_test_fan"])
            for name in ("fvOptions", "fvOptions.fullFan", "fvOptions.flowOnly"):
                converted = (fluid / name).read_text(encoding="utf-8")
                self.assertNotIn("fanMomentumSource", converted)
                self.assertIn("scalarSemiImplicitSource", converted)
                self.assertIn(
                    "fanMomentumSource",
                    (fluid / f"{name}.momentumSource").read_text(encoding="utf-8"),
                )
            baffles = (system / "fluid" / "createBafflesDict").read_text(
                           encoding="utf-8")
            self.assertIn("zoneName internal_test_fan_faces", baffles)
            self.assertIn("type fan;", baffles)
            self.assertIn("mode volumeFlowRate;", baffles)
            self.assertIn("(0 50)", baffles)
            self.assertIn("(0.02 0)", baffles)
            manifest = json.loads(
                (fluid / "pressureJumpFanCurves.json").read_text()
            )
            self.assertEqual(
                manifest["fans"]["internal_test_fan"],
                [[0.0, 50.0], [0.02, 0.0]],
            )
            self.assertTrue((case / "scale_pressure_jump_fans.py").is_file())
            apply_script = (case / "apply_internal_fan_pressure_jumps.sh").read_text(
                encoding="utf-8")
            self.assertIn("createBaffles", apply_script)
            self.assertIn("topoSetDict_fluid_interfaces", apply_script)
            self.assertLess(apply_script.index("createBaffles"),
                            apply_script.index("topoSetDict_fluid_interfaces"))

    def test_second_conversion_refuses_to_overwrite_backups(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            fluid = case / "constant" / "fluid"
            (case / "system").mkdir(parents=True)
            fluid.mkdir(parents=True)
            (fluid / "fvOptions.fullFan").write_text(
                FAN_OPTIONS, encoding="utf-8")
            convert(case)
            with self.assertRaises(ValueError):
                convert(case)

    def test_scaler_applies_absolute_scale_to_root_and_latest_rank_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            fluid = case / "constant" / "fluid"
            fluid.mkdir(parents=True)
            (fluid / "pressureJumpFanCurves.json").write_text(json.dumps({
                "fans": {"internal_test_fan": [[0.0, 50.0], [0.02, 0.0]]}
            }))
            field = """FoamFile { format ascii; }
internalField uniform 0;
boundaryField
{
 internal_test_fan_pressure_jump_master
 {
  type fan;
  jumpTable { type table; values ((0 50) (0.02 0)); }
 }
}
"""
            targets = [case / "0" / "fluid" / "p_rgh"]
            for rank in range(2):
                targets.append(case / f"processor{rank}" / "0.01" / "fluid" / "p_rgh")
            for target in targets:
                target.parent.mkdir(parents=True)
                target.write_text(field)

            files, fans = scale_case(case, 0.2)
            self.assertEqual((files, fans), (3, 3))
            for target in targets:
                self.assertIn("(0 10)", target.read_text())
                self.assertIn("values\n2\n(", target.read_text())
            scale_case(case, 0.8)
            for target in targets:
                text = target.read_text()
                self.assertIn("(0 40)", text)
                self.assertNotIn("(0 8)", text)


if __name__ == "__main__":
    unittest.main()

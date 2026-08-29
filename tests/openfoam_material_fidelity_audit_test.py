import tempfile
import unittest
import os
from pathlib import Path

from tools.openfoam_material_fidelity_audit import audit_model


class MaterialFidelityAuditTest(unittest.TestCase):
    def test_detects_only_differing_solid_materials(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "model.toml"
            path.write_text(
                '''
[[components]]
name = "device"
watts = 0
[components.material]
rho = 2700
cp = 900
k = 150
[[components.internal_regions]]
name = "matching"
state = "solid"
watts = 1
[components.internal_regions.material]
rho = 2700
cp = 900
k = 150
[components.internal_regions.size]
units = "mm"
width = 100
depth = 100
height = 100
[[components.internal_regions]]
name = "core"
state = "solid"
watts = 2
[components.internal_regions.material]
rho = 2330
cp = 700
k = 130
[components.internal_regions.size]
units = "mm"
width = 100
depth = 100
height = 100
[[components.internal_regions]]
name = "air"
state = "air"
''',
                encoding="utf-8",
            )
            rows = audit_model(path)
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["solid_regions"], 2)
            self.assertEqual(
                [region["name"] for region in rows[0]["differing_regions"]],
                ["core"],
            )
            core = rows[0]["differing_regions"][0]
            self.assertAlmostEqual(core["volume"], 0.001)
            self.assertAlmostEqual(core["mass_delta"], -0.37)
            self.assertAlmostEqual(core["capacity_delta"], -799.0)

    def test_resolves_material_files_and_preserves_duplicate_instances(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "outer.toml").write_text(
                "rho=2700\ncp=900\nk=150\n", encoding="utf-8")
            (root / "inner.toml").write_text(
                "rho=2200\ncp=800\nk=2\n", encoding="utf-8")
            (root / "component.toml").write_text(
                '''
name = "referenced device"
watts = 0
material = "outer.toml"
[[internal_regions]]
name = "battery"
state = "solid"
watts = 2
material = "inner.toml"
[internal_regions.size]
units = "inches"
width = 1
depth = 2
height = 3
''',
                encoding="utf-8",
            )
            (root / "model.toml").write_text(
                '[[components]]\ntemplate="component.toml"\n'
                '[[components]]\ntemplate="component.toml"\n',
                encoding="utf-8",
            )
            original = Path.cwd()
            try:
                os.chdir(root)
                rows = audit_model(Path("model.toml"))
            finally:
                os.chdir(original)
            self.assertEqual(len(rows), 2)
            self.assertEqual([row["instance"] for row in rows], [0, 1])
            self.assertTrue(all(len(row["differing_regions"]) == 1 for row in rows))
            expected_volume = 6 * 0.0254**3
            self.assertAlmostEqual(rows[0]["differing_regions"][0]["volume"],
                                   expected_volume)


if __name__ == "__main__":
    unittest.main()

import math
import tomllib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODEL_PATH = ROOT / "library/models/final_model.toml"
FAN_LIBRARY_PATH = ROOT / "library/fan_curves/fan_curves.toml"
PSEUDO_ELECTRONICS = "library/components/materials/mixed_electronics.toml"


def load_toml(path: Path) -> dict:
    return tomllib.loads(path.read_text(encoding="utf-8-sig"))


def heat_summary(node: object) -> tuple[float, int]:
    if isinstance(node, dict):
        own = float(node.get("watts", 0.0))
        total = own
        count = int(own != 0.0)
        for key, value in node.items():
            if key != "watts":
                child_total, child_count = heat_summary(value)
                total += child_total
                count += child_count
        return total, count
    if isinstance(node, list):
        total = 0.0
        count = 0
        for value in node:
            child_total, child_count = heat_summary(value)
            total += child_total
            count += child_count
        return total, count
    return 0.0, 0


class FinalModelContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.model = load_toml(MODEL_PATH)
        cls.curve_names = {
            curve["name"] for curve in load_toml(FAN_LIBRARY_PATH)["fan_curve"]
        }

    def resolve(self, relative: str) -> Path:
        path = ROOT / relative
        self.assertTrue(path.is_file(), f"missing referenced file: {relative}")
        return path

    def test_all_component_and_material_references_resolve(self) -> None:
        for component in self.model["components"]:
            if "template" not in component:
                material = component.get("material")
                self.assertIsInstance(material, str)
                self.resolve(material)
                continue
            component_path = self.resolve(component["template"])
            template = load_toml(component_path)
            self.assertIsInstance(template.get("material"), str)
            self.resolve(template["material"])
            for region in template.get("internal_regions", []):
                if region.get("state") != "solid":
                    continue
                self.assertIsInstance(
                    region.get("material"), str,
                    f"inline/missing material in {component_path.name}:"
                    f"{region.get('name')}",
                )
                self.resolve(region["material"])

    def test_fan_components_use_pseudo_electronics_for_every_solid(self) -> None:
        checked = 0
        for component in self.model["components"]:
            if "template" not in component:
                continue
            template = load_toml(self.resolve(component["template"]))
            regions = template.get("internal_regions", [])
            if not any(region.get("state") == "fan" for region in regions):
                continue
            for region in regions:
                if region.get("state") == "solid":
                    self.assertEqual(region.get("material"), PSEUDO_ELECTRONICS)
                    checked += 1
        self.assertEqual(checked, 55)

    def test_every_fan_curve_resolves(self) -> None:
        fan_count = 0
        for fan in self.model.get("fans", []):
            self.assertIn(fan.get("curve"), self.curve_names)
            fan_count += 1
        for component in self.model["components"]:
            if "template" not in component:
                continue
            template = load_toml(self.resolve(component["template"]))
            for region in template.get("internal_regions", []):
                if region.get("state") == "fan":
                    self.assertIn(region.get("curve"), self.curve_names)
                    fan_count += 1
        self.assertEqual(fan_count, 44)

    def test_final_heat_and_topology_contract(self) -> None:
        total = 0.0
        sources = 0
        vent_count = len(self.model.get("vents", []))
        for component in self.model["components"]:
            if "template" in component:
                data = load_toml(self.resolve(component["template"]))
                vent_count += sum(
                    region.get("state") == "vent"
                    for region in data.get("internal_regions", [])
                )
            else:
                data = component
            component_total, component_sources = heat_summary(data)
            total += component_total
            sources += component_sources
        self.assertEqual(len(self.model["components"]), 17)
        self.assertEqual(sources, 47)
        self.assertTrue(math.isclose(total, 2434.8, abs_tol=1e-9))
        self.assertEqual(vent_count, 31)

    def test_v23_solver_safety_settings_are_retained(self) -> None:
        self.assertEqual(self.model["simulation"]["duration"], 30)
        self.assertEqual(self.model["simulation"]["max_megabyte_usage"], 1536)
        self.assertTrue(
            math.isclose(self.model["environment"]["rho"], 0.9833, abs_tol=1e-12)
        )
        solver = self.model["openfoam_solver"]
        self.assertTrue(solver["enabled"])
        self.assertEqual(solver["parallel_processes"], 4)
        self.assertEqual(solver["mesh"]["fine_dx"], 0.019)


if __name__ == "__main__":
    unittest.main()

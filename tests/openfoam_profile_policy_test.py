import tomllib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class OpenFoamProfilePolicyTest(unittest.TestCase):
    def profile(self, name):
        with (ROOT / "library" / "openfoam_cfg" / name).open("rb") as stream:
            return tomllib.load(stream)["openfoam_solver"]

    def test_screening_keeps_fast_refresh_limit(self):
        profile = self.profile("screening_foam_cfg.toml")
        self.assertEqual(profile["parallel_processes"], 2)
        self.assertEqual(profile["airflow_refresh_maximum_courant_number"], 10.0)
        self.assertEqual(profile["airflow_maximum_time_step"], 0.001)
        self.assertEqual(profile["pimple_outer_correctors"], 3)
        self.assertEqual(profile["pimple_pressure_correctors"], 2)

    def test_validation_profiles_use_courant_one_refreshes(self):
        for name in ("validation_foam_cfg.toml", "indepth_foam_cfg.toml"):
            with self.subTest(name=name):
                profile = self.profile(name)
                self.assertEqual(
                    profile["airflow_refresh_maximum_courant_number"], 1.0
                )
                self.assertEqual(profile["maximum_courant_number"], 1.0)

    def test_indepth_uses_validated_workstation_correctors(self):
        profile = self.profile("indepth_foam_cfg.toml")
        self.assertEqual(profile["parallel_processes"], 2)
        self.assertEqual(profile["pimple_outer_correctors"], 3)
        self.assertEqual(profile["pimple_pressure_correctors"], 2)

    def test_strict_validation_retains_three_by_three(self):
        profile = self.profile("validation_foam_cfg.toml")
        self.assertEqual(profile["parallel_processes"], 4)
        self.assertEqual(profile["pimple_outer_correctors"], 3)
        self.assertEqual(profile["pimple_pressure_correctors"], 3)


if __name__ == "__main__":
    unittest.main()

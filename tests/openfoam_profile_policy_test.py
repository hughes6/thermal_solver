import tomllib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class OpenFoamProfilePolicyTest(unittest.TestCase):
    def config(self, name):
        with (ROOT / "library" / "openfoam_cfg" / name).open("rb") as stream:
            return tomllib.load(stream)

    def profile(self, name):
        return self.config(name)["openfoam_solver"]

    def test_screening_uses_validated_balanced_refresh_limit(self):
        profile = self.profile("screening_foam_cfg.toml")
        self.assertEqual(profile["parallel_processes"], 2)
        self.assertEqual(profile["airflow_refresh_maximum_courant_number"], 2.0)
        self.assertEqual(profile["airflow_maximum_time_step"], 0.001)
        self.assertEqual(profile["pimple_outer_correctors"], 3)
        self.assertEqual(profile["pimple_pressure_correctors"], 2)
        self.assertEqual(profile["thermal_only_maximum_time_step"], 20.0)
        self.assertEqual(
            profile["minimum_tracked_boundary_flow_fraction"], 0.001
        )

    def test_validation_profiles_use_courant_one_refreshes(self):
        for name in ("validation_foam_cfg.toml", "indepth_foam_cfg.toml"):
            with self.subTest(name=name):
                profile = self.profile(name)
                self.assertEqual(
                    profile["airflow_refresh_maximum_courant_number"], 1.0
                )
                self.assertEqual(profile["maximum_courant_number"], 1.0)

    def test_all_profiles_require_spatial_velocity_convergence(self):
        for name in (
            "default_foam_cfg.toml",
            "screening_foam_cfg.toml",
            "validation_foam_cfg.toml",
            "indepth_foam_cfg.toml",
        ):
            with self.subTest(name=name):
                self.assertEqual(
                    self.profile(name)["maximum_velocity_rms_change_fraction"],
                    0.01,
                )

    def test_fidelity_profiles_bound_transient_long_lag_velocity(self):
        self.assertEqual(
            self.profile("screening_foam_cfg.toml")[
                "maximum_accepted_velocity_rms_change_fraction"
            ],
            0.03,
        )
        self.assertEqual(
            self.profile("indepth_foam_cfg.toml")[
                "maximum_accepted_velocity_rms_change_fraction"
            ],
            0.02,
        )
        for name in ("default_foam_cfg.toml", "validation_foam_cfg.toml"):
            with self.subTest(name=name):
                self.assertEqual(
                    self.profile(name)[
                        "maximum_accepted_velocity_rms_change_fraction"
                    ],
                    0.01,
                )

    def test_indepth_uses_validated_workstation_correctors(self):
        config = self.config("indepth_foam_cfg.toml")
        profile = config["openfoam_solver"]
        self.assertEqual(config["mesh"]["fine_dx"], 0.015)
        self.assertEqual(config["mesh"]["coarse_dx"], 0.10)
        self.assertEqual(profile["parallel_processes"], 2)
        self.assertEqual(profile["pimple_outer_correctors"], 3)
        self.assertEqual(profile["pimple_pressure_correctors"], 2)
        self.assertEqual(profile["airflow_refresh_duration"], 0.01)
        self.assertEqual(profile["airflow_refresh_check_interval"], 0.01)
        self.assertEqual(profile["maximum_airflow_refresh_duration"], 0.10)
        self.assertEqual(profile["thermal_only_maximum_time_step"], 20.0)
        self.assertEqual(profile["minimum_tracked_boundary_flow_fraction"], 0.001)

    def test_strict_validation_retains_three_by_three(self):
        profile = self.profile("validation_foam_cfg.toml")
        self.assertEqual(profile["parallel_processes"], 4)
        self.assertEqual(profile["pimple_outer_correctors"], 3)
        self.assertEqual(profile["pimple_pressure_correctors"], 3)


if __name__ == "__main__":
    unittest.main()

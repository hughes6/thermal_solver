import unittest
from pathlib import Path
import re


class SemiFrozenSolverPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = Path(
            "openfoam_semifrozen_solver/semiFrozenChtMultiRegionFoam.C"
        ).read_text(encoding="utf-8")
        cls.exporter = Path("src/openfoam_exporter.hpp").read_text(
            encoding="utf-8"
        )

    def test_thermal_only_solves_fluid_energy_without_full_flow(self):
        start = self.source.index("if (thermalOnlyFlowMode)")
        end = self.source.index("else", start)
        branch = self.source[start:end]
        self.assertIn('#include "EEqn.H"', branch)
        self.assertNotIn('#include "solveFluid.H"', branch)

    def test_thermal_only_updates_density_and_hydrostatic_split(self):
        marker = "Keep the converged velocity and mass-flux operating"
        start = self.source.index(marker)
        end = self.source.index("else if (!frozenFlow)", start)
        branch = self.source[start:end]
        self.assertIn("rho = thermo.rho();", branch)
        self.assertIn("p_rgh = p - rho*gh;", branch)

    def test_solids_and_coupled_energy_remain_active(self):
        self.assertIn("forAll(solidRegions, i)", self.source)
        self.assertIn('#include "solveSolid.H"', self.source)
        self.assertIn("fvMatrixAssemblyPtr->solve();", self.source)
        self.assertIn('#include "correctThermos.H"', self.source)

    def test_mode_resolution_precedes_physical_time_advance(self):
        resolver = self.source.index("bool foundFluidRegion = false;")
        time_advance = self.source.index("++runTime;")
        self.assertLess(resolver, time_advance)
        pre_advance = self.source[resolver:time_advance]
        self.assertIn('getOrDefault("thermalOnlyFlow", false)', pre_advance)
        self.assertIn('getOrDefault("isothermalAirflow", false)', pre_advance)
        self.assertIn("regionThermalOnlyFlow && regionIsothermalAirflow",
                      pre_advance)
        self.assertIn("if (regionIsothermalAirflow)", pre_advance)
        self.assertGreaterEqual(pre_advance.count("exit(FatalError)"), 4)

    def test_zero_step_invocation_still_resolves_mode(self):
        initial_resolution = self.source.index(
            "const bool invocationThermalOnlyFlow = "
            "resolveThermalOnlyFlowMode();"
        )
        time_loop = self.source.index("while (runTime.run())")
        self.assertLess(initial_resolution, time_loop)

    def test_empty_fluid_list_preserves_solid_only_live_path(self):
        self.assertIn(
            "return foundFluidRegion && requestedThermalOnlyFlow;",
            self.source,
        )
        self.assertNotIn("No fluid region is available", self.source)

    def test_isothermal_mode_is_fail_closed_not_solved(self):
        self.assertNotIn("Solving isothermal airflow region", self.source)
        self.assertNotIn("Pressure-correcting isothermal airflow region",
                         self.source)
        self.assertNotIn("if (!isothermalAirflow)", self.source)
        self.assertNotIn("bool isothermalAirflow =", self.source)

    def test_one_pinned_mode_is_reused_in_both_fluid_passes(self):
        # The configuration flags are read only by the pre-time resolver.
        self.assertEqual(
            self.source.count('getOrDefault("thermalOnlyFlow", false)'), 1)
        self.assertEqual(
            self.source.count('getOrDefault("isothermalAirflow", false)'), 1)
        self.assertGreaterEqual(self.source.count("thermalOnlyFlowMode"), 3)
        self.assertIn(
            "requestedThermalOnlyFlow != invocationThermalOnlyFlow",
            self.source)

    def test_solids_and_matrix_lifecycle_have_no_isothermal_guard(self):
        outer_start = self.source.index("for (int oCorr=0;")
        write_start = self.source.index("runTime.write();", outer_start)
        solve_body = self.source[outer_start:write_start]
        self.assertNotIn("isothermalAirflow", solve_body)
        self.assertRegex(
            solve_body,
            re.compile(
                r"forAll\(solidRegions, i\).*?solveSolid\.H.*?"
                r"fvMatrixAssemblyPtr->solve\(\).*?"
                r"fvMatrixAssemblyPtr->clear\(\)",
                re.DOTALL,
            ),
        )

    def test_deployed_binary_policy_marker_is_printed_and_runner_pinned(self):
        marker_match = re.search(
            r'static const char modePolicyMarker\[\]\s*=\s*"([^"]+)";',
            self.source,
        )
        self.assertIsNotNone(marker_match)
        marker = marker_match.group(1)
        self.assertEqual(marker, "THERMAL_SIM_SEMIFROZEN_MODE_POLICY_V1")
        self.assertIn("<< modePolicyMarker << nl << endl;", self.source)
        self.assertLess(
            self.source.index("<< modePolicyMarker << nl << endl;"),
            self.source.index('#include "createTime.H"'),
        )
        self.assertEqual(self.exporter.count(marker), 1)
        self.assertIn(
            'command -v "\n                "semiFrozenChtMultiRegionFoam',
            self.exporter,
        )
        self.assertIn("--thermal-sim-attest", self.exporter)
        self.assertIn(
            "project_source_sha256=$solver_project_source_sha256",
            self.exporter,
        )
        self.assertIn('\\"$semi_frozen_solver\\" -case', self.exporter)
        self.assertNotIn("grep -aFq --", self.exporter)
        self.assertNotIn("semiFrozenChtMultiRegionFoam -case", self.exporter)


if __name__ == "__main__":
    unittest.main()

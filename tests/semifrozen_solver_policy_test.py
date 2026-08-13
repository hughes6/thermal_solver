import unittest
from pathlib import Path


class SemiFrozenSolverPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = Path(
            "openfoam_semifrozen_solver/semiFrozenChtMultiRegionFoam.C"
        ).read_text(encoding="utf-8")

    def test_thermal_only_solves_fluid_energy_without_full_flow(self):
        start = self.source.index("if (thermalOnlyFlow)")
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


if __name__ == "__main__":
    unittest.main()

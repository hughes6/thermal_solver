import tempfile
import unittest
from pathlib import Path

from tools.openfoam_pressure_jump_fan_audit import audit, markdown


def write_field(
    path: Path, patches: dict[str, list[float]], internal: float = 0.0
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    entries = []
    for name, values in patches.items():
        payload = "\n".join(str(value) for value in values)
        entries.append(
            f"{name}\n{{\n    value nonuniform List<scalar>\n"
            f"    {len(values)}\n    (\n{payload}\n    );\n}}"
        )
    path.write_text(
        "FoamFile\n{\n    format ascii;\n}\n"
        f"internalField uniform {internal};\nboundaryField\n{{\n"
        + "\n".join(entries)
        + "\n}\n",
        encoding="ascii",
    )


class PressureJumpFanAuditTest(unittest.TestCase):
    def test_markdown_marks_reverse_flow_and_summarizes(self):
        report = markdown(
            0.4,
            {"forward": (1.0, 0.8), "reverse": (-0.2, -0.1)},
        )
        self.assertIn("t=0.4 mass flow", report)
        self.assertIn("`reverse` | -0.2 | -0.1 ⚠ reverse", report)
        self.assertIn("nonpositive flows: 1", report)

    def test_sums_decomposed_mass_and_volume_flow(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            patch = "internal_test_fan_pressure_jump_master"
            boundary = case / "constant" / "fluid" / "polyMesh" / "boundary"
            boundary.parent.mkdir(parents=True)
            boundary.write_text(
                f"{patch}\n{{\n    type cyclic;\n}}\n", encoding="ascii"
            )
            for rank, phi in enumerate(([1.2, 2.4], [3.6])):
                time = case / f"processor{rank}" / "0.05"
                write_field(time / "fluid" / "phi", {patch: list(phi)})
                write_field(time / "fluid" / "rho", {patch: [1.2]}, 1.2)

            time_s, flows = audit(case)

            self.assertEqual(time_s, 0.05)
            self.assertAlmostEqual(flows["internal_test_fan"][0], 7.2)
            self.assertAlmostEqual(flows["internal_test_fan"][1], 6.0)

    def test_uses_internal_density_when_cyclic_patch_has_no_value(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            patch = "internal_test_fan_pressure_jump_master"
            boundary = case / "constant" / "fluid" / "polyMesh" / "boundary"
            boundary.parent.mkdir(parents=True)
            boundary.write_text(f"{patch}\n{{\n type cyclic;\n}}\n")
            time = case / "0.05"
            write_field(time / "fluid" / "phi", {patch: [1.2, 2.4]})
            write_field(time / "fluid" / "rho", {patch: []}, 1.2)

            _, flows = audit(case)

            self.assertAlmostEqual(flows["internal_test_fan"][0], 3.6)
            self.assertAlmostEqual(flows["internal_test_fan"][1], 3.0)

    def test_rejects_missing_pressure_jump_patches(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            boundary = case / "constant" / "fluid" / "polyMesh" / "boundary"
            boundary.parent.mkdir(parents=True)
            boundary.write_text("ordinary_patch\n{\n type patch;\n}\n")
            (case / "0").mkdir()
            with self.assertRaisesRegex(ValueError, "no cyclic internal-fan"):
                audit(case)


if __name__ == "__main__":
    unittest.main()

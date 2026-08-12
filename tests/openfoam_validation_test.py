import tempfile
import unittest
import struct
from pathlib import Path

from tools.validate_openfoam_case import (
    internal_values,
    expected_fluid_regions,
    latest_result_paths,
    patch_values,
    signed_weighted_average,
)


class SignedOutletAverageTests(unittest.TestCase):
    def test_reverse_flow_uses_net_flux(self):
        temperatures = [300.0, 293.0]
        fluxes = [0.010, -0.002]
        self.assertAlmostEqual(
            signed_weighted_average(temperatures, fluxes), 301.75
        )

    def test_zero_net_flow_is_rejected(self):
        with self.assertRaises(ValueError):
            signed_weighted_average([300.0, 293.0], [0.01, -0.01])


class BinaryPatchParsingTests(unittest.TestCase):
    def test_empty_decomposed_internal_scalar_field_is_supported(self):
        field = (
            b"FoamFile\n{\nformat binary;\n}\n"
            b"internalField nonuniform List<scalar> 0;\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "T"
            path.write_bytes(field)
            parsed = internal_values(path)
        self.assertEqual(parsed, [])

    def test_empty_decomposed_patch_does_not_consume_next_patch_payload(self):
        field = (
            b"FoamFile\n{\nformat binary;\n}\n"
            b"boundaryField\n{\nEmpty_patch\n{\n"
            b"value nonuniform List<scalar> 0;\n}\nNext_patch\n{\n"
            b"value nonuniform List<scalar> 1(" + struct.pack("<d", 42.0)
            + b")\n;\n}\n}\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "phi"
            path.write_bytes(field)
            parsed = patch_values(path, "Empty_patch")
        self.assertEqual(parsed, [])

    def test_binary_scalar_first_byte_is_not_trimmed_as_whitespace(self):
        # This finite scalar starts with byte 0x20, the ASCII space byte.
        first = struct.unpack("<d", bytes.fromhex("20aa363455537240"))[0]
        values = [first, 298.5]
        payload = struct.pack("<2d", *values)
        field = (
            b"FoamFile\n{\nformat binary;\n}\n"
            b"boundaryField\n{\nValidation_outlet\n{\n"
            b"type calculated;\nvalue nonuniform List<scalar> 2(" +
            payload + b")\n;\n}\n}\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "T"
            path.write_bytes(field)
            parsed = patch_values(path, "Validation_outlet")
        self.assertEqual(len(parsed), 2)
        self.assertEqual(parsed, values)


class LatestResultTests(unittest.TestCase):
    def test_expected_topology_reads_export_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            constant = case / "constant"
            constant.mkdir()
            (constant / "openfoamExportProperties").write_text(
                "expectedConnectedFluidRegions 2;\n"
            )
            self.assertEqual(expected_fluid_regions(case), 2)

    def test_decomposed_common_checkpoint_wins_over_stale_reconstructed_time(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "99").mkdir()
            for rank in range(2):
                processor = case / f"processor{rank}"
                (processor / "100.1").mkdir(parents=True)
                (processor / "100.2").mkdir()
            value, paths = latest_result_paths(case)
        self.assertEqual(value, 100.2)
        self.assertEqual([path.name for path in paths], ["100.2", "100.2"])


if __name__ == "__main__":
    unittest.main()

import tempfile
import unittest
import struct
from pathlib import Path

from tools.validate_openfoam_case import patch_values, signed_weighted_average


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


if __name__ == "__main__":
    unittest.main()

import tempfile
import unittest
import struct
from types import SimpleNamespace
from pathlib import Path

from tools.validate_openfoam_case import (
    internal_values,
    expected_fluid_regions,
    latest_result_paths,
    mesh_connectivity,
    markdown,
    patch_values,
    signed_weighted_average,
)


class ReportSemanticsTests(unittest.TestCase):
    def test_outlet_power_metric_is_not_called_transient_conservation(self):
        result = SimpleNamespace(
            passed=False,
            fluent_temperature_k=None,
            connected_fluid_regions=1,
            cells=10,
            expected_connected_fluid_regions=1,
            pass_connectivity=True,
            inlet_mass_flow_kg_s=-0.1,
            outlet_mass_flow_kg_s=0.1,
            mass_imbalance_fraction=0.0,
            pass_mass_balance=True,
            transported_power_w=1.0,
            applied_power_w=100.0,
            energy_error_fraction=0.99,
            pass_energy_balance=False,
            time_s=1.0,
            inlet_temperature_k=293.15,
            outlet_temperature_k=293.16,
            expected_outlet_temperature_k=294.15,
            solid_average_temperature_k=293.2,
            solid_min_temperature_k=293.15,
            solid_max_temperature_k=293.3,
            outlet_gross_mass_flow_kg_s=0.1,
            outlet_reverse_flow_fraction=0.0,
        )
        rendered = markdown(result)
        self.assertIn("Steady-state heat removal", rendered)
        self.assertIn("transient first-law conservation audit", rendered)
        self.assertNotIn("| Energy conservation |", rendered)


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
    def test_single_decomposed_checkpoint_is_valid_latest_result(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            expected = []
            for rank in range(2):
                checkpoint = case / f"processor{rank}" / "12.5"
                checkpoint.mkdir(parents=True)
                expected.append(checkpoint)
            value, paths = latest_result_paths(case)
            self.assertEqual(value, 12.5)
            self.assertEqual(paths, expected)

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


class MeshConnectivityTests(unittest.TestCase):
    def test_cyclic_patch_pair_is_counted_as_connected(self):
        with tempfile.TemporaryDirectory() as directory:
            mesh = Path(directory)
            header = "FoamFile\n{\nformat ascii;\n}\n"
            # Two cells have no internal face.  Boundary faces 0 and 1 are a
            # coupled cyclic pair, so the physical mesh has one component.
            (mesh / "owner").write_text(header + "2\n(\n0\n1\n)\n")
            (mesh / "neighbour").write_text(header + "0\n(\n)\n")
            (mesh / "boundary").write_text(header + """2
(
    fan_master
    {
        type cyclic;
        neighbourPatch fan_slave;
        nFaces 1;
        startFace 0;
    }
    fan_slave
    {
        type cyclic;
        neighbourPatch fan_master;
        nFaces 1;
        startFace 1;
    }
)
""")
            self.assertEqual(mesh_connectivity(mesh), (2, 1))


if __name__ == "__main__":
    unittest.main()

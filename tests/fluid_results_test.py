import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "plot"))

from fluid_results import (
    build_argument_parser,
    plane_seed_points,
    resolve_field,
    seed_points_from_openings,
    select_fluid_leaves,
)


class FluidResultsTest(unittest.TestCase):
    def test_field_aliases_and_arguments(self):
        self.assertEqual(resolve_field("speed"), ("U", "Velocity magnitude"))
        self.assertEqual(resolve_field("p_rgh"), ("p_rgh", "Hydrostatic pressure"))
        self.assertEqual(resolve_field("custom"), ("custom", "custom"))
        args = build_argument_parser().parse_args([
            "--case", "case", "--field", "speed", "--slice-axis", "z",
            "--slice-position", "0.5", "--vectors", "--contours", "12",
            "--streamlines", "--seed", "fans", "--seed-count", "64",
            "--streamline-direction", "backward", "--save",
        ])
        self.assertEqual(args.field, "speed")
        self.assertEqual(args.slice_axis, "z")
        self.assertEqual(args.slice_position, 0.5)
        self.assertTrue(args.vectors)
        self.assertEqual(args.contours, 12)
        self.assertTrue(args.streamlines)
        self.assertEqual(args.seed, "fans")
        self.assertEqual(args.seed_count, 64)
        self.assertEqual(args.streamline_direction, "backward")
        self.assertTrue(args.save)

    def test_fluid_region_is_selected_from_multiregion_tree(self):
        class Leaf:
            n_cells = 1
            n_points = 8

        class Blocks:
            def __init__(self, mapping):
                self.mapping = mapping
                self.n_blocks = len(mapping)

            def __iter__(self):
                return iter(self.mapping.values())

            def keys(self):
                return self.mapping.keys()

        fluid = Leaf()
        solid = Leaf()
        tree = Blocks({
            "fluid": Blocks({"internalMesh": fluid}),
            "server": Blocks({"internalMesh": solid}),
        })
        selected = select_fluid_leaves(tree)
        self.assertEqual(len(selected), 1)
        self.assertIn("fluid", selected[0][0])

    def test_plane_and_opening_seeds_are_inside_domain(self):
        try:
            import numpy as np
        except ImportError:
            self.skipTest("optional NumPy dependency unavailable")
        bounds = (0.0, 1.0, 0.0, 2.0, 0.0, 3.0)
        plane = plane_seed_points(bounds, "y", 0.5, 16, np)
        self.assertEqual(plane.shape, (16, 3))
        np.testing.assert_allclose(plane[:, 1], 0.5)
        rack = SimpleNamespace(openings=[SimpleNamespace(
            kind="Vent", center=(0.5, 0.0, 1.5), direction=(0, 1, 0),
            size=(0.4, 0.0, 0.4), diameter=0.0,
        )])
        vents = seed_points_from_openings(rack, "vent", bounds, 9, np)
        self.assertEqual(vents.shape, (9, 3))
        self.assertTrue(np.all(vents[:, 1] > 0.0))


if __name__ == "__main__":
    unittest.main()

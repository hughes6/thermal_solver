import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "plot"))

from fluid_results import build_argument_parser, resolve_field, select_fluid_leaves


class FluidResultsTest(unittest.TestCase):
    def test_field_aliases_and_arguments(self):
        self.assertEqual(resolve_field("speed"), ("U", "Velocity magnitude"))
        self.assertEqual(resolve_field("p_rgh"), ("p_rgh", "Hydrostatic pressure"))
        self.assertEqual(resolve_field("custom"), ("custom", "custom"))
        args = build_argument_parser().parse_args([
            "--case", "case", "--field", "speed", "--slice-axis", "z",
            "--slice-position", "0.5", "--vectors", "--save",
        ])
        self.assertEqual(args.field, "speed")
        self.assertEqual(args.slice_axis, "z")
        self.assertEqual(args.slice_position, 0.5)
        self.assertTrue(args.vectors)
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


if __name__ == "__main__":
    unittest.main()

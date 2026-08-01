import ast
from pathlib import Path
import unittest


def load_centered_surface_rectangle():
    source_path = Path(__file__).resolve().parents[1] / "plot" / "plot.py"
    tree = ast.parse(source_path.read_text(encoding="utf-8"), source_path)
    function = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "centered_surface_rectangle"
    )
    namespace = {}
    module = ast.Module(body=[function], type_ignores=[])
    exec(compile(module, source_path, "exec"), namespace)
    return namespace["centered_surface_rectangle"]


centered_surface_rectangle = load_centered_surface_rectangle()


class PlotGeometryTest(unittest.TestCase):
    def assert_rectangle(self, actual, expected):
        self.assertEqual(actual[0], expected[0])
        self.assertAlmostEqual(actual[1], expected[1])
        for actual_value, expected_value in zip(actual[2], expected[2]):
            self.assertAlmostEqual(actual_value, expected_value)
        for actual_value, expected_value in zip(actual[3], expected[3]):
            self.assertAlmostEqual(actual_value, expected_value)

    def test_z_normal_vent_is_centered_in_xy(self):
        self.assert_rectangle(
            centered_surface_rectangle(
                (0.50, 0.40, 0.30), (0.20, 0.10, 0.0), (0.0, 0.0, 1.0)
            ),
            ("z", 0.30, (0.40, 0.35), (0.20, 0.10)),
        )

    def test_y_normal_vent_is_centered_in_xz(self):
        self.assert_rectangle(
            centered_surface_rectangle(
                (0.50, 0.40, 0.30), (0.20, 0.0, 0.10), (0.0, -1.0, 0.0)
            ),
            ("y", 0.40, (0.40, 0.25), (0.20, 0.10)),
        )

    def test_x_normal_vent_is_centered_in_yz(self):
        self.assert_rectangle(
            centered_surface_rectangle(
                (0.50, 0.40, 0.30), (0.0, 0.20, 0.10), (1.0, 0.0, 0.0)
            ),
            ("x", 0.50, (0.30, 0.25), (0.20, 0.10)),
        )


if __name__ == "__main__":
    unittest.main()

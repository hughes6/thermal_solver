import ast
import importlib.util
import runpy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


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


def load_component_region_color():
    source_path = Path(__file__).resolve().parents[1] / "plot" / "plot_component.py"
    tree = ast.parse(source_path.read_text(encoding="utf-8"), source_path)
    function = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "component_region_color"
    )
    namespace = {}
    module = ast.Module(body=[function], type_ignores=[])
    exec(compile(module, source_path, "exec"), namespace)
    return namespace["component_region_color"]


def load_select_component_block():
    source_path = Path(__file__).resolve().parents[1] / "plot" / "plot_component.py"
    tree = ast.parse(source_path.read_text(encoding="utf-8"), source_path)
    function = next(
        node for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "select_component_block"
    )
    namespace = {}
    exec(compile(ast.Module(body=[function], type_ignores=[]), source_path, "exec"), namespace)
    return namespace["select_component_block"]


def load_rack_internal_region_colors():
    source_path = Path(__file__).resolve().parents[1] / "plot" / "plot.py"
    tree = ast.parse(source_path.read_text(encoding="utf-8"), source_path)
    assignment = next(
        node
        for node in tree.body
        if isinstance(node, ast.Assign)
        and any(
            isinstance(target, ast.Name)
            and target.id == "internal_region_colors"
            for target in node.targets
        )
    )
    return ast.literal_eval(assignment.value)


centered_surface_rectangle = load_centered_surface_rectangle()
component_region_color = load_component_region_color()
select_component_block = load_select_component_block()
rack_internal_region_colors = load_rack_internal_region_colors()


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

    def test_all_air_regions_use_the_same_color(self):
        for spelling in ("Air", "air", "AIR"):
            with self.subTest(spelling=spelling):
                self.assertEqual(
                    component_region_color(spelling, "arbitrary-cycle-color"),
                    "tab:cyan",
                )

    def test_non_air_region_keeps_its_cycle_color(self):
        self.assertEqual(
            component_region_color("Fan", "tab:orange"), "tab:orange"
        )

    def test_rack_and_component_air_colors_match(self):
        self.assertEqual(
            rack_internal_region_colors["Air"],
            component_region_color("Air", "unused"),
        )

    def test_component_index_selects_duplicate_name_instance(self):
        lines = [
            "Component 1: Duplicate", "dimensions: 1 x 1 x 1 m",
            "Component 2: Duplicate", "dimensions: 2 x 2 x 2 m",
            "Component 3: Last", "dimensions: 3 x 3 x 3 m",
        ]
        self.assertEqual(
            select_component_block(lines, component_index=2),
            lines[2:4],
        )

    def test_duplicate_name_reports_index_remedy(self):
        lines = ["Component 1: Duplicate", "Component 2: Duplicate"]
        with self.assertRaisesRegex(ValueError, "component-index"):
            select_component_block(lines, component_name="Duplicate")

    def test_component_index_range_is_checked(self):
        with self.assertRaisesRegex(ValueError, "range 1..1"):
            select_component_block(["Component 1: Only"], component_index=2)

    @unittest.skipUnless(
        importlib.util.find_spec("matplotlib"),
        "Matplotlib is required for the artist-level plotting test",
    )
    def test_two_rendered_air_regions_use_identical_artist_colors(self):
        import matplotlib

        matplotlib.use("Agg", force=True)
        import matplotlib.pyplot as plt
        from mpl_toolkits.mplot3d.axes3d import Axes3D

        plotter = (
            Path(__file__).resolve().parents[1] / "plot" / "plot_component.py"
        )
        geometry = """\
Component 1: Two air regions
dimensions: 1 x 1 x 1 m
coordinates: 0 0 0 m
Internal Region 1:
type: Air
size: 0.3 0.4 0.5 m
local_position: 0.1 0.1 0.1 m
diameter: 0 m
direction: 0 0 0
Internal Region 2:
type: material/AIR
size: 0.2 0.3 0.4 m
local_position: 0.6 0.5 0.4 m
diameter: 0 m
direction: 0 0 0
"""
        calls = []
        original_bar3d = Axes3D.bar3d

        def recording_bar3d(axis, *args, **kwargs):
            calls.append(kwargs.copy())
            return original_bar3d(axis, *args, **kwargs)

        with tempfile.TemporaryDirectory() as directory:
            input_path = Path(directory) / "geometry.txt"
            input_path.write_text(geometry, encoding="utf-8")
            with (
                mock.patch.object(Axes3D, "bar3d", recording_bar3d),
                mock.patch.object(plt, "show"),
                mock.patch.object(
                    sys,
                    "argv",
                    [str(plotter), "--input", str(input_path)],
                ),
            ):
                runpy.run_path(str(plotter), run_name="__main__")
        plt.close("all")

        region_calls = calls[1:]
        self.assertEqual(len(region_calls), 2)
        self.assertEqual(
            [call["color"] for call in region_calls],
            ["tab:cyan", "tab:cyan"],
        )
        self.assertEqual(
            [call["edgecolor"] for call in region_calls],
            ["tab:cyan", "tab:cyan"],
        )


if __name__ == "__main__":
    unittest.main()

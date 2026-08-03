import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "plot"))

from heat_animation import parse_rack_file


class HeatAnimationGeometryTest(unittest.TestCase):
    def test_internal_vent_keeps_center_direction_and_shape(self):
        geometry = """\
Rack dimensions:
  height: 1 m
  width: 2 m
  depth: 3 m
Components:
Component 1: enclosure
  dimensions: 1 1 1 m
  coordinates: 0.5 0.5 0 m
  Internal Region 1:
    type: Vent
    size: 0.4 0 0.2 m
    global_position: 0.8 0.5 0.4 m
    diameter: 0 m
    direction: 0 -1 0
Fans:
Vents:
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "output.txt"
            path.write_text(geometry, encoding="utf-8")
            rack = parse_rack_file(str(path))

        vent = rack.components[0].regions[0]
        self.assertEqual(vent.origin, (0.8, 0.5, 0.4))
        self.assertEqual(vent.direction, (0.0, -1.0, 0.0))
        self.assertEqual(vent.size, (0.4, 0.0, 0.2))
        self.assertEqual(vent.diameter, 0.0)


if __name__ == "__main__":
    unittest.main()

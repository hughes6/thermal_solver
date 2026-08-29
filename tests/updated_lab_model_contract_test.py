import math
import tomllib
import unittest
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "library" / "models" / "new_model_updated.toml"
CURVES = ROOT / "library" / "fan_curves" / "fan_curves.toml"
ASSUMPTIONS = ROOT / "validation" / "UPDATED_MODEL_ASSUMPTIONS.toml"
NI_CANONICAL = (
    ROOT / "library" / "components" / "updated_NI_PXIe_Chassis.toml"
)
NI_MINIMUM_SEPARATOR_SENSITIVITY = (
    ROOT / "library" / "components"
    / "updated_NI_PXIe_Chassis_minimum_separator_sensitivity.toml"
)
NI_MINIMUM_SEPARATOR_MODEL = (
    ROOT / "library" / "models"
    / "validation_ni_minimum_separator_sensitivity.toml"
)


def first_positive_zero(curve):
    a, b, c = (float(curve[key]) for key in ("a", "b", "c"))
    roots = []
    if c:
        discriminant = b * b + 4.0 * c * a
        if discriminant >= 0.0:
            root = math.sqrt(discriminant)
            roots.extend(((-b - root) / (2.0 * c),
                          (-b + root) / (2.0 * c)))
    elif b:
        roots.append(a / b)
    positive = [value for value in roots if math.isfinite(value) and value > 0.0]
    if not positive:
        raise ValueError(f"curve {curve['name']} has no positive zero")
    return min(positive)


class UpdatedLabModelContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with MODEL.open("rb") as source:
            cls.model = tomllib.load(source)
        with CURVES.open("rb") as source:
            cls.curves = {
                curve["name"]: curve for curve in tomllib.load(source)["fan_curve"]
            }

    def test_revision_has_separate_identity_and_altitude_density(self):
        self.assertEqual(self.model["name"], "new model updated 2026-08-24")
        self.assertEqual(self.model["environment"]["elevation"], 5500.0)
        self.assertTrue(math.isclose(
            self.model["environment"]["rho"], 0.9833, abs_tol=1e-12
        ))
        self.assertEqual(len(self.model["components"]), 13)
        self.assertEqual(len(self.model["fans"]), 9)
        self.assertEqual(self.model["simulation"]["duration"], 30)

    def test_every_versioned_component_exists_and_parses(self):
        templates = [
            component.get("template") for component in self.model["components"]
            if component.get("template", "").startswith("library/components/updated_")
        ]
        self.assertEqual(len(templates), 8)
        for template in templates:
            with self.subTest(template=template):
                path = ROOT / template
                self.assertTrue(path.is_file())
                with path.open("rb") as source:
                    tomllib.load(source)

    def test_every_component_template_has_portable_filename_case(self):
        for component in self.model["components"]:
            template = component.get("template")
            if not template:
                continue
            current = ROOT
            for part in Path(template).parts:
                with self.subTest(template=template, part=part):
                    self.assertIn(
                        part,
                        {entry.name for entry in current.iterdir()},
                        f"{template!r} does not match on-disk filename case",
                    )
                current /= part

    def test_ordered_component_placements_and_rail_two_depths_are_pinned(self):
        observed = []
        for component in self.model["components"]:
            identity = component.get("template")
            if identity is None:
                identity = f"inline:{component['name']}"
            placement = component["position"]
            observed.append((
                identity,
                placement["units"],
                float(placement["x"]),
                float(placement["y"]),
                float(placement["z"]),
            ))
        self.assertEqual(
            observed,
            [
                ("library/components/updated_eaton_UPS.toml", "u", 1.6, 0.0, 0.0),
                ("library/components/updated_DELL_R470.toml", "u", 1.128, 0.0, 4.0),
                ("library/components/updated_Keysight_N5766A.toml", "u", 1.79, 0.0, 6.0),
                ("library/components/updated_keysight_N6701C.toml", "u", 1.769, 0.0, 7.0),
                ("library/components/updated_trenton_3u_bam.toml", "u", 1.12, 0.0, 9.0),
                ("library/components/eaton_KVM.toml", "u", 1.15, 0.0, 14.0),
                ("inline:3U storage Shelf", "u", 1.55, 0.0, 15.0),
                ("library/components/updated_Thruster_Load_Box.toml", "u", 1.12, 0.0, 30.0),
                ("library/components/updated_cisco_catalyst_9300_24.toml", "u", 1.544, 0.0, 34.0),
                ("library/components/EATON_PDU_PDUMNH30.toml", "u", 1.55, 16.0, 2.0),
                ("library/components/Fan_control_kit_PS.toml", "u", 6.0, 21.0, 31.0),
                ("library/components/Fan_control_kit_PS.toml", "u", 5.0, 21.0, 31.0),
                ("library/components/updated_NI_PXIe_Chassis.toml", "u", 1.0, 18.0, 35.0),
            ],
        )

    def test_inline_storage_shelf_numerical_contract_is_pinned(self):
        shelves = [
            component for component in self.model["components"]
            if "template" not in component
        ]
        self.assertEqual(len(shelves), 1)
        shelf = shelves[0]
        self.assertEqual(shelf["name"], "3U storage Shelf")
        self.assertEqual(float(shelf["watts"]), 0.0)
        self.assertEqual(
            shelf["size"],
            {"units": "u", "width": 10, "depth": 9.3, "height": 3.0},
        )
        self.assertEqual(
            shelf["material"],
            {"rho": 2700, "cp": 900, "k": 150},
        )
        self.assertNotIn("internal_regions", shelf)

    def test_every_installed_template_geometry_inventory_is_pinned(self):
        expected = {
            "library/components/updated_eaton_UPS.toml":
                ((440.0, 605.0, 86.5, "mm"), Counter(air=2, vent=2, fan=1, solid=3)),
            "library/components/updated_DELL_R470.toml":
                ((482.0, 817.0, 43.0, "mm"), Counter(air=1, solid=4, vent=2, fan=6)),
            "library/components/updated_Keysight_N5766A.toml":
                ((422.8, 432.8, 43.6, "mm"), Counter(air=6, fan=8, vent=4, solid=10)),
            "library/components/updated_keysight_N6701C.toml":
                ((425.0, 549.7, 44.45, "mm"), Counter(air=6, fan=8, vent=4, solid=10)),
            "library/components/updated_trenton_3u_bam.toml":
                ((482.6, 500.0, 133.35, "mm"), Counter(air=7, vent=4, fan=6, solid=16)),
            "library/components/eaton_KVM.toml":
                ((480.0, 685.0, 44.0, "mm"), Counter(air=1, solid=2)),
            "library/components/updated_Thruster_Load_Box.toml":
                ((482.6, 228.6, 88.9, "mm"), Counter(air=1, vent=2, fan=2, solid=1)),
            "library/components/updated_cisco_catalyst_9300_24.toml":
                ((445.0, 488.0, 44.0, "mm"), Counter(air=1, vent=3, fan=4, solid=3)),
            "library/components/EATON_PDU_PDUMNH30.toml":
                ((444.5, 317.5, 88.9, "mm"), Counter(air=1, solid=1)),
            "library/components/Fan_control_kit_PS.toml":
                ((40.0, 113.5, 125.2, "mm"), Counter(air=1, vent=2, solid=1)),
            "library/components/updated_NI_PXIe_Chassis.toml":
                ((355.6, 214.2, 177.2, "mm"), Counter(air=2, vent=4, fan=3, solid=7)),
        }
        observed_templates = {
            component["template"]
            for component in self.model["components"]
            if "template" in component
        }
        self.assertEqual(observed_templates, set(expected))
        for template, (expected_size, expected_states) in expected.items():
            with self.subTest(template=template):
                with (ROOT / template).open("rb") as source:
                    definition = tomllib.load(source)
                component_size = definition["size"]
                self.assertEqual(
                    (
                        float(component_size["width"]),
                        float(component_size["depth"]),
                        float(component_size["height"]),
                        component_size["units"],
                    ),
                    expected_size,
                )
                self.assertEqual(
                    Counter(
                        region["state"]
                        for region in definition.get("internal_regions", [])
                    ),
                    expected_states,
                )

    def test_ni_partial_separator_geometry_and_overlap_are_explicit(self):
        with NI_CANONICAL.open("rb") as source:
            ni = tomllib.load(source)
        regions = {region["name"]: region for region in ni["internal_regions"]}

        self.assertEqual(
            regions["Interior air"]["position"],
            {"units": "mm", "x": 5.0, "y": 5.0, "z": 5.0},
        )
        self.assertEqual(
            regions["Interior air"]["size"],
            {
                "units": "mm",
                "width": 345.6,
                "depth": 204.2,
                "height": 167.2,
            },
        )
        self.assertEqual(
            regions["Card Slot air"]["position"],
            {"units": "mm", "x": 10.0, "y": 64.2, "z": 25.0},
        )
        self.assertEqual(
            regions["Card Slot air"]["size"],
            {
                "units": "mm",
                "width": 240.0,
                "depth": 150.0,
                "height": 135.0,
            },
        )
        expected_partial_separator = {
            "Card Slot wall 1": (
                {"units": "mm", "x": 10.0, "y": 59.2, "z": 10.0},
                {"units": "mm", "width": 240.0, "depth": 5.0, "height": 160.0},
            ),
            "Card Slot wall 2": (
                {"units": "mm", "x": 250.0, "y": 59.2, "z": 10.0},
                {"units": "mm", "width": 5.0, "depth": 155.0, "height": 160.0},
            ),
            "Card End Filler": (
                {"units": "mm", "x": 10.0, "y": 209.2, "z": 25.0},
                {"units": "mm", "width": 240.0, "depth": 5.0, "height": 135.0},
            ),
        }
        for name, (expected_position, expected_size) in expected_partial_separator.items():
            with self.subTest(region=name):
                self.assertEqual(regions[name]["state"], "solid")
                self.assertEqual(regions[name]["position"], expected_position)
                self.assertEqual(regions[name]["size"], expected_size)

    def test_ni_minimum_separator_sensitivity_is_bounded_and_inactive(self):
        with NI_CANONICAL.open("rb") as source:
            canonical = tomllib.load(source)
        with NI_MINIMUM_SEPARATOR_SENSITIVITY.open("rb") as source:
            sensitivity = tomllib.load(source)
        self.assertEqual(
            sensitivity["name"],
            canonical["name"] + " - PROVISIONAL minimum separator sensitivity",
        )

        provisional_name = (
            "PROVISIONAL minimum Card Slot wall 3 separator sensitivity"
        )
        canonical_names = {
            region["name"] for region in canonical["internal_regions"]
        }
        self.assertNotIn(provisional_name, canonical_names)
        self.assertEqual(
            [
                component.get("template")
                for component in self.model["components"]
                if "NI_PXIe_Chassis" in component.get("template", "")
            ],
            ["library/components/updated_NI_PXIe_Chassis.toml"],
        )

        added_regions = [
            region for region in sensitivity["internal_regions"]
            if region["name"] == provisional_name
        ]
        self.assertEqual(len(added_regions), 1)
        wall = added_regions[0]
        self.assertEqual(wall["state"], "solid")
        self.assertEqual(wall["watts"], 0.0)
        self.assertEqual(
            wall["position"],
            {"units": "mm", "x": 5.0, "y": 59.2, "z": 10.0},
        )
        self.assertEqual(
            wall["size"],
            {
                "units": "mm",
                "width": 5.0,
                "depth": 155.0,
                "height": 160.0,
            },
        )
        self.assertEqual(
            wall["material"], {"rho": 2700.0, "cp": 900.0, "k": 200.0}
        )
        wall_volume_m3 = math.prod(
            float(wall["size"][axis]) / 1000.0
            for axis in ("width", "depth", "height")
        )
        self.assertTrue(
            math.isclose(wall_volume_m3, 0.000124, abs_tol=1.0e-15)
        )

        sensitivity_without_wall = dict(sensitivity)
        sensitivity_without_wall["name"] = canonical["name"]
        sensitivity_without_wall["internal_regions"] = [
            region for region in sensitivity["internal_regions"]
            if region["name"] != provisional_name
        ]
        self.assertEqual(sensitivity_without_wall, canonical)

    def test_ni_minimum_separator_has_isolated_validation_model(self):
        with NI_MINIMUM_SEPARATOR_MODEL.open("rb") as source:
            fixture = tomllib.load(source)
        self.assertIn("PROVISIONAL", fixture["name"])
        self.assertEqual(
            [component["template"] for component in fixture["components"]],
            [
                "library/components/"
                "updated_NI_PXIe_Chassis_minimum_separator_sensitivity.toml"
            ],
        )
        self.assertEqual(fixture["mesh"]["fine_dx"], 0.019)
        self.assertEqual(fixture["openfoam_solver"]["parallel_processes"], 4)
        self.assertEqual(fixture["simulation"]["dt"], 0.01)
        self.assertEqual(fixture["simulation"]["duration"], 30.0)

    def test_machine_readable_assumption_ledger_matches_provisional_model(self):
        with ASSUMPTIONS.open("rb") as source:
            ledger = tomllib.load(source)
        self.assertEqual(
            ledger["schema"], "thermal-sim-research-lab-assumptions-v1"
        )
        assumptions = {row["id"]: row for row in ledger["assumption"]}
        self.assertEqual(len(assumptions), 9)
        self.assertEqual(
            assumptions["ni_separator_walls"]["status"], "missing"
        )
        self.assertEqual(
            assumptions["heat_load_inventory"]["total_watts"], 2315.0
        )
        self.assertEqual(
            assumptions["storage_shelf_construction"]["status"],
            "provisional_conservative",
        )

        expected_depths = {
            "rail2_pdu_depth": (
                "library/components/EATON_PDU_PDUMNH30.toml", [16.0]
            ),
            "rail2_meanwell_depths": (
                "library/components/Fan_control_kit_PS.toml", [21.0, 21.0]
            ),
            "rail2_ni_depth": (
                "library/components/updated_NI_PXIe_Chassis.toml", [18.0]
            ),
        }
        for assumption_id, (template, expected_y) in expected_depths.items():
            with self.subTest(assumption=assumption_id):
                assumption = assumptions[assumption_id]
                self.assertEqual(assumption["status"], "provisional")
                self.assertEqual(assumption["component_template"], template)
                self.assertEqual(assumption["coordinate"], "y")
                self.assertEqual(assumption["units"], "u")
                self.assertEqual(
                    [
                        float(component["position"]["y"])
                        for component in self.model["components"]
                        if component.get("template") == template
                    ],
                    expected_y,
                )
                self.assertEqual(float(assumption["value"]), expected_y[0])

    def test_unplaced_r360_identity_matches_its_one_u_height(self):
        path = ROOT / "library/components/updated_DELL_R360.toml"
        with path.open("rb") as source:
            component = tomllib.load(source)
        self.assertEqual(component["name"], "Dell PowerEdge R360 1U")
        self.assertAlmostEqual(component["size"]["height"], 43.0)
        self.assertEqual(component["size"]["units"], "mm")

    def test_revised_heat_inventory_and_component_identities(self):
        expected = {
            "Eaton SU3000RTXLCD2UTAA UPS": 120.0,
            "Dell PowerEdge R470 1U": 1000.0,
            "Keysight N5766A PS": 270.0,
            "Keysight N6701C PS": 270.0,
            "Trenton 3U BAM": 370.0,
            "Tripp Lite B020-U08-19-IP KVM": 20.0,
            "3U storage Shelf": 0.0,
            "Thruster Load Box 2U": 10.0,
            "Cisco 9300 Network Switch": 150.0,
            "Eaton PDUMNH30 PDU": 10.0,
            "NI PXIe 784782-01 Chassis": 65.0,
        }
        observed = {}
        for component in self.model["components"]:
            template = component.get("template")
            if template:
                with (ROOT / template).open("rb") as source:
                    definition = tomllib.load(source)
            else:
                definition = component
            watts = float(definition.get("watts", 0.0)) + sum(
                float(region.get("watts", 0.0))
                for region in definition.get("internal_regions", [])
            )
            name = definition["name"]
            if name == "Meanwell EDR-120-24":
                observed[name] = observed.get(name, 0.0) + watts
            else:
                observed[name] = watts
        expected["Meanwell EDR-120-24"] = 30.0
        self.assertEqual(observed, expected)
        self.assertEqual(sum(observed.values()), 2315.0)

    def test_corrected_device_flow_directions_are_preserved(self):
        for template in (
            "library/components/updated_Keysight_N5766A.toml",
            "library/components/updated_keysight_N6701C.toml",
        ):
            with (ROOT / template).open("rb") as source:
                component = tomllib.load(source)
            for fan in (
                region for region in component["internal_regions"]
                if region.get("state") == "fan"
            ):
                with self.subTest(component=template, fan=fan["name"]):
                    self.assertEqual(
                        fan["direction"], {"x": 0.0, "y": 1.0, "z": 0.0}
                    )
        with (ROOT / "library/components/updated_NI_PXIe_Chassis.toml").open(
            "rb"
        ) as source:
            ni = tomllib.load(source)
        power_fan = next(
            region for region in ni["internal_regions"]
            if region.get("name") == "Power Supply fan"
        )
        self.assertEqual(
            power_fan["direction"], {"x": 0.0, "y": 1.0, "z": 0.0}
        )

        with (ROOT / "library/components/updated_eaton_UPS.toml").open(
            "rb"
        ) as source:
            ups = tomllib.load(source)
        air = next(
            region for region in ups["internal_regions"]
            if region.get("state") == "air"
        )
        exhaust = next(
            region for region in ups["internal_regions"]
            if region.get("state") == "fan"
        )
        self.assertEqual(
            exhaust["size"]["height"],
            air["size"]["height"],
        )

    def test_staged_profiles_preserve_revised_physics(self):
        for filename in (
            "new_model_updated_native_smoke.toml",
            "new_model_updated_native_regression.toml",
            "new_model_updated_openfoam_export_test.toml",
        ):
            with self.subTest(profile=filename):
                with (ROOT / "library" / "models" / filename).open("rb") as source:
                    variant = tomllib.load(source)
                for key in ("environment", "rack", "components", "fans", "vents"):
                    self.assertEqual(variant[key], self.model[key])
        with (
            ROOT / "library" / "models" / "new_model_updated_native_smoke.toml"
        ).open("rb") as source:
            smoke = tomllib.load(source)
        self.assertFalse(smoke["openfoam_solver"]["enabled"])
        self.assertFalse(smoke["multistage"]["enabled"])
        self.assertEqual(smoke["simulation"]["dt"], 0.00001)

        with (
            ROOT
            / "library"
            / "models"
            / "new_model_updated_native_regression.toml"
        ).open("rb") as source:
            regression = tomllib.load(source)
        self.assertFalse(regression["openfoam_solver"]["enabled"])
        self.assertFalse(regression["multistage"]["enabled"])
        self.assertEqual(regression["simulation"]["dt"], 0.00001)
        self.assertEqual(regression["simulation"]["duration"], 0.00010)
        self.assertEqual(regression["simulation"]["output_interval"], 10)
        self.assertEqual(regression["simulation"]["update_flow_interval"], -1)
        self.assertEqual(
            regression["simulation"]["native_output_directory"],
            "validation/revised_native_regression_2026-08-26/run_002",
        )
        self.assertFalse(regression["simulation"]["native_overwrite"])
        self.assertTrue(regression["mesh"]["adaptive"])
        self.assertEqual(
            (regression["mesh"]["fine_dx"],
             regression["mesh"]["coarse_dx"],
             regression["mesh"]["refinement_margin"]),
            (0.050, 0.200, 0.0),
        )
        self.assertEqual(regression["flow_solver"]["max_iterations"], 1000)
        self.assertEqual(regression["flow_solver"]["max_outer_iters"], 10)
        self.assertEqual(
            regression["logger"]["template"],
            "library/loggers/native_regression_logger.toml",
        )
        with (
            ROOT / "library" / "loggers" / "native_regression_logger.toml"
        ).open("rb") as source:
            logger = tomllib.load(source)["logger"]
        self.assertFalse(logger["enable_field_logging"])
        self.assertTrue(logger["enable_summary_logging"])
        self.assertFalse(logger["enable_probe_logging"])
        self.assertEqual(logger["summary_interval"], 1)
        self.assertEqual(
            [item["name"] for item in logger["summary"]],
            ["solid_temperature", "fluid_temperature", "fluid_velocity"],
        )
        self.assertEqual(smoke["simulation"]["duration"], 0.00001)
        self.assertEqual(smoke["mesh"]["fine_dx"], 0.019)
        self.assertEqual(smoke["simulation"]["max_megabyte_usage"], 1536)
        self.assertEqual(smoke["flow_solver"]["max_iterations"], 100)
        self.assertEqual(smoke["flow_solver"]["max_outer_iters"], 2)

    def test_openfoam_campaign_is_locked_to_four_ranks(self):
        self.assertEqual(
            self.model["openfoam_solver"]["parallel_processes"], 4
        )
        export_path = (
            ROOT / "library" / "models"
            / "new_model_updated_openfoam_export_test.toml"
        )
        with export_path.open("rb") as source:
            export_fixture = tomllib.load(source)
        self.assertEqual(
            export_fixture["openfoam_solver"]["parallel_processes"], 4
        )
        self.assertEqual(
            export_fixture["openfoam_solver"][
                "thermal_only_maximum_time_step"
            ],
            20.0,
        )
        self.assertNotIn(
            "thermal_only_maximum_time_step",
            self.model["openfoam_solver"],
        )
        self.assertEqual(self.model["simulation"]["max_megabyte_usage"], 1536)
        self.assertEqual(
            export_fixture["simulation"]["max_megabyte_usage"], 1536
        )
        self.assertEqual(self.model["openfoam_solver"]["mesh"]["fine_dx"], 0.019)
        self.assertEqual(
            export_fixture["openfoam_solver"]["mesh"]["fine_dx"], 0.0225
        )

    def test_pre_cfd_rack_system_bound_is_reproducible(self):
        fans = self.model["fans"]
        self.assertEqual(len(fans), 9)
        self.assertEqual({fan["curve"] for fan in fans}, {"top_fan_MS1238E-H"})
        vent = self.model["vents"][0]
        scale = 0.04445
        free_area = (
            vent["size"]["width"] * scale
            * vent["size"]["height"] * scale
            * vent["free_area_ratio"]
        )
        curve = self.curves["top_fan_MS1238E-H"]
        rho = self.model["environment"]["rho"]
        count = len(fans)

        def residual(per_fan_flow):
            total = count * per_fan_flow
            fan_pressure = (
                curve["a"] - curve["b"] * per_fan_flow
                - curve["c"] * per_fan_flow**2
            )
            vent_pressure = 0.5 * rho * (
                total / (vent["vent_discharge_coeff"] * free_area)
            ) ** 2
            return fan_pressure - vent_pressure

        low, high = 0.0, first_positive_zero(curve)
        for _ in range(100):
            midpoint = (low + high) / 2.0
            if residual(midpoint) > 0.0:
                low = midpoint
            else:
                high = midpoint
        total_flow = count * (low + high) / 2.0
        temperature_rise = 2315.0 / (
            rho * total_flow * self.model["environment"]["cp"]
        )
        self.assertTrue(math.isclose(total_flow, 0.2939930588, rel_tol=1e-9))
        self.assertTrue(math.isclose(temperature_rise, 7.96426688, rel_tol=1e-9))

    def test_referenced_curve_domains_and_known_mismatches_are_explicit(self):
        fans = [("rack", fan) for fan in self.model["fans"]]
        for component in self.model["components"]:
            template = component.get("template")
            if not template:
                continue
            with (ROOT / template).open("rb") as source:
                definition = tomllib.load(source)
            fans.extend(
                (template, region)
                for region in definition.get("internal_regions", [])
                if region.get("state") == "fan"
            )
        known_mismatches = {
            # Decimal serialization only: the polynomial crosses at
            # 211.8879972759679 CFM while the rack stores 211.888 CFM.
            ("rack", "top_fan_MS1238E-H"): (
                211.888,
                1.000000012856,
            ),
            (
                "library/components/updated_DELL_R470.toml",
                "Sanyo_Denki_9CRH0412P5J001",
            ): (32.9, 1.0091203978019427),
            (
                "library/components/updated_Thruster_Load_Box.toml",
                "Sunon_MA1062_HVL_GN",
            ): (18.0, 1.307722638540673),
        }
        observed_mismatches = set()
        mismatch_fan_count = 0
        for source_name, fan in fans:
            with self.subTest(
                source=source_name, fan=fan["name"], curve=fan["curve"]
            ):
                self.assertIn(fan["curve"], self.curves)
                curve = self.curves[fan["curve"]]
                self.assertGreater(float(curve["a"]), 0.0)
                self.assertGreater(float(curve["rho_rated"]), 0.0)
                for key in ("a", "b", "c", "rho_rated"):
                    self.assertTrue(math.isfinite(float(curve[key])))
                zero = first_positive_zero(curve)
                self.assertGreater(zero, 0.0)
                nominal = float(fan["cfm"]) * 0.00047194745
                key = (source_name, fan["curve"])
                if nominal > zero * (1.0 + 1.0e-9):
                    self.assertIn(
                        key,
                        known_mismatches,
                        f"{fan['name']} has an unreviewed curve-domain mismatch",
                    )
                    expected_cfm, expected_ratio = known_mismatches[key]
                    self.assertEqual(float(fan["cfm"]), expected_cfm)
                    self.assertTrue(
                        math.isclose(
                            nominal / zero,
                            expected_ratio,
                            rel_tol=1.0e-12,
                            abs_tol=0.0,
                        )
                    )
                    observed_mismatches.add(key)
                    mismatch_fan_count += 1
                else:
                    self.assertNotIn(
                        key,
                        known_mismatches,
                        "known mismatch unexpectedly disappeared; update its evidence",
                    )
                # The exporter clamps all samples beyond the first crossing.
                raw_rebound = (
                    float(curve["a"]) - float(curve["b"]) * 2.0 * zero
                    - float(curve["c"]) * (2.0 * zero) ** 2
                )
                exported = 0.0 if 2.0 * zero > zero else max(0.0, raw_rebound)
                self.assertEqual(exported, 0.0)
        self.assertEqual(observed_mismatches, set(known_mismatches))
        self.assertEqual(mismatch_fan_count, 17)


if __name__ == "__main__":
    unittest.main()

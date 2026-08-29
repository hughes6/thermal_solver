#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "openfoam_exporter.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: openfoam_negative_axis_fan_case CASE_DIR\n";
        return 2;
    }

    Environment environment(
        30.0, 0.0, 20.0, 1005.0, 0.02587, 1.8e-5, 0.71, 1.2);
    Workload workload(100000, 100000000, 100000, 64);
    Rack rack = Rack::from_meters(0.8, 0.2, 0.2);
    rack.set_t(20.0);
    rack.set_cp(1005.0);
    rack.set_k(0.02587);
    rack.set_rho(1.2);
    Mesh mesh = Mesh().build_mesh(
        rack, 0.02, 0.02, 0.02, environment, workload);

    // Symmetric ambient openings leave the internal -X fan as the only
    // directional pressure source. This deliberately isolates the sign and
    // enclosing-face-zone behavior seen by OpenFOAM's fanMomentumSource.
    Vent left_ambient(
        "left ambient", {0.0, 0.16, 0.16}, 1.0, 0.0, 1.0,
        {0.0, 0.10, 0.10}, {-1.0, 0.0, 0.0},
        VentShapeType::Rectangular);
    Vent right_ambient(
        "right ambient", {0.0, 0.16, 0.16}, 1.0, 0.0, 1.0,
        {0.8, 0.10, 0.10}, {1.0, 0.0, 0.0},
        VentShapeType::Rectangular);
    mesh.stamp_vent_for_openfoam(left_ambient);
    mesh.stamp_vent_for_openfoam(right_ambient);

    Component duct = Component::from_meters(0.4, 0.16, 0.16, "negative x duct");
    duct.set_coords_m(0.2, 0.02, 0.02);
    duct.set_t(20.0);
    duct.set_rho_solid(2700.0);
    duct.set_cp(900.0);
    duct.set_k_solid(150.0);
    duct.set_watts(0.0);
    duct.add_region(InternalRegion(
        "duct air", {0.36, 0.12, 0.12}, {0.02, 0.02, 0.02}));
    duct.add_region(InternalRegion(
        "right inlet", {0.0, 0.12, 0.12}, {0.4, 0.08, 0.08},
        {-1.0, 0.0, 0.0}, 1.0, 1.0));
    duct.add_region(InternalRegion(
        "left outlet", {0.0, 0.12, 0.12}, {0.0, 0.08, 0.08},
        {1.0, 0.0, 0.0}, 1.0, 1.0));

    Fan fan(
        "negative x fan", 42.38, 0.0,
        {0.0, 0.08, 0.08}, {0.02, 0.08, 0.08},
        {-1.0, 0.0, 0.0}, FlowType::Exhaust, ShapeType::Rectangular);
    // 20 L/s free flow, 50 Pa shutoff pressure, linear diagnostic curve.
    fan.set_curve(50.0, 2500.0, 0.0, 1.2);
    duct.add_region(InternalRegion(fan));
    duct.order_internal_regions();
    mesh.stamp_component_for_openfoam(duct);

    if (mesh.get_openfoam_internal_flow_devices().size() != 3) {
        std::cerr << "expected one fan and two component vents\n";
        return 3;
    }

    OpenFoamExporter::export_mesh(
        mesh,
        {.case_directory = std::filesystem::path(argv[1]),
         .overwrite = true,
         .parallel_processes = 4,
         .end_time = 0.2,
         .initial_time_step = 1e-4,
         .maximum_time_step = 1e-3,
         .maximum_courant_number = 0.5,
         .field_write_interval = 0.02,
         .report_interval = 0.02,
         .use_k_omega_sst = true,
         .use_fan_curves = true,
         .pimple_outer_correctors = 3,
         .pimple_pressure_correctors = 3});

    const std::filesystem::path case_directory(argv[1]);
    const auto read_text = [](const std::filesystem::path& path) {
        std::ifstream input(path);
        std::ostringstream text;
        text << input.rdbuf();
        return text.str();
    };
    if (std::filesystem::exists(
            case_directory / "system/topoSetDict_internal_fan_baffles") ||
        std::filesystem::exists(
            case_directory / "system/createBafflesDict_internal_fans")) {
        std::cerr << "internal fan baffles must not be exported\n";
        return 4;
    }
    const std::string fan_topology = read_text(
        case_directory / "system/topoSetDict_internal_negative_x_fan_2");
    const std::size_t outside_faces = fan_topology.find(
        "source cellToFace; option outside");
    const std::size_t axial_subset = fan_topology.find(
        "type faceSet; action subset", outside_faces);
    const std::size_t axial_box = fan_topology.find(
        "source boxToFace", axial_subset);
    if (outside_faces == std::string::npos ||
        axial_subset == std::string::npos ||
        axial_box == std::string::npos ||
        fan_topology.find("type faceZoneSet") == std::string::npos ||
        fan_topology.find(
            "source setsToFaceZone; faceSet internal_negative_x_fan_2_faces; "
            "cellSet internal_negative_x_fan_2; flip true;") ==
            std::string::npos) {
        std::cerr << "fan must intersect its boundary with one axial plane\n";
        return 5;
    }
    const std::string preparation =
        read_text(case_directory / "prepare_regions.sh");
    if (preparation.find("createBaffles ") != std::string::npos ||
        preparation.find("fan-baffle topology") != std::string::npos) {
        std::cerr << "preparation must preserve an unbaffled fluid mesh\n";
        return 6;
    }

    std::cout << std::filesystem::absolute(argv[1]).string() << '\n';
}

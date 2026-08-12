#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "openfoam_exporter.hpp"

int main() {
    Environment environment(
        30.0,0.0,20.0,1005.0,0.02587,0.000018,0.71,1.225);
    Workload workload(10000,1000000,100000,100);
    Rack rack=Rack::from_meters(0.5,0.5,0.5);
    Mesh mesh=Mesh().build_mesh(
        rack,0.1,0.1,0.1,environment,workload);

    Component component=Component::from_meters(
        0.3,0.3,0.3,"device report fixture");
    component.set_coords_m(0.1,0.1,0.1);
    component.set_rho_solid(1000.0);
    component.set_cp(1000.0);
    component.set_k_solid(1.0);
    component.add_region(InternalRegion(
        "fixture air",{0.3,0.3,0.3},{0.0,0.0,0.0}));
    Fan fan(
        "centered fixture fan",10.0,0.0,{0.2,0.0,0.2},
        {0.15,0.15,0.15},{0.0,1.0,0.0},
        FlowType::Exhaust,ShapeType::Rectangular);
    component.add_region(InternalRegion(fan));
    component.order_internal_regions();
    mesh.stamp_component_for_openfoam(component);
    Fan top_fan(
        "centered top fan",20.0,0.0,{0.2,0.2,0.0},
        {0.25,0.25,0.5},{0.0,0.0,1.0},
        FlowType::Exhaust,ShapeType::Rectangular);
    mesh.stamp_fan_for_openfoam(top_fan);

    const auto case_path=std::filesystem::temp_directory_path()/
        "thermal_solver_openfoam_device_report_test";
    OpenFoamExporter::export_mesh(
        mesh,{.case_directory=case_path,.overwrite=true});
    std::ifstream report(case_path/"airflow_devices.txt");
    std::ostringstream text;
    text << report.rdbuf();
    report.close();
    const std::string output=text.str();
    assert(output.find("- centered top fan | exhaust fan | faces=9")!=
           std::string::npos);
    assert(output.find("requestedCenter=(0.25 0.25 0.5)")!=
           std::string::npos);
    assert(output.find("requestedSize=(0.2 0.2 0)")!=std::string::npos);
    assert(output.find("faceBoundsMin=(0.1 0.1 0.5)")!=std::string::npos);
    assert(output.find("faceBoundsMax=(0.4 0.4 0.5)")!=std::string::npos);
    assert(output.find("faceCentroid=(0.25 0.25 0.5)")!=
           std::string::npos);
    assert(output.find("- centered fixture fan | internal fan | cells=9")!=
           std::string::npos);
    assert(output.find("requestedCenter=(0.25 0.25 0.25)")!=
           std::string::npos);
    assert(output.find("requestedSize=(0.2 0 0.2)")!=std::string::npos);
    assert(output.find("shape=rectangular")!=std::string::npos);
    assert(output.find("zoneBoundsMin=(0.1 0.2 0.1)")!=std::string::npos);
    assert(output.find("zoneBoundsMax=(0.4 0.3 0.4)")!=std::string::npos);
    assert(output.find("zoneCentroid=(0.25 0.25 0.25)")!=
           std::string::npos);
    assert(output.find("zoneVolume=0.009 m3")!=std::string::npos);

    std::filesystem::remove_all(case_path);
}

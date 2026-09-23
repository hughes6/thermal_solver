#include <openfoam_exporter.hpp>
#include <iostream>
#include <cstdlib>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    Environment env(20,0,20,1005,0.02587,0.000018,0.71,1.225);
    Workload load(10000,1000000,100000,100);
    auto rack=Rack::from_meters(0.2,0.4,0.2);
    rack.set_t(20); rack.set_cp(1005); rack.set_k(0.02587); rack.set_rho(1.225);
    auto mesh=Mesh().build_mesh(rack,0.05,0.05,0.05,env,load);
    auto heater=Component::from_meters(0.1,0.1,0.1,"heater");
    heater.set_coords_m(0,0.15,0); heater.set_t(20);
    heater.set_rho_solid(1000); heater.set_cp(1000); heater.set_k_solid(10);
    heater.set_watts(std::stod(argv[2])); mesh.stamp_component_for_openfoam(heater);
    Fan inlet("inlet",10,0,{0.2,0,0.2},{0.1,0,0.1},{0,1,0},FlowType::Intake,ShapeType::Rectangular);
    Vent outlet("outlet",{0.2,0,0.2},1,0,0.65,{0.1,0.4,0.1},{0,1,0},VentShapeType::Rectangular);
    mesh.stamp_fan_for_openfoam(inlet); mesh.stamp_vent_for_openfoam(outlet);
    OpenFoamExportOptions o; o.case_directory=argv[1]; o.parallel_processes=2;
    if(std::getenv("REUSE_TEST_BUOYANT")) {
        o.temperature_dependent_air=true;
        o.gravity={0.0,-9.81,0.0};
    }
    o.use_multirate_thermal=true; o.use_k_omega_sst=true;
    o.end_time=3; o.airflow_warmup_time=2; o.use_fan_startup_ramp=false;
    o.initial_time_step=0.002; o.airflow_maximum_time_step=0.005;
    o.airflow_refresh_maximum_time_step=0.005; o.maximum_time_step=0.005;
    o.initial_airflow_check_interval=0.05; o.airflow_checkpoint_interval=0.05;
    o.minimum_initial_airflow_duration=0.1; o.frozen_flow_maximum_time_step=0.005;
    o.airflow_refresh_interval=1; o.airflow_refresh_duration=0.05;
    o.field_write_interval=0.05; o.report_interval=0.01;
    o.pimple_outer_correctors=3; o.pimple_pressure_correctors=2;
    o.thermal_only_pimple_outer_correctors=3; o.saved_time_directories=20;
    OpenFoamExporter::export_mesh(mesh,o);
    std::cout<<"Exported 128-cell physical fixture: "<<argv[1]<<"\n";
}

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/solver.hpp"
#include "../src/thermal_estimator.hpp"

namespace {

constexpr double kAirRho = 0.9833;
constexpr double kAirCp = 1005.5;
constexpr double kAirK = 0.02587;
constexpr double kAirMu = 1.81e-5;
constexpr double kAirPr = 0.71;
constexpr double kSolidTemperature = 60.0;
constexpr double kAirTemperature = 20.0;
constexpr double kDt = 1.0e-5;

struct TemporaryCsv {
    std::filesystem::path path;
    ~TemporaryCsv() {
        std::error_code ignored;
        std::filesystem::remove(path,ignored);
    }
};

void require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

std::array<int,3> coordinate_on_axis(int axis,int index) {
    std::array<int,3> result{0,0,0};
    result[axis]=index;
    return result;
}

double width_on_axis(const Cell& cell,int axis) {
    if(axis == 0) return cell.get_dx();
    if(axis == 1) return cell.get_dy();
    return cell.get_dz();
}

struct CaseResult {
    double h_solid = 0.0;
    double h_air = 0.0;
    double expected_h = 0.0;
    double legacy_h = 0.0;
    double frozen_capacity_energy_change = 0.0;
    double estimator_h = 0.0;
};

CaseResult run_case(bool adaptive,int axis,double forced_velocity) {
    std::array<std::vector<double>,3> widths{
        std::vector<double>{0.04},
        std::vector<double>{0.04},
        std::vector<double>{0.04}};
    widths[axis]=adaptive
        ? std::vector<double>{0.019,0.050}
        : std::vector<double>{0.035,0.035};

    Rack rack=Rack::from_meters(
        widths[0][0]+(widths[0].size()>1 ? widths[0][1] : 0.0),
        widths[1][0]+(widths[1].size()>1 ? widths[1][1] : 0.0),
        widths[2][0]+(widths[2].size()>1 ? widths[2][1] : 0.0),
        "convection conservation");
    rack.set_t(kAirTemperature);
    rack.set_cp(kAirCp);
    rack.set_k(kAirK);
    rack.set_rho(kAirRho);
    Environment environment(
        30.0,5500.0,kAirTemperature,kAirCp,kAirK,kAirMu,kAirPr,kAirRho);
    Workload workload(100,10000,1000,16);

    Mesh mesh = adaptive
        ? Mesh().build_adaptive_mesh(
              rack,widths[0],widths[1],widths[2],environment,workload)
        : Mesh().build_mesh(
              rack,widths[0][0],widths[1][0],widths[2][0],
              environment,workload);

    const auto solid_coordinate=coordinate_on_axis(axis,0);
    const auto air_coordinate=coordinate_on_axis(axis,1);
    Cell& solid=mesh.at(
        solid_coordinate[0],solid_coordinate[1],solid_coordinate[2]);
    Cell& air=mesh.at(
        air_coordinate[0],air_coordinate[1],air_coordinate[2]);
    solid.set_state(Cell::State::Component);
    solid.set_T(kSolidTemperature);
    solid.set_rho(2700.0);
    solid.set_cp(900.0);
    solid.set_k(200.0);
    air.set_T(kAirTemperature);
    // Use a tangential velocity so the h correlation sees forced flow while
    // the one-cell transverse direction has no advective neighbor.
    const int velocity_axis=(axis+1)%3;
    if(velocity_axis == 0) air.set_vx(forced_velocity);
    if(velocity_axis == 1) air.set_vy(forced_velocity);
    if(velocity_axis == 2) air.set_vz(forced_velocity);

    const double solid_capacity=solid.get_rho()*solid.get_cp()*solid.volume();
    const double air_capacity=air.get_rho()*air.get_cp()*air.volume();
    const double air_width=width_on_axis(air,axis);
    const double film_temperature=
        0.5*(kSolidTemperature+kAirTemperature)+273.15;
    const double expected_h=Convection::compute_local_h(
        forced_velocity,air_width,kAirRho,kAirMu,kAirK,kAirPr,
        kSolidTemperature-kAirTemperature,film_temperature);
    const double legacy_h=Convection::compute_local_h(
        forced_velocity,air_width,Convection::AIR_RHO,Convection::AIR_MU,
        Convection::AIR_K,Convection::AIR_PR,
        kSolidTemperature-kAirTemperature,film_temperature);
    const double estimator_h=ThermalTimeEstimator::estimate(mesh).
        h_estimate_W_m2K;

    const std::string label=std::string(adaptive ? "adaptive" : "uniform")+
        "_axis_"+std::to_string(axis)+
        (forced_velocity > 0.0 ? "_forced" : "_natural");
    TemporaryCsv csv{
        std::filesystem::temp_directory_path()/
        ("thermal_"+label+".csv")};
    Solver solver(
        std::move(mesh),kDt,kDt,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,10000,
        csv.path.string());
    solver.solve();

    const Mesh& result=solver.get_mesh();
    const Cell& result_solid=result.at(
        solid_coordinate[0],solid_coordinate[1],solid_coordinate[2]);
    const Cell& result_air=result.at(
        air_coordinate[0],air_coordinate[1],air_coordinate[2]);
    CaseResult case_result;
    case_result.h_solid=result_solid.get_h();
    case_result.h_air=result_air.get_h();
    case_result.expected_h=expected_h;
    case_result.legacy_h=legacy_h;
    case_result.estimator_h=estimator_h;
    case_result.frozen_capacity_energy_change=
        solid_capacity*(result_solid.get_T()-kSolidTemperature)+
        air_capacity*(result_air.get_T()-kAirTemperature);
    return case_result;
}

void verify_case(bool adaptive,int axis,double forced_velocity) {
    const CaseResult result=run_case(adaptive,axis,forced_velocity);
    const std::string label=std::string(adaptive ? "adaptive" : "uniform")+
        " axis "+std::to_string(axis)+
        (forced_velocity > 0.0 ? " forced" : " natural");
    const double h_tolerance=std::max(1.0e-12,1.0e-12*result.expected_h);
    require(std::abs(result.h_solid-result.h_air)<=h_tolerance,
            label+" computed different h values on the two sides of one face");
    require(std::abs(result.h_solid-result.expected_h)<=h_tolerance,
            label+" did not use the air cell's width and properties");
    require(std::abs(result.estimator_h-result.expected_h)<=h_tolerance,
            label+" thermal estimator disagrees with the solver face h");
    require(std::abs(result.frozen_capacity_energy_change)<=1.0e-9,
            label+" created or destroyed energy across an internal face");
    require(std::abs(result.h_solid-result.legacy_h)>1.0e-4,
            label+" unexpectedly matches the legacy sea-level correlation");
}

} // namespace

int main() {
    try {
        std::size_t cases=0;
        for(const bool adaptive : {false,true})
            for(int axis=0;axis<3;++axis)
                for(const double velocity : {0.0,0.8}) {
                    verify_case(adaptive,axis,velocity);
                    ++cases;
                }
        std::cout << "adaptive_convection_conservation_test PASSED: "
                  << cases
                  << " all-axis natural/forced cases with property fidelity "
                     "and frozen-capacity energy closure\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "adaptive_convection_conservation_test FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}

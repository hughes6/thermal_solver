#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/solver.hpp"

namespace {

constexpr double kAirTemperature = 20.0;
constexpr double kSolidTemperature = 40.0;
constexpr double kAirRho = 0.9833;
constexpr double kAirCp = 1005.5;
constexpr double kAirK = 0.02587;
constexpr double kAirMu = 1.81e-5;
constexpr double kAirPr = 0.71;

struct TemporaryCsv {
    std::filesystem::path path;
    ~TemporaryCsv() {
        std::error_code ignored;
        std::filesystem::remove(path,ignored);
    }
};

struct Fixture {
    Mesh mesh;
    double stable_dt = 0.0;
    std::array<int,3> limiting_cell{0,0,0};
};

void require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

double width_on_axis(const Cell& cell,int axis) {
    if(axis == 0) return cell.get_dx();
    if(axis == 1) return cell.get_dy();
    return cell.get_dz();
}

double area_on_axis(const Cell& cell,int axis) {
    if(axis == 0) return cell.area_x();
    if(axis == 1) return cell.area_y();
    return cell.area_z();
}

std::array<int,3> coordinate_on_axis(int axis,int index) {
    std::array<int,3> result{0,0,0};
    result[axis]=index;
    return result;
}

double bounded_face_h(const Cell& air,const Cell& solid,int axis) {
    const double velocity=std::sqrt(
        air.get_vx()*air.get_vx()+
        air.get_vy()*air.get_vy()+
        air.get_vz()*air.get_vz());
    const double delta_temperature=std::max(
        std::abs(solid.get_T()-air.get_T()),80.0);
    const double film_temperature=
        0.5*(solid.get_T()+air.get_T())+273.15;
    return Convection::compute_local_h(
        velocity,width_on_axis(air,axis),air.get_rho(),air.get_mu(),
        air.get_k(),air.get_pr(),delta_temperature,film_temperature);
}

Rack make_rack(const std::array<std::vector<double>,3>& widths,
               const std::string& name) {
    std::array<double,3> extent{0.0,0.0,0.0};
    for(int axis=0;axis<3;++axis)
        for(const double width:widths[axis]) extent[axis]+=width;
    Rack rack=Rack::from_meters(extent[0],extent[1],extent[2],name);
    rack.set_t(kAirTemperature);
    rack.set_cp(kAirCp);
    rack.set_k(kAirK);
    rack.set_rho(kAirRho);
    return rack;
}

Environment make_environment() {
    return Environment(
        30.0,5500.0,kAirTemperature,kAirCp,kAirK,kAirMu,kAirPr,kAirRho);
}

Fixture make_unequal_single_face_fixture(int axis) {
    std::array<std::vector<double>,3> widths{
        std::vector<double>{0.04},
        std::vector<double>{0.04},
        std::vector<double>{0.04}};
    widths[axis]={0.019,0.050};
    Workload workload(100,10000,1000,16);
    Mesh mesh=Mesh().build_adaptive_mesh(
        make_rack(widths,"unequal convection stability"),
        widths[0],widths[1],widths[2],make_environment(),workload);

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
    // Keep the fixture's unrelated conduction diagnostic below its warning
    // threshold so this test isolates the convection guard.
    solid.set_k(0.1);
    air.set_T(kAirTemperature);

    const double h=bounded_face_h(air,solid,axis);
    const double conductance=h*area_on_axis(air,axis);
    const double capacity=air.get_rho()*air.get_cp()*air.volume();
    require(std::isfinite(h) && h > 0.0,
            "single-face fixture has invalid bounded h");
    require(std::isfinite(capacity/conductance) && capacity/conductance > 0.0,
            "single-face fixture has invalid stable dt");
    return {std::move(mesh),capacity/conductance,air_coordinate};
}

Fixture make_six_face_fixture() {
    std::array<std::vector<double>,3> widths{
        std::vector<double>{0.03,0.03,0.03},
        std::vector<double>{0.03,0.03,0.03},
        std::vector<double>{0.03,0.03,0.03}};
    Workload workload(100,100000,1000,16);
    Mesh mesh=Mesh().build_mesh(
        make_rack(widths,"six-face convection stability"),
        0.03,0.03,0.03,make_environment(),workload);
    const std::array<int,3> center{1,1,1};
    static const int offsets[6][3]{
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(const auto& offset:offsets) {
        Cell& solid=mesh.at(
            center[0]+offset[0],center[1]+offset[1],center[2]+offset[2]);
        solid.set_state(Cell::State::Component);
        solid.set_T(kSolidTemperature);
        solid.set_rho(2700.0);
        solid.set_cp(900.0);
        solid.set_k(0.1);
    }
    Cell& air=mesh.at(center[0],center[1],center[2]);
    air.set_T(kAirTemperature);
    const Cell& x_solid=mesh.at(2,1,1);
    const double h=bounded_face_h(air,x_solid,0);
    const double conductance=6.0*h*air.area_x();
    const double capacity=air.get_rho()*air.get_cp()*air.volume();
    require(std::isfinite(h) && h > 0.0,
            "six-face fixture has invalid bounded h");
    return {std::move(mesh),capacity/conductance,center};
}

double parse_max_c(const std::string& message) {
    const std::string prefix="max C=";
    const std::size_t start=message.find(prefix);
    require(start != std::string::npos,"guard error omitted max C");
    const std::size_t end=message.find(" > 1",start+prefix.size());
    require(end != std::string::npos,"guard error has malformed max C");
    return std::stod(message.substr(start+prefix.size(),
                                    end-(start+prefix.size())));
}

void run_guard_case(Fixture fixture,double factor,bool expect_throw,
                    const std::string& label) {
    const double dt=factor*fixture.stable_dt;
    std::vector<double> initial_temperatures;
    initial_temperatures.reserve(fixture.mesh.get_cell_count());
    for(const Cell& cell:fixture.mesh.get_cells())
        initial_temperatures.push_back(cell.get_T());

    TemporaryCsv csv{
        std::filesystem::temp_directory_path()/
        ("thermal_convection_stability_"+label+".csv")};
    Solver solver(
        std::move(fixture.mesh),dt,dt,false,1,-1,
        4.5,1e-3,10,1.3,2,1e-2,false,0.8,10000,
        csv.path.string());
    bool threw=false;
    std::string message;
    try {
        solver.solve();
    } catch(const std::runtime_error& error) {
        threw=true;
        message=error.what();
    }
    require(threw == expect_throw,
            label+(expect_throw ? " did not reject unsafe dt" :
                                  " rejected safe dt: "+message));

    if(expect_throw) {
        require(message.find("explicit convection stability limit exceeded") !=
                    std::string::npos,
                label+" threw for the wrong reason: "+message);
        require(message.find("dt=") != std::string::npos &&
                message.find("maximum stable dt=") != std::string::npos &&
                message.find("Reduce simulation.dt") != std::string::npos &&
                message.find("thermal advancement was refused") !=
                    std::string::npos,
                label+" guard error is not actionable: "+message);
        const std::string coordinate="cell ("+
            std::to_string(fixture.limiting_cell[0])+","+
            std::to_string(fixture.limiting_cell[1])+","+
            std::to_string(fixture.limiting_cell[2])+")";
        require(message.find(coordinate) != std::string::npos,
                label+" reported the wrong limiting cell: "+message);
        require(std::abs(parse_max_c(message)-factor) <= 1.0e-11,
                label+" did not use the complete face row sum");
        const Mesh& unchanged=solver.get_mesh();
        std::size_t index=0;
        for(const Cell& cell:unchanged.get_cells()) {
            require(cell.get_T() == initial_temperatures[index],
                    label+" changed temperature before rejecting unsafe dt");
            ++index;
        }
    }
}

} // namespace

int main() {
    try {
        std::size_t cases=0;
        for(int axis=0;axis<3;++axis) {
            run_guard_case(
                make_unequal_single_face_fixture(axis),0.75,false,
                "unequal_axis_"+std::to_string(axis)+"_safe");
            ++cases;
            run_guard_case(
                make_unequal_single_face_fixture(axis),1.25,true,
                "unequal_axis_"+std::to_string(axis)+"_unsafe");
            ++cases;
        }
        run_guard_case(make_six_face_fixture(),0.75,false,"six_face_safe");
        ++cases;
        run_guard_case(make_six_face_fixture(),1.25,true,"six_face_unsafe");
        ++cases;
        std::cout << "convection_stability_guard_test PASSED: " << cases
                  << " safe/unsafe unequal all-axis and six-face cases\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "convection_stability_guard_test FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}

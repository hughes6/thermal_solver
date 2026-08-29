#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/solver.hpp"

namespace {

constexpr double kDt = 2.0e-3;
constexpr int kSteps = 5;
constexpr double kBoundaryFlowM3s = 3.0e-4;

struct TemporaryCsv {
    std::filesystem::path path;
    ~TemporaryCsv() {
        std::error_code ignored;
        std::filesystem::remove(path,ignored);
    }
};

TemporaryCsv make_temporary_csv() {
    const auto nonce=std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return {
        std::filesystem::temp_directory_path()/
        ("native_multiaxis_persistent_ledger_"+
         std::to_string(nonce)+".csv")};
}

struct SnapshotCell {
    double temperature_C = 0.0;
    double qdot_Wm3 = 0.0;
    double conductivity_WmK = 0.0;
    double density_kgm3 = 0.0;
    double cp_JkgK = 0.0;
};

using Snapshot = std::vector<SnapshotCell>;

struct FlowFieldSnapshot {
    std::vector<double> x_face_m3s;
    std::vector<double> y_face_m3s;
    std::vector<double> z_face_m3s;
    std::vector<double> ambient_m3s;
};

struct Evidence {
    std::array<double,3> minimum_axis_flux_sum_m3s{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array<double,3> minimum_axis_transfer_W{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    double source_energy_J = 0.0;
    double boundary_net_energy_J = 0.0;
    double storage_energy_J = 0.0;
    double maximum_cell_residual_J = 0.0;
    double maximum_global_residual_J = 0.0;
    double maximum_continuity_residual_m3s = 0.0;
    double minimum_boundary_inlet_W =
        std::numeric_limits<double>::infinity();
    double minimum_boundary_outlet_W =
        std::numeric_limits<double>::infinity();
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition,const std::string& message) {
    if(!condition) fail(message);
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while(std::getline(stream,field,',')) fields.push_back(field);
    return fields;
}

Mesh make_fixture() {
    constexpr double ambient_temperature_C = 18.0;
    constexpr double air_cp_JkgK = 1005.5;
    constexpr double air_k_WmK = 0.02587;
    constexpr double air_mu_Pas = 1.81e-5;
    constexpr double air_pr = 0.71;
    constexpr double air_rho_kgm3 = 0.9833;

    Rack rack=Rack::from_meters(0.30,0.30,0.30,
                                "persistent multiaxis ledger");
    rack.set_t(ambient_temperature_C);
    rack.set_cp(air_cp_JkgK);
    rack.set_k(air_k_WmK);
    rack.set_rho(air_rho_kgm3);
    Environment environment(
        30.0,5500.0,ambient_temperature_C,air_cp_JkgK,air_k_WmK,
        air_mu_Pas,air_pr,air_rho_kgm3);
    Workload workload(100,10000,1000,16);
    Mesh mesh=Mesh().build_mesh(
        rack,0.10,0.10,0.10,environment,workload);

    // A diagonal source/sink pair drives a genuinely three-dimensional
    // pressure field.  The zero-net source network still receives an explicit
    // ambient ground through a third vent cell, satisfying the flow topology
    // preflight without adding a material boundary-energy term.
    Cell& intake=mesh.at(0,0,0);
    intake.set_state(Cell::State::Intake);
    intake.set_flow_source(kBoundaryFlowM3s);
    Cell& exhaust=mesh.at(2,2,2);
    exhaust.set_state(Cell::State::Exhaust);
    exhaust.set_flow_source(-kBoundaryFlowM3s);
    Cell& reference_vent=mesh.at(0,2,0);
    reference_vent.set_state(Cell::State::Vent);
    reference_vent.set_vent_conductance(0.01);

    for(int x=0;x<mesh.get_nx();++x)
        for(int y=0;y<mesh.get_ny();++y)
            for(int z=0;z<mesh.get_nz();++z) {
                Cell& cell=mesh.at(x,y,z);
                cell.set_T(21.0+2.0*x+3.0*y+5.0*z+
                           0.25*x*y+0.15*y*z);
                cell.set_qdot(800.0+110.0*x+170.0*y+230.0*z);
            }
    return mesh;
}

std::vector<Snapshot> read_snapshots(
    const std::filesystem::path& path,const Mesh& geometry) {
    std::ifstream input(path);
    require(static_cast<bool>(input),
            "could not read persistent solver CSV output");
    std::vector<Snapshot> snapshots(
        static_cast<std::size_t>(kSteps+1),
        Snapshot(geometry.get_cell_count()));
    std::vector<std::vector<unsigned char>> seen(
        static_cast<std::size_t>(kSteps+1),
        std::vector<unsigned char>(geometry.get_cell_count(),0));

    std::string line;
    while(std::getline(input,line)) {
        const std::vector<std::string> fields=split_csv(line);
        if(fields.empty() || fields[0] == "step" || fields[0] == "dx")
            continue;
        require(fields.size() == 15,
                "unexpected persistent solver CSV row width");
        const int step=std::stoi(fields[0]);
        const double time_s=std::stod(fields[1]);
        const int x=std::stoi(fields[2]);
        const int y=std::stoi(fields[3]);
        const int z=std::stoi(fields[4]);
        require(step >= 0 && step <= kSteps,
                "persistent solver CSV contains an unexpected step");
        require(std::abs(time_s-step*kDt) <= 1.0e-15,
                "persistent solver CSV time is inconsistent with its step");
        require(geometry.in_bounds(x,y,z),
                "persistent solver CSV contains an invalid cell index");
        const std::size_t index=geometry.idx(x,y,z);
        require(seen[static_cast<std::size_t>(step)][index] == 0,
                "persistent solver CSV contains a duplicate cell row");
        seen[static_cast<std::size_t>(step)][index]=1;
        SnapshotCell& cell=snapshots[static_cast<std::size_t>(step)][index];
        cell.temperature_C=std::stod(fields[5]);
        cell.qdot_Wm3=std::stod(fields[6]);
        cell.conductivity_WmK=std::stod(fields[8]);
        cell.density_kgm3=std::stod(fields[9]);
        cell.cp_JkgK=std::stod(fields[10]);
    }
    for(int step=0;step<=kSteps;++step)
        for(std::size_t index=0;index<geometry.get_cell_count();++index)
            require(seen[static_cast<std::size_t>(step)][index] != 0,
                    "persistent solver CSV is missing a cell transition row");
    return snapshots;
}

FlowFieldSnapshot capture_flow_field(
    const Mesh& geometry,const Solver& solver) {
    FlowFieldSnapshot snapshot;
    for(int x=0;x<=geometry.get_nx();++x)
        for(int y=0;y<geometry.get_ny();++y)
            for(int z=0;z<geometry.get_nz();++z)
                snapshot.x_face_m3s.push_back(
                    solver.x_face_flux_m3s(x,y,z));
    for(int x=0;x<geometry.get_nx();++x)
        for(int y=0;y<=geometry.get_ny();++y)
            for(int z=0;z<geometry.get_nz();++z)
                snapshot.y_face_m3s.push_back(
                    solver.y_face_flux_m3s(x,y,z));
    for(int x=0;x<geometry.get_nx();++x)
        for(int y=0;y<geometry.get_ny();++y)
            for(int z=0;z<=geometry.get_nz();++z)
                snapshot.z_face_m3s.push_back(
                    solver.z_face_flux_m3s(x,y,z));
    for(int x=0;x<geometry.get_nx();++x)
        for(int y=0;y<geometry.get_ny();++y)
            for(int z=0;z<geometry.get_nz();++z)
                snapshot.ambient_m3s.push_back(
                    solver.ambient_flow_into_cell_m3s(x,y,z));
    return snapshot;
}

void require_identical_flow_field(
    const FlowFieldSnapshot& before,const FlowFieldSnapshot& after) {
    auto require_vector_equal = [](
        const std::vector<double>& first,const std::vector<double>& second,
        const char* label) {
        require(first.size() == second.size(),
                std::string("persistent ")+label+" field size changed");
        for(std::size_t index=0;index<first.size();++index)
            require(first[index] == second[index],
                    std::string("persistent ")+label+
                    " field changed during the thermal-only transitions");
    };
    require_vector_equal(
        before.x_face_m3s,after.x_face_m3s,"x-face flux");
    require_vector_equal(
        before.y_face_m3s,after.y_face_m3s,"y-face flux");
    require_vector_equal(
        before.z_face_m3s,after.z_face_m3s,"z-face flux");
    require_vector_equal(
        before.ambient_m3s,after.ambient_m3s,"ambient flow");
}

double face_area(const Cell& cell,int axis) {
    if(axis == 0) return cell.area_x();
    if(axis == 1) return cell.area_y();
    return cell.area_z();
}

double face_distance(const Cell& low,const Cell& high,int axis) {
    if(axis == 0) return 0.5*(low.get_dx()+high.get_dx());
    if(axis == 1) return 0.5*(low.get_dy()+high.get_dy());
    return 0.5*(low.get_dz()+high.get_dz());
}

double axis_face_flux(
    const Solver& solver,int axis,int x,int y,int z) {
    if(axis == 0) return solver.x_face_flux_m3s(x,y,z);
    if(axis == 1) return solver.y_face_flux_m3s(x,y,z);
    return solver.z_face_flux_m3s(x,y,z);
}

std::array<int,3> shifted(
    int x,int y,int z,int axis,int amount) {
    std::array<int,3> coordinate{x,y,z};
    coordinate[axis] += amount;
    return coordinate;
}

Evidence audit_persistent_run(
    const Mesh& geometry,const Solver& solver,
    const std::vector<Snapshot>& snapshots) {
    Evidence evidence;
    const std::size_t count=geometry.get_cell_count();

    for(int step=0;step<kSteps;++step) {
        const Snapshot& before=snapshots[static_cast<std::size_t>(step)];
        const Snapshot& after=snapshots[static_cast<std::size_t>(step+1)];
        std::vector<double> conduction_W(count,0.0);
        std::vector<double> advection_W(count,0.0);
        std::vector<double> boundary_W(count,0.0);
        std::vector<double> generation_W(count,0.0);
        std::array<double,3> axis_flux_sum_m3s{};
        std::array<double,3> axis_transfer_W{};

        // Visit each positive-coordinate face once for conservative
        // conduction and independent per-axis flow/energy diagnostics.
        for(int x=0;x<geometry.get_nx();++x)
            for(int y=0;y<geometry.get_ny();++y)
                for(int z=0;z<geometry.get_nz();++z) {
                    const std::size_t low_index=geometry.idx(x,y,z);
                    generation_W[low_index]=
                        before[low_index].qdot_Wm3*
                        geometry.at(x,y,z).volume();
                    for(int axis=0;axis<3;++axis) {
                        const auto high_coordinate=shifted(
                            x,y,z,axis,1);
                        if(!geometry.in_bounds(
                               high_coordinate[0],high_coordinate[1],
                               high_coordinate[2]))
                            continue;
                        const std::size_t high_index=geometry.idx(
                            high_coordinate[0],high_coordinate[1],
                            high_coordinate[2]);
                        const double low_k=
                            before[low_index].conductivity_WmK;
                        const double high_k=
                            before[high_index].conductivity_WmK;
                        const double harmonic_k=
                            2.0*low_k*high_k/(low_k+high_k);
                        const double into_low=harmonic_k*
                            face_area(geometry.at(x,y,z),axis)*
                            (before[high_index].temperature_C-
                             before[low_index].temperature_C)/
                            face_distance(
                                geometry.at(x,y,z),
                                geometry.at(
                                    high_coordinate[0],high_coordinate[1],
                                    high_coordinate[2]),axis);
                        conduction_W[low_index] += into_low;
                        conduction_W[high_index] -= into_low;

                        const int face_x=x+(axis == 0);
                        const int face_y=y+(axis == 1);
                        const int face_z=z+(axis == 2);
                        const double flow=axis_face_flux(
                            solver,axis,face_x,face_y,face_z);
                        axis_flux_sum_m3s[static_cast<std::size_t>(axis)] +=
                            std::abs(flow);
                        const std::size_t upstream_index=
                            flow >= 0.0 ? low_index : high_index;
                        const SnapshotCell& upstream=before[upstream_index];
                        axis_transfer_W[static_cast<std::size_t>(axis)] +=
                            std::abs(flow)*upstream.density_kgm3*
                            upstream.cp_JkgK*upstream.temperature_C;
                    }
                }

        auto add_advective_face = [&](int x,int y,int z,
                                       double outward_flow,
                                       int nx,int ny,int nz) {
            if(outward_flow == 0.0) return;
            const std::size_t cell_index=geometry.idx(x,y,z);
            const std::size_t upstream_index=outward_flow < 0.0
                ? geometry.idx(nx,ny,nz) : cell_index;
            const SnapshotCell& upstream=before[upstream_index];
            advection_W[cell_index] -= outward_flow*
                upstream.density_kgm3*upstream.cp_JkgK*
                upstream.temperature_C;
        };

        double boundary_inlet_W=0.0;
        double boundary_outlet_W=0.0;
        double reconstructed_continuity_residual_m3s=0.0;
        for(int x=0;x<geometry.get_nx();++x)
            for(int y=0;y<geometry.get_ny();++y)
                for(int z=0;z<geometry.get_nz();++z) {
                    const std::size_t index=geometry.idx(x,y,z);
                    add_advective_face(
                        x,y,z,-solver.x_face_flux_m3s(x,y,z),x-1,y,z);
                    add_advective_face(
                        x,y,z,solver.x_face_flux_m3s(x+1,y,z),x+1,y,z);
                    add_advective_face(
                        x,y,z,-solver.y_face_flux_m3s(x,y,z),x,y-1,z);
                    add_advective_face(
                        x,y,z,solver.y_face_flux_m3s(x,y+1,z),x,y+1,z);
                    add_advective_face(
                        x,y,z,-solver.z_face_flux_m3s(x,y,z),x,y,z-1);
                    add_advective_face(
                        x,y,z,solver.z_face_flux_m3s(x,y,z+1),x,y,z+1);

                    const double ambient_flow=
                        solver.ambient_flow_into_cell_m3s(x,y,z);
                    if(ambient_flow != 0.0) {
                        const double transported=ambient_flow > 0.0
                            ? geometry.get_env().get_rho()*
                                  geometry.get_env().get_cp()*
                                  geometry.get_env().get_T_ambient()
                            : before[index].density_kgm3*
                                  before[index].cp_JkgK*
                                  before[index].temperature_C;
                        boundary_W[index]=ambient_flow*transported;
                        if(boundary_W[index] > 0.0)
                            boundary_inlet_W += boundary_W[index];
                        else
                            boundary_outlet_W += -boundary_W[index];
                    }

                    const double outward=
                        -solver.x_face_flux_m3s(x,y,z)+
                         solver.x_face_flux_m3s(x+1,y,z)-
                         solver.y_face_flux_m3s(x,y,z)+
                         solver.y_face_flux_m3s(x,y+1,z)-
                         solver.z_face_flux_m3s(x,y,z)+
                         solver.z_face_flux_m3s(x,y,z+1);
                    reconstructed_continuity_residual_m3s=std::max(
                        reconstructed_continuity_residual_m3s,
                        std::abs(ambient_flow-outward));
                }

        require(std::abs(
                    reconstructed_continuity_residual_m3s-
                    solver.maximum_flow_continuity_residual_m3s()) <= 1e-15,
                "independently reconstructed continuity residual differs "
                "from the published persistent-flow diagnostic");
        evidence.maximum_continuity_residual_m3s=std::max(
            evidence.maximum_continuity_residual_m3s,
            reconstructed_continuity_residual_m3s);

        double conduction_sum_W=0.0;
        double advection_sum_W=0.0;
        double step_source_J=0.0;
        double step_boundary_J=0.0;
        double step_storage_J=0.0;
        double step_expected_J=0.0;
        for(std::size_t index=0;index<count;++index) {
            conduction_sum_W += conduction_W[index];
            advection_sum_W += advection_W[index];
            const double capacity_JK=before[index].density_kgm3*
                before[index].cp_JkgK*geometry.get_cells()[index].volume();
            const double actual_J=capacity_JK*
                (after[index].temperature_C-before[index].temperature_C);
            const double expected_J=kDt*(
                conduction_W[index]+advection_W[index]+boundary_W[index]+
                generation_W[index]);
            const double residual_J=std::abs(actual_J-expected_J);
            evidence.maximum_cell_residual_J=std::max(
                evidence.maximum_cell_residual_J,residual_J);
            require(residual_J <= 3.0e-9*std::max(
                        {1.0,std::abs(actual_J),std::abs(expected_J)}),
                    "persistent per-cell frozen-capacity ledger residual "
                    "exceeds tolerance at step "+std::to_string(step)+
                    ", linear cell "+std::to_string(index));
            step_storage_J += actual_J;
            step_expected_J += expected_J;
            step_source_J += kDt*generation_W[index];
            step_boundary_J += kDt*boundary_W[index];
        }
        require(std::abs(conduction_sum_W) <= 1.0e-12,
                "persistent internal conduction did not cancel");
        require(std::abs(advection_sum_W) <= 1.0e-10,
                "persistent internal advection did not cancel");
        require(std::abs(step_storage_J-step_expected_J) <= 5.0e-9,
                "persistent summed cell ledger does not match storage");
        const double global_residual_J=
            step_storage_J-(step_source_J+step_boundary_J);
        evidence.maximum_global_residual_J=std::max(
            evidence.maximum_global_residual_J,
            std::abs(global_residual_J));
        require(std::abs(global_residual_J) <= 5.0e-9,
                "persistent global frozen-capacity ledger does not close at "
                "step "+std::to_string(step));

        for(int axis=0;axis<3;++axis) {
            evidence.minimum_axis_flux_sum_m3s[axis]=std::min(
                evidence.minimum_axis_flux_sum_m3s[axis],
                axis_flux_sum_m3s[axis]);
            evidence.minimum_axis_transfer_W[axis]=std::min(
                evidence.minimum_axis_transfer_W[axis],
                axis_transfer_W[axis]);
        }
        evidence.minimum_boundary_inlet_W=std::min(
            evidence.minimum_boundary_inlet_W,boundary_inlet_W);
        evidence.minimum_boundary_outlet_W=std::min(
            evidence.minimum_boundary_outlet_W,boundary_outlet_W);
        evidence.source_energy_J += step_source_J;
        evidence.boundary_net_energy_J += step_boundary_J;
        evidence.storage_energy_J += step_storage_J;
    }

    for(int axis=0;axis<3;++axis) {
        require(evidence.minimum_axis_flux_sum_m3s[axis] > 1.0e-6,
                "persistent fixture did not exercise nonzero face flux on "
                "axis "+std::to_string(axis));
        require(evidence.minimum_axis_transfer_W[axis] > 1.0,
                "persistent fixture did not exercise advective energy "
                "transport on axis "+std::to_string(axis));
    }
    require(evidence.minimum_boundary_inlet_W > 1.0 &&
            evidence.minimum_boundary_outlet_W > 1.0,
            "persistent fixture did not exercise both ambient inlet and "
            "outlet enthalpy terms");
    require(evidence.maximum_continuity_residual_m3s <= 1.0e-10,
            "persistent flow continuity is too loose for the ledger");
    require(std::abs(
                evidence.storage_energy_J-
                (evidence.source_energy_J+
                 evidence.boundary_net_energy_J)) <= 1.0e-8,
            "cumulative persistent frozen-capacity ledger does not close");
    return evidence;
}

} // namespace

int main() {
    try {
        TemporaryCsv csv=make_temporary_csv();
        Mesh fixture=make_fixture();
        Solver solver(
            fixture,kDt,kSteps*kDt,false,1,-1,
            4.6,1.0e-12,3000,1.1,100,1.0e-8,
            false,0.8,10000,csv.path.string(),"pcg",false);
        solver.initialize_flow();
        const Mesh geometry=solver.get_mesh();
        const FlowFieldSnapshot initial_flow=
            capture_flow_field(geometry,solver);
        solver.solve();
        const FlowFieldSnapshot final_flow=
            capture_flow_field(geometry,solver);
        require_identical_flow_field(initial_flow,final_flow);
        const std::vector<Snapshot> snapshots=
            read_snapshots(csv.path,geometry);
        const Mesh& final_mesh=solver.get_mesh();
        const Snapshot& final_snapshot=snapshots.back();
        for(std::size_t index=0;index<geometry.get_cell_count();++index) {
            require(final_snapshot[index].temperature_C ==
                        final_mesh.get_cells()[index].get_T(),
                    "final logged temperature differs from Solver state");
            require(final_snapshot[index].density_kgm3 ==
                        final_mesh.get_cells()[index].get_rho(),
                    "final logged density differs from Solver state");
        }
        const Evidence evidence=
            audit_persistent_run(geometry,solver,snapshots);

        std::cout << std::setprecision(12)
                  << "persistent multiaxis ledger: solver_instances=1, "
                     "solve_calls=1, transitions=" << kSteps
                  << ", axis_flux_sum_min=["
                  << evidence.minimum_axis_flux_sum_m3s[0] << ','
                  << evidence.minimum_axis_flux_sum_m3s[1] << ','
                  << evidence.minimum_axis_flux_sum_m3s[2] << "] m^3/s"
                  << ", axis_transfer_min=["
                  << evidence.minimum_axis_transfer_W[0] << ','
                  << evidence.minimum_axis_transfer_W[1] << ','
                  << evidence.minimum_axis_transfer_W[2] << "] W"
                  << ", source=" << evidence.source_energy_J << " J"
                  << ", boundary-net="
                  << evidence.boundary_net_energy_J << " J"
                  << ", storage=" << evidence.storage_energy_J << " J"
                  << ", max-cell-residual="
                  << evidence.maximum_cell_residual_J << " J"
                  << ", max-global-residual="
                  << evidence.maximum_global_residual_J << " J"
                  << ", max-continuity-residual="
                  << evidence.maximum_continuity_residual_m3s
                  << " m^3/s\n";
        std::cout
            << "native_multiaxis_persistent_ledger_test PASSED: one "
               "persistent Solver advanced five logged transitions with "
               "nonzero x/y/z face-flux advection and discrete "
               "beginning-step frozen-capacity closure\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "native_multiaxis_persistent_ledger_test FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}

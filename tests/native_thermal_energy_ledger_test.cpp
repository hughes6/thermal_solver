#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/convection.hpp"
#include "../src/solver.hpp"

namespace {

constexpr double kDt = 2.0e-3;
constexpr int kSteps = 4;
constexpr double kFanFlowM3s = 4.0e-4;

struct TemporaryCsv {
    std::filesystem::path path;
    ~TemporaryCsv() {
        std::error_code ignored;
        std::filesystem::remove(path,ignored);
    }
};

struct MechanismLedger {
    std::vector<double> conduction_W;
    std::vector<double> convection_W;
    std::vector<double> advection_internal_W;
    std::vector<double> advection_boundary_W;
    std::vector<double> generation_W;
    double solid_conduction_transfer_W = 0.0;
    double fluid_conduction_transfer_W = 0.0;
    double convection_transfer_W = 0.0;
    double internal_advection_transfer_W = 0.0;
    double boundary_inlet_W = 0.0;
    double boundary_outlet_W = 0.0;
};

struct CaseEvidence {
    double source_energy_J = 0.0;
    double storage_energy_J = 0.0;
    double boundary_inlet_energy_J = 0.0;
    double boundary_outlet_energy_J = 0.0;
    double boundary_net_energy_J = 0.0;
    double maximum_cell_residual_J = 0.0;
    double maximum_global_residual_J = 0.0;
    double maximum_continuity_residual_m3s = 0.0;
    double minimum_solid_conduction_transfer_W =
        std::numeric_limits<double>::infinity();
    double minimum_fluid_conduction_transfer_W =
        std::numeric_limits<double>::infinity();
    double minimum_convection_transfer_W =
        std::numeric_limits<double>::infinity();
    double minimum_internal_advection_transfer_W =
        std::numeric_limits<double>::infinity();
    double minimum_boundary_inlet_W =
        std::numeric_limits<double>::infinity();
    double minimum_boundary_outlet_W =
        std::numeric_limits<double>::infinity();
};

void require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

double face_area(const Cell& cell,int axis) {
    if(axis == 0) return cell.area_x();
    if(axis == 1) return cell.area_y();
    return cell.area_z();
}

double width(const Cell& cell,int axis) {
    if(axis == 0) return cell.get_dx();
    if(axis == 1) return cell.get_dy();
    return cell.get_dz();
}

std::array<int,3> shifted(int x,int y,int z,int axis,int amount) {
    std::array<int,3> result{x,y,z};
    result[axis] += amount;
    return result;
}

Mesh make_fixture(bool adaptive) {
    constexpr double ambient_temperature = 18.0;
    constexpr double air_cp = 1005.5;
    constexpr double air_k = 0.02587;
    constexpr double air_mu = 1.81e-5;
    constexpr double air_pr = 0.71;
    constexpr double air_rho = 0.9833;
    Rack rack = Rack::from_meters(
        0.4,0.2,0.1,
        adaptive ? "adaptive energy ledger" : "uniform energy ledger");
    rack.set_t(ambient_temperature);
    rack.set_cp(air_cp);
    rack.set_k(air_k);
    rack.set_rho(air_rho);
    Environment environment(
        30.0,5500.0,ambient_temperature,air_cp,air_k,air_mu,air_pr,
        air_rho);
    Workload workload(100,10000,1000,16);

    Mesh mesh;
    if(adaptive) {
        mesh = Mesh().build_adaptive_mesh(
            rack,{0.08,0.11,0.13,0.08},{0.09,0.11},{0.10},
            environment,workload);
    } else {
        mesh = Mesh().build_mesh(
            rack,0.10,0.10,0.10,environment,workload);
    }

    constexpr std::array<double,4> air_temperature{22.0,24.0,27.0,31.0};
    constexpr std::array<double,4> solid_temperature{72.0,61.0,53.0,43.0};
    constexpr std::array<double,4> solid_k{7.0,13.0,19.0,5.0};
    constexpr std::array<double,4> source_qdot{25000.0,0.0,17000.0,0.0};
    for(int x=0;x<4;++x) {
        Cell& air=mesh.at(x,0,0);
        air.set_T(air_temperature[static_cast<std::size_t>(x)]);

        Cell& solid=mesh.at(x,1,0);
        solid.set_state(Cell::State::Component);
        solid.set_T(solid_temperature[static_cast<std::size_t>(x)]);
        solid.set_rho(2700.0);
        solid.set_cp(900.0);
        solid.set_k(solid_k[static_cast<std::size_t>(x)]);
        solid.set_qdot(source_qdot[static_cast<std::size_t>(x)]);
    }
    for(const int x : {0,3}) {
        Cell& vent=mesh.at(x,0,0);
        vent.set_state(Cell::State::Vent);
        vent.set_vent_conductance(0.01);
    }
    mesh.get_internal_fans().push_back(
        {{1,0,0},{2,0,0},kFanFlowM3s,{1.0,0.0,0.0},
         0.0,0.0,0.0,air_rho,kFanFlowM3s});
    return mesh;
}

MechanismLedger reconstruct_ledger(
    const Mesh& mesh,const Solver& solver) {
    MechanismLedger ledger;
    const std::size_t count=mesh.get_cell_count();
    ledger.conduction_W.assign(count,0.0);
    ledger.convection_W.assign(count,0.0);
    ledger.advection_internal_W.assign(count,0.0);
    ledger.advection_boundary_W.assign(count,0.0);
    ledger.generation_W.assign(count,0.0);

    // Visit each internal face once.  Equal-and-opposite entries are kept in
    // separate cell rows so both per-cell advancement and global cancellation
    // are independently checked below.
    for(int x=0;x<mesh.get_nx();++x)
        for(int y=0;y<mesh.get_ny();++y)
            for(int z=0;z<mesh.get_nz();++z) {
                const Cell& cell=mesh.at(x,y,z);
                const std::size_t index=mesh.idx(x,y,z);
                ledger.generation_W[index]=cell.get_qdot()*cell.volume();
                for(int axis=0;axis<3;++axis) {
                    const auto neighbor_coordinate=shifted(x,y,z,axis,1);
                    if(!mesh.in_bounds(
                           neighbor_coordinate[0],neighbor_coordinate[1],
                           neighbor_coordinate[2]))
                        continue;
                    const Cell& neighbor=mesh.at(
                        neighbor_coordinate[0],neighbor_coordinate[1],
                        neighbor_coordinate[2]);
                    const std::size_t neighbor_index=mesh.idx(
                        neighbor_coordinate[0],neighbor_coordinate[1],
                        neighbor_coordinate[2]);
                    const double area=face_area(cell,axis);

                    if(cell.is_solid() == neighbor.is_solid()) {
                        const double distance=
                            0.5*(width(cell,axis)+width(neighbor,axis));
                        const double face_k=
                            2.0*cell.get_k()*neighbor.get_k()/
                            (cell.get_k()+neighbor.get_k());
                        const double into_cell=face_k*area*
                            (neighbor.get_T()-cell.get_T())/distance;
                        ledger.conduction_W[index] += into_cell;
                        ledger.conduction_W[neighbor_index] -= into_cell;
                        if(cell.is_solid())
                            ledger.solid_conduction_transfer_W +=
                                std::abs(into_cell);
                        else
                            ledger.fluid_conduction_transfer_W +=
                                std::abs(into_cell);
                        continue;
                    }

                    const Cell& fluid=cell.is_solid() ? neighbor : cell;
                    const Cell& solid=cell.is_solid() ? cell : neighbor;
                    const double film_temperature_K=
                        0.5*(solid.get_T()+fluid.get_T())+273.15;
                    const double h=Convection::compute_local_h(
                        fluid.get_vmag(),width(fluid,axis),fluid.get_rho(),
                        fluid.get_mu(),fluid.get_k(),fluid.get_pr(),
                        std::abs(solid.get_T()-fluid.get_T()),
                        film_temperature_K);
                    const double into_cell=h*area*
                        (neighbor.get_T()-cell.get_T());
                    ledger.convection_W[index] += into_cell;
                    ledger.convection_W[neighbor_index] -= into_cell;
                    ledger.convection_transfer_W += std::abs(into_cell);
                }
            }

    auto add_advective_face = [&](int x,int y,int z,double outward_flow,
                                  int nx,int ny,int nz) {
        if(outward_flow == 0.0) return;
        const Cell& cell=mesh.at(x,y,z);
        const Cell& upstream=outward_flow < 0.0
            ? mesh.at(nx,ny,nz) : cell;
        const double transported=upstream.get_rho()*upstream.get_cp()*
            upstream.get_T();
        ledger.advection_internal_W[mesh.idx(x,y,z)] -=
            outward_flow*transported;
    };

    for(int x=0;x<mesh.get_nx();++x)
        for(int y=0;y<mesh.get_ny();++y)
            for(int z=0;z<mesh.get_nz();++z) {
                const Cell& cell=mesh.at(x,y,z);
                if(!cell.is_fluid()) continue;
                if(x>0) add_advective_face(
                    x,y,z,-solver.x_face_flux_m3s(x,y,z),x-1,y,z);
                if(x+1<mesh.get_nx()) add_advective_face(
                    x,y,z,solver.x_face_flux_m3s(x+1,y,z),x+1,y,z);
                if(y>0) add_advective_face(
                    x,y,z,-solver.y_face_flux_m3s(x,y,z),x,y-1,z);
                if(y+1<mesh.get_ny()) add_advective_face(
                    x,y,z,solver.y_face_flux_m3s(x,y+1,z),x,y+1,z);
                if(z>0) add_advective_face(
                    x,y,z,-solver.z_face_flux_m3s(x,y,z),x,y,z-1);
                if(z+1<mesh.get_nz()) add_advective_face(
                    x,y,z,solver.z_face_flux_m3s(x,y,z+1),x,y,z+1);

                const double ambient_flow=
                    solver.ambient_flow_into_cell_m3s(x,y,z);
                if(ambient_flow == 0.0) continue;
                const double transported=ambient_flow > 0.0
                    ? mesh.get_env().get_rho()*mesh.get_env().get_cp()*
                          mesh.get_env().get_T_ambient()
                    : cell.get_rho()*cell.get_cp()*cell.get_T();
                const double power=ambient_flow*transported;
                ledger.advection_boundary_W[mesh.idx(x,y,z)] += power;
                if(power > 0.0) ledger.boundary_inlet_W += power;
                else ledger.boundary_outlet_W += -power;
            }

    // Count each internal advective transfer once from the positive-coordinate
    // faces.  This proves that the case actually exercised face-flux transport
    // even though its global contribution must cancel.
    for(int x=1;x<mesh.get_nx();++x)
        for(int y=0;y<mesh.get_ny();++y)
            for(int z=0;z<mesh.get_nz();++z) {
                const double flow=solver.x_face_flux_m3s(x,y,z);
                if(flow == 0.0) continue;
                const Cell& upstream=flow > 0.0
                    ? mesh.at(x-1,y,z) : mesh.at(x,y,z);
                ledger.internal_advection_transfer_W += std::abs(flow)*
                    upstream.get_rho()*upstream.get_cp()*upstream.get_T();
            }
    return ledger;
}

double sum(const std::vector<double>& values) {
    double result=0.0;
    for(const double value : values) result += value;
    return result;
}

CaseEvidence run_case(bool adaptive) {
    Mesh carried=make_fixture(adaptive);
    CaseEvidence evidence;
    const std::string label=adaptive ? "adaptive" : "uniform";

    for(int step=0;step<kSteps;++step) {
        TemporaryCsv csv{
            std::filesystem::temp_directory_path()/
            ("native_thermal_energy_ledger_"+label+"_"+
             std::to_string(step)+".csv")};
        Solver solver(
            carried,kDt,kDt,false,1,-1,
            4.6,1.0e-12,2000,1.1,100,1.0e-8,
            false,0.8,10000,csv.path.string(),"pcg",false);
        solver.initialize_flow();
        const Mesh before=solver.get_mesh();
        const MechanismLedger ledger=reconstruct_ledger(before,solver);

        double reconstructed_continuity_residual=0.0;
        for(int x=0;x<before.get_nx();++x)
            for(int y=0;y<before.get_ny();++y)
                for(int z=0;z<before.get_nz();++z) {
                    if(!before.at(x,y,z).is_fluid()) continue;
                    const double outward=
                        -solver.x_face_flux_m3s(x,y,z)+
                         solver.x_face_flux_m3s(x+1,y,z)-
                         solver.y_face_flux_m3s(x,y,z)+
                         solver.y_face_flux_m3s(x,y+1,z)-
                         solver.z_face_flux_m3s(x,y,z)+
                         solver.z_face_flux_m3s(x,y,z+1);
                    const double residual=
                        solver.ambient_flow_into_cell_m3s(x,y,z)-outward;
                    reconstructed_continuity_residual=std::max(
                        reconstructed_continuity_residual,
                        std::abs(residual));
                }
        const double published_continuity_residual=
            solver.maximum_flow_continuity_residual_m3s();
        require(std::abs(reconstructed_continuity_residual-
                         published_continuity_residual) <= 1.0e-15,
                label+" independently reconstructed continuity residual "
                "does not match the published diagnostic");
        evidence.maximum_continuity_residual_m3s=std::max(
            evidence.maximum_continuity_residual_m3s,
            reconstructed_continuity_residual);
        solver.solve();
        const Mesh after=solver.get_mesh();

        double step_storage=0.0;
        double step_expected=0.0;
        double step_source=0.0;
        double step_boundary=0.0;
        for(int x=0;x<before.get_nx();++x)
            for(int y=0;y<before.get_ny();++y)
                for(int z=0;z<before.get_nz();++z) {
                    const std::size_t index=before.idx(x,y,z);
                    const Cell& old_cell=before.at(x,y,z);
                    const Cell& new_cell=after.at(x,y,z);
                    const double capacity=old_cell.get_rho()*old_cell.get_cp()*
                        old_cell.volume();
                    const double actual=capacity*
                        (new_cell.get_T()-old_cell.get_T());
                    const double expected=kDt*(
                        ledger.conduction_W[index]+
                        ledger.convection_W[index]+
                        ledger.advection_internal_W[index]+
                        ledger.advection_boundary_W[index]+
                        ledger.generation_W[index]);
                    const double cell_residual=std::abs(actual-expected);
                    const double cell_tolerance=2.0e-9*std::max(
                        {1.0,std::abs(actual),std::abs(expected)});
                    evidence.maximum_cell_residual_J=std::max(
                        evidence.maximum_cell_residual_J,cell_residual);
                    require(cell_residual <= cell_tolerance,
                            label+" per-cell ledger residual exceeds tolerance "
                            "at step "+std::to_string(step)+" cell ("+
                            std::to_string(x)+","+std::to_string(y)+","+
                            std::to_string(z)+"): residual="+
                            std::to_string(cell_residual)+" J, actual="+
                            std::to_string(actual)+" J, expected="+
                            std::to_string(expected)+" J");
                    step_storage += actual;
                    step_expected += expected;
                    step_source += kDt*ledger.generation_W[index];
                    step_boundary +=
                        kDt*ledger.advection_boundary_W[index];
                }

        require(std::abs(sum(ledger.conduction_W)) <= 1.0e-12,
                label+" internal conduction did not cancel");
        require(std::abs(sum(ledger.convection_W)) <= 1.0e-12,
                label+" internal convection did not cancel");
        require(std::abs(sum(ledger.advection_internal_W)) <= 1.0e-12,
                label+" internal advection did not cancel");
        const double global_residual=step_storage-(step_source+step_boundary);
        evidence.maximum_global_residual_J=std::max(
            evidence.maximum_global_residual_J,std::abs(global_residual));
        require(std::abs(step_storage-step_expected) <= 2.0e-9,
                label+" cell-ledger sum does not match storage at step "+
                std::to_string(step));
        require(std::abs(global_residual) <= 2.0e-9,
                label+" discrete global energy-balance residual exceeds "
                "tolerance at step "+
                std::to_string(step));

        evidence.source_energy_J += step_source;
        evidence.storage_energy_J += step_storage;
        evidence.boundary_inlet_energy_J +=
            kDt*ledger.boundary_inlet_W;
        evidence.boundary_outlet_energy_J +=
            kDt*ledger.boundary_outlet_W;
        evidence.boundary_net_energy_J += step_boundary;
        evidence.minimum_solid_conduction_transfer_W=std::min(
            evidence.minimum_solid_conduction_transfer_W,
            ledger.solid_conduction_transfer_W);
        evidence.minimum_fluid_conduction_transfer_W=std::min(
            evidence.minimum_fluid_conduction_transfer_W,
            ledger.fluid_conduction_transfer_W);
        evidence.minimum_convection_transfer_W=std::min(
            evidence.minimum_convection_transfer_W,
            ledger.convection_transfer_W);
        evidence.minimum_internal_advection_transfer_W=std::min(
            evidence.minimum_internal_advection_transfer_W,
            ledger.internal_advection_transfer_W);
        evidence.minimum_boundary_inlet_W=std::min(
            evidence.minimum_boundary_inlet_W,ledger.boundary_inlet_W);
        evidence.minimum_boundary_outlet_W=std::min(
            evidence.minimum_boundary_outlet_W,ledger.boundary_outlet_W);
        carried=after;
    }

    require(evidence.source_energy_J > 0.0,
            label+" fixture did not inject heat");
    require(evidence.minimum_solid_conduction_transfer_W > 1.0,
            label+" fixture did not exercise solid conduction");
    require(evidence.minimum_fluid_conduction_transfer_W > 1.0e-3,
            label+" fixture did not exercise fluid conduction");
    require(evidence.minimum_convection_transfer_W > 1.0,
            label+" fixture did not exercise solid-air convection");
    require(evidence.minimum_internal_advection_transfer_W > 1.0,
            label+" fixture did not exercise internal face-flux advection");
    require(evidence.minimum_boundary_inlet_W > 1.0 &&
            evidence.minimum_boundary_outlet_W > 1.0,
            label+" fixture did not exchange enthalpy with both ambient "
                  "boundaries");
    require(evidence.maximum_continuity_residual_m3s <= 1.0e-10,
            label+" flow continuity is too loose for the discrete update "
            "ledger");
    require(std::abs(
                evidence.storage_energy_J-
                (evidence.source_energy_J+evidence.boundary_net_energy_J)) <=
            5.0e-9,
            label+" cumulative frozen-capacity update ledger does not close");
    return evidence;
}

} // namespace

int main() {
    try {
        for(const bool adaptive : {false,true}) {
            const CaseEvidence evidence=run_case(adaptive);
            std::cout << (adaptive ? "adaptive" : "uniform")
                      << " ledger: source=" << evidence.source_energy_J
                      << " J, inlet=" << evidence.boundary_inlet_energy_J
                      << " J, outlet=" << evidence.boundary_outlet_energy_J
                      << " J, boundary-net="
                      << evidence.boundary_net_energy_J
                      << " J, storage=" << evidence.storage_energy_J
                      << " J, max-cell-residual="
                      << evidence.maximum_cell_residual_J
                      << " J, max-global-residual="
                      << evidence.maximum_global_residual_J << " J\n";
        }
        std::cout
            << "native_thermal_energy_ledger_test PASSED: 2 meshes x "
            << kSteps
            << " steps with source, solid/fluid conduction, solid-air "
               "convection, face-flux advection, ambient inlet/outlet, and "
               "frozen-step storage closure\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "native_thermal_energy_ledger_test FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}

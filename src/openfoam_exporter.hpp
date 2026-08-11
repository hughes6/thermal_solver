#ifndef OPENFOAM_EXPORTER_HPP
#define OPENFOAM_EXPORTER_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "mesh.hpp"

struct OpenFoamExportOptions {
    std::filesystem::path case_directory;
    bool overwrite = false;
    int parallel_processes = 4;
    double end_time = 10.0;
    double initial_time_step = 0.01;
    double maximum_time_step = 1.0;
    double maximum_courant_number = 0.5;
    double field_write_interval = 60.0;
    int saved_time_directories = 3;
    double report_interval = 10.0;
    bool use_k_omega_sst = false;
    double inlet_turbulence_intensity = 0.05;
    double turbulence_length_scale = 0.01;
    double turbulent_prandtl_number = 0.85;
    bool temperature_dependent_air = false;
    std::array<double,3> gravity = {0.0, 0.0, 0.0};
    double sutherland_temperature = 110.4;
    bool use_vent_pressure_loss = false;
    bool use_fan_curves = false;
    // Zero retains the legacy automatic choice based on use_fan_curves.
    int pimple_outer_correctors = 0;
    int pimple_pressure_correctors = 0;
    double fan_curve_extension_multiplier = 2.0;
    bool use_multirate_thermal = false;
    double airflow_warmup_time = 5.0;
    bool use_fan_startup_ramp = true;
    double fan_startup_ramp_time = 0.05;
    int fan_startup_ramp_steps = 5;
    double initial_airflow_check_interval = 0.01;
    double minimum_initial_airflow_duration = 0.02;
    double minimum_initial_air_exchange_fraction = 0.0;
    double airflow_maximum_time_step = 0.001;
    // Retain these names until the TOML schema is finalized. They control the
    // thermal-only stage, not OpenFOAM's native frozenFlow implementation.
    double frozen_flow_maximum_time_step = 1.0;
    double frozen_flow_maximum_courant_number = 1000.0;
    double airflow_refresh_interval = 300.0;
    double airflow_refresh_duration = 1.0;
    bool use_adaptive_airflow_refresh = true;
    double airflow_refresh_maximum_courant_number = 10.0;
    double airflow_refresh_check_interval = 0.01;
    double maximum_airflow_refresh_duration = 0.2;
    double maximum_mass_imbalance_fraction = 0.01;
    double maximum_device_flow_change_fraction = 0.02;
    double maximum_velocity_rms_change_fraction = 0.01;
    double maximum_accepted_velocity_rms_change_fraction = 0.01;
    double minimum_tracked_boundary_flow_fraction = 1e-4;
    bool stop_when_thermally_converged = false;
    double minimum_thermal_convergence_time = 3600.0;
    double thermal_convergence_reference_interval = 300.0;
    double maximum_temperature_change = 0.1;
    double maximum_component_average_temperature_change = 0.05;
    int thermal_convergence_required_checkpoints = 2;
};

class OpenFoamExporter {
public:
    static void export_mesh(const Mesh& mesh,
                            const OpenFoamExportOptions& options) {
        if(options.case_directory.empty())
            throw std::invalid_argument(
                "OpenFoamExporter: case directory must not be empty.");
        if(!mesh.has_openfoam_export_metadata())
            throw std::invalid_argument(
                "OpenFoamExporter: mesh was not stamped with "
                "stamp_component_for_openfoam().");
        validate_flow_device_connectivity(mesh,options);
        validate_pimple_correctors(options);
        if(options.parallel_processes < 2)
            throw std::invalid_argument(
                "OpenFoamExporter: parallel_processes must be at least two "
                "because the generated runner uses OpenFOAM parallel mode.");

        const std::filesystem::path poly_mesh =
            options.case_directory / "constant" / "polyMesh";
        if(std::filesystem::exists(poly_mesh) && !options.overwrite)
            throw std::runtime_error(
                "OpenFoamExporter: polyMesh already exists at '" +
                poly_mesh.string() + "'. Set overwrite=true to replace files.");
        if(options.overwrite) {
            std::filesystem::remove(
                options.case_directory/".openfoam_regions_prepared");
            std::filesystem::remove(
                options.case_directory/".thermal_convergence_state");
            std::filesystem::remove(
                options.case_directory/".thermal_convergence_streak");
            std::filesystem::remove(
                options.case_directory/".initial_airflow_converged");
            std::filesystem::remove(
                options.case_directory/".airflow_refresh_pending");
            std::filesystem::remove(
                options.case_directory/".airflow_convergence_state");
            clear_generated_solution_state(options.case_directory);
        }

        std::filesystem::create_directories(poly_mesh);
        std::filesystem::create_directories(
            options.case_directory / "system");

        const std::vector<FaceRecord> faces = build_faces(mesh);
        std::size_t internal_face_count = 0;
        while(internal_face_count < faces.size() &&
              faces[internal_face_count].neighbour >= 0)
            ++internal_face_count;

        write_points(mesh, poly_mesh / "points");
        write_faces(faces, poly_mesh / "faces");
        write_owner(faces, poly_mesh / "owner", mesh.get_cell_count());
        write_neighbour(
            faces, internal_face_count, poly_mesh / "neighbour",
            mesh.get_cell_count());
        write_boundary(
            mesh, faces, internal_face_count, poly_mesh / "boundary");
        write_cell_zones(mesh, poly_mesh / "cellZones");
        write_heat_source_sets(mesh, options.case_directory);
        write_heat_source_masks(mesh, options.case_directory);
        write_heat_source_toposet_dicts(mesh, options.case_directory);
        write_internal_device_files(mesh, options.case_directory);
        write_external_device_files(mesh, options, options.case_directory);
        write_device_report(
            mesh,options,options.case_directory/"airflow_devices.txt");
        write_interface_toposet_dict(mesh, options.case_directory);
        write_region_properties(
            mesh, options.case_directory/"constant"/"regionProperties");
        write_cht_case_files(mesh, options, options.case_directory);
        validate_time_controls(options);
        if(options.use_vent_pressure_loss) {
            for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
                if(patch.kind != Mesh::OpenFoamBoundaryPatch::Kind::Vent)
                    continue;
                validate_positive_finite(
                    patch.vent_free_area_m2, "vent free area");
                validate_positive_finite(
                    patch.vent_discharge_coefficient,
                    "vent discharge coefficient");
            }
        }
        if(options.use_fan_curves) {
            if(!std::isfinite(options.fan_curve_extension_multiplier) ||
               options.fan_curve_extension_multiplier <= 1.0)
                throw std::invalid_argument(
                    "OpenFoamExporter: fan_curve_extension_multiplier "
                    "must be finite and greater than one.");
            for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
                if(!patch.fan_has_curve) continue;
                validate_positive_finite(
                    patch.fan_curve_a, "fan curve shutoff pressure");
                validate_positive_finite(
                    patch.fan_rated_density, "fan rated density");
                if(!std::isfinite(patch.fan_curve_b) ||
                   !std::isfinite(patch.fan_curve_c) ||
                   patch.fan_curve_b < 0.0 ||
                   patch.fan_curve_c < 0.0)
                    throw std::invalid_argument(
                        "OpenFoamExporter: fan curve b/c coefficients "
                        "must be finite and non-negative.");
            }
            for(const auto& device :
                mesh.get_openfoam_internal_flow_devices()) {
                if(device.kind !=
                   Mesh::OpenFoamInternalFlowDevice::Kind::Fan)
                    continue;
                validate_positive_finite(
                    device.curve_a,
                    ("internal fan '"+device.name+
                     "' curve shutoff pressure").c_str());
                validate_positive_finite(
                    device.rated_density,
                    ("internal fan '"+device.name+
                     "' rated density").c_str());
                if(!std::isfinite(device.curve_b) ||
                   !std::isfinite(device.curve_c) ||
                   device.curve_b < 0.0 || device.curve_c < 0.0)
                    throw std::invalid_argument(
                        "OpenFoamExporter: internal fan '"+device.name+
                        "' curve b/c coefficients must be finite and "
                        "non-negative.");
            }
        }
        write_control_dict(
            mesh, options,
            options.case_directory / "system" / "controlDict");
        write_spatial_convergence_dict(
            options.case_directory / "system" / "spatialConvergenceDict");
        write_decompose_par_dict(
            mesh, options.parallel_processes,
            options.case_directory / "system" / "decomposeParDict");
        write_fv_schemes(options.case_directory / "system" / "fvSchemes");
        write_fv_solution(
            mesh, options,
            options.case_directory / "system" / "fvSolution");
        write_region_preparation_script(
            mesh, options,
            options.case_directory / "prepare_regions.sh");
        write_run_script(options.case_directory / "run_cht.sh");
        write_parallel_run_script(
            mesh, options,
            options.case_directory / "run_parallel.sh");
    }

private:
    static bool is_openfoam_time_name(const std::string& name) {
        if(name.empty() || name=="0") return false;
        std::size_t consumed=0;
        try {
            (void)std::stod(name,&consumed);
        } catch(...) {
            return false;
        }
        return consumed==name.size();
    }

    static void clear_generated_solution_state(
        const std::filesystem::path& case_directory) {
        if(!std::filesystem::is_directory(case_directory)) return;
        for(const auto& entry :
            std::filesystem::directory_iterator(case_directory)) {
            if(!entry.is_directory()) continue;
            const std::string name=entry.path().filename().string();
            const bool processor=
                name.rfind("processor",0)==0 &&
                name.size()>9 &&
                std::all_of(
                    name.begin()+9,name.end(),
                    [](unsigned char c) { return std::isdigit(c)!=0; });
            if(processor || name=="postProcessing" ||
               is_openfoam_time_name(name))
                std::filesystem::remove_all(entry.path());
        }
    }

    struct FaceRecord {
        std::array<std::size_t,4> points{};
        std::size_t owner = 0;
        long long neighbour = -1;
        int patch = -1;
    };

    struct PatchInfo {
        std::string name;
        std::string type;
        std::size_t count = 0;
        std::size_t start = 0;
    };

    struct BoundaryDeviceGeometry {
        std::array<double,3> outward_normal{0.0,0.0,0.0};
        double gross_area = 0.0;
        std::size_t face_count = 0;
    };

    struct BoundaryFanInterfaceGeometry {
        int axis = -1;
        int side = -1;
        double boundary_plane = 0.0;
        double plane = 0.0;
        std::array<double,3> minimum{
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()};
        std::array<double,3> maximum{
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest()};
    };

    static BoundaryDeviceGeometry boundary_device_geometry(
        const Mesh& mesh, int patch_id) {
        BoundaryDeviceGeometry result;
        for(int i=0;i<mesh.get_nx();++i)
            for(int j=0;j<mesh.get_ny();++j)
                for(int k=0;k<mesh.get_nz();++k)
                    for(int axis=0;axis<3;++axis)
                        for(int side=0;side<2;++side) {
                            if(mesh.get_openfoam_boundary_patch_id(
                                   i,j,k,axis,side)!=patch_id)
                                continue;
                            const Cell& cell=mesh.at(i,j,k);
                            const double area=
                                axis==0 ? cell.get_dy()*cell.get_dz() :
                                (axis==1 ? cell.get_dx()*cell.get_dz() :
                                           cell.get_dx()*cell.get_dy());
                            result.outward_normal[
                                static_cast<std::size_t>(axis)] +=
                                (side==0 ? -1.0 : 1.0)*area;
                            result.gross_area+=area;
                            ++result.face_count;
                        }
        if(result.gross_area>0.0)
            for(double& value : result.outward_normal)
                value/=result.gross_area;
        return result;
    }

    static BoundaryFanInterfaceGeometry boundary_fan_interface_geometry(
        const Mesh& mesh, int patch_id) {
        BoundaryFanInterfaceGeometry result;
        for(int i=0;i<mesh.get_nx();++i)
            for(int j=0;j<mesh.get_ny();++j)
                for(int k=0;k<mesh.get_nz();++k)
                    for(int axis=0;axis<3;++axis)
                        for(int side=0;side<2;++side) {
                            if(mesh.get_openfoam_boundary_patch_id(
                                   i,j,k,axis,side)!=patch_id)
                                continue;
                            if(result.axis>=0 &&
                               (result.axis!=axis || result.side!=side))
                                throw std::invalid_argument(
                                    "OpenFoamExporter: an ambient fan patch "
                                    "must lie on one exterior plane.");
                            result.axis=axis;
                            result.side=side;
                            const Cell& cell=mesh.at(i,j,k);
                            const std::array<double,3> center{
                                mesh.cell_center_x(i),
                                mesh.cell_center_y(j),
                                mesh.cell_center_z(k)};
                            const std::array<double,3> width{
                                cell.get_dx(),cell.get_dy(),cell.get_dz()};
                            std::array<int,3> neighbour_index{i,j,k};
                            neighbour_index[axis]+=(side==0 ? 1 : -1);
                            const std::array<int,3> cell_count{
                                mesh.get_nx(),mesh.get_ny(),mesh.get_nz()};
                            if(neighbour_index[axis]<0 ||
                               neighbour_index[axis]>=cell_count[axis])
                                throw std::invalid_argument(
                                    "OpenFoamExporter: an ambient curve fan "
                                    "requires at least two mesh cells normal "
                                    "to its rack boundary.");
                            const Cell& neighbour=mesh.at(
                                neighbour_index[0],neighbour_index[1],
                                neighbour_index[2]);
                            const std::array<double,3> neighbour_width{
                                neighbour.get_dx(),neighbour.get_dy(),
                                neighbour.get_dz()};
                            result.plane=
                                center[axis]+
                                (side==0 ? 1.0 : -1.0)*
                                (0.5*width[axis]+
                                 neighbour_width[axis]);
                            result.boundary_plane=
                                center[axis]+
                                (side==0 ? -0.5 : 0.5)*width[axis];
                            for(int tangent=0;tangent<3;++tangent) {
                                if(tangent==axis) continue;
                                result.minimum[tangent]=std::min(
                                    result.minimum[tangent],
                                    center[tangent]-0.5*width[tangent]);
                                result.maximum[tangent]=std::max(
                                    result.maximum[tangent],
                                    center[tangent]+0.5*width[tangent]);
                            }
                        }
        if(result.axis<0)
            throw std::invalid_argument(
                "OpenFoamExporter: ambient fan has no exterior faces.");
        result.minimum[result.axis]=result.plane;
        result.maximum[result.axis]=result.plane;
        return result;
    }

    static void validate_flow_device_connectivity(
        const Mesh& mesh, const OpenFoamExportOptions& options) {
        std::vector<int> occupied(mesh.get_cell_count(),-1);
        int device_index=0;
        for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
            const auto geometry=boundary_device_geometry(mesh,patch.id);
            if(geometry.face_count==0 || patch.adjacent_cells.empty())
                throw std::invalid_argument(
                    "OpenFoamExporter: ambient device '"+patch.name+
                    "' has no exterior faces or adjacent fluid cells.");
            const double magnitude=std::sqrt(
                patch.direction[0]*patch.direction[0]+
                patch.direction[1]*patch.direction[1]+
                patch.direction[2]*patch.direction[2]);
            validate_positive_finite(
                magnitude,"ambient device direction magnitude");
            const double normal_component=
                (patch.direction[0]*geometry.outward_normal[0]+
                 patch.direction[1]*geometry.outward_normal[1]+
                 patch.direction[2]*geometry.outward_normal[2])/magnitude;
            if(std::abs(normal_component)<0.9)
                throw std::invalid_argument(
                    "OpenFoamExporter: ambient device '"+patch.name+
                    "' direction must be normal to its rack boundary.");
            if(patch.kind==Mesh::OpenFoamBoundaryPatch::Kind::Inlet &&
               normal_component>=0.0)
                throw std::invalid_argument(
                    "OpenFoamExporter: intake fan '"+patch.name+
                    "' direction points out of the rack.");
            if(patch.kind==Mesh::OpenFoamBoundaryPatch::Kind::Outlet &&
               normal_component<=0.0)
                throw std::invalid_argument(
                    "OpenFoamExporter: exhaust fan '"+patch.name+
                    "' direction points into the rack.");
            if(is_external_source_device(patch,options))
                for(std::size_t cell : patch.adjacent_cells) {
                    if(occupied.at(cell)>=0)
                        throw std::invalid_argument(
                            "OpenFoamExporter: ambient flow-device source "
                            "zones overlap.");
                    occupied[cell]=device_index;
                }
            ++device_index;
        }
        for(const auto& device :
            mesh.get_openfoam_internal_flow_devices()) {
            const double magnitude=std::sqrt(
                device.direction[0]*device.direction[0]+
                device.direction[1]*device.direction[1]+
                device.direction[2]*device.direction[2]);
            validate_positive_finite(
                magnitude,"internal device direction magnitude");
            if(device.cells.empty())
                throw std::invalid_argument(
                    "OpenFoamExporter: internal flow device '"+
                    device.name+"' has no fluid cells.");
            for(std::size_t cell : device.cells) {
                if(occupied.at(cell)>=0)
                    throw std::invalid_argument(
                        "OpenFoamExporter: internal/ambient flow-device "
                        "source zones overlap.");
                occupied[cell]=device_index;
            }
            ++device_index;
        }
    }

    static void write_device_report(
        const Mesh& mesh, const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        output << "OpenFOAM airflow-device connectivity report\n"
               << "Runtime phi values are mass flow [kg/s]. Positive "
                  "ambient phi is outward; negative is inward.\n"
               << "Total ambient net phi should approach zero.\n\n"
               << "AMBIENT DEVICES\n";
        output.precision(10);
        for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
            const auto geometry=boundary_device_geometry(mesh,patch.id);
            const char* kind=
                patch.kind==Mesh::OpenFoamBoundaryPatch::Kind::Inlet
                    ? "intake fan" :
                (patch.kind==Mesh::OpenFoamBoundaryPatch::Kind::Outlet
                    ? "exhaust fan" : "passive vent");
            output << "- "<<patch.name<<" | "<<kind
                   <<" | faces="<<geometry.face_count
                   <<" | adjacentCells="<<patch.adjacent_cells.size()
                   <<" | grossArea="<<geometry.gross_area<<" m2"
                   <<" | outwardNormal=("<<geometry.outward_normal[0]<<' '
                   <<geometry.outward_normal[1]<<' '
                   <<geometry.outward_normal[2]<<")"
                   <<" | direction=("<<patch.direction[0]<<' '
                   <<patch.direction[1]<<' '<<patch.direction[2]<<")";
            if(patch.fan_has_curve && options.use_fan_curves)
                output<<" | fanCurve=("<<patch.fan_curve_a<<' '
                      <<patch.fan_curve_b<<' '<<patch.fan_curve_c<<")";
            if(patch.kind==Mesh::OpenFoamBoundaryPatch::Kind::Vent)
                output<<" | freeArea="<<patch.vent_free_area_m2
                      <<" m2 | Cd="<<patch.vent_discharge_coefficient;
            output<<'\n';
        }
        output << "\nINTERNAL DEVICES\n";
        for(const auto& device :
            mesh.get_openfoam_internal_flow_devices())
            output<<"- "<<device.name<<" | "
                  <<(device.kind==
                         Mesh::OpenFoamInternalFlowDevice::Kind::Fan
                        ? "internal fan" : "internal passive vent")
                  <<" | cells="<<device.cells.size()
                  <<" | direction=("<<device.direction[0]<<' '
                  <<device.direction[1]<<' '<<device.direction[2]<<")\n";
    }

    static int boundary_patch(const Mesh& mesh,
                              int x, int y, int z,
                              int axis, int side) {
        const int opening =
            mesh.get_openfoam_boundary_patch_id(x,y,z,axis,side);
        return opening < 0 ? 0 : opening+1;
    }

    static std::size_t point_index(const Mesh& mesh,
                                   int i, int j, int k) {
        return static_cast<std::size_t>(i) *
                   static_cast<std::size_t>(mesh.get_ny()+1) *
                   static_cast<std::size_t>(mesh.get_nz()+1) +
               static_cast<std::size_t>(j) *
                   static_cast<std::size_t>(mesh.get_nz()+1) +
               static_cast<std::size_t>(k);
    }

    static std::vector<FaceRecord> build_faces(const Mesh& mesh) {
        std::vector<FaceRecord> faces;
        const int nx = mesh.get_nx();
        const int ny = mesh.get_ny();
        const int nz = mesh.get_nz();
        faces.reserve(
            static_cast<std::size_t>((nx+1)*ny*nz) +
            static_cast<std::size_t>(nx*(ny+1)*nz) +
            static_cast<std::size_t>(nx*ny*(nz+1)));

        // Internal faces first: neighbour entries correspond exactly to this
        // initial portion of the face and owner lists.
        for(int i=1; i<nx; ++i) for(int j=0; j<ny; ++j)
            for(int k=0; k<nz; ++k)
                faces.push_back({
                    {point_index(mesh,i,j,k),
                     point_index(mesh,i,j+1,k),
                     point_index(mesh,i,j+1,k+1),
                     point_index(mesh,i,j,k+1)},
                    mesh.idx(i-1,j,k),
                    static_cast<long long>(mesh.idx(i,j,k)), -1});

        for(int j=1; j<ny; ++j) for(int i=0; i<nx; ++i)
            for(int k=0; k<nz; ++k)
                faces.push_back({
                    {point_index(mesh,i,j,k),
                     point_index(mesh,i,j,k+1),
                     point_index(mesh,i+1,j,k+1),
                     point_index(mesh,i+1,j,k)},
                    mesh.idx(i,j-1,k),
                    static_cast<long long>(mesh.idx(i,j,k)), -1});

        for(int k=1; k<nz; ++k) for(int i=0; i<nx; ++i)
            for(int j=0; j<ny; ++j)
                faces.push_back({
                    {point_index(mesh,i,j,k),
                     point_index(mesh,i+1,j,k),
                     point_index(mesh,i+1,j+1,k),
                     point_index(mesh,i,j+1,k)},
                    mesh.idx(i,j,k-1),
                    static_cast<long long>(mesh.idx(i,j,k)), -1});

        std::sort(
            faces.begin(), faces.end(),
            [](const FaceRecord& a, const FaceRecord& b) {
                if(a.owner != b.owner) return a.owner < b.owner;
                return a.neighbour < b.neighbour;
            });
        const std::size_t internal_count = faces.size();

        // Boundary faces: patch zero is the remaining rack wall, followed
        // by export-aware fan/vent opening patches.
        for(int j=0; j<ny; ++j) for(int k=0; k<nz; ++k)
            faces.push_back({
                {point_index(mesh,0,j,k),
                 point_index(mesh,0,j,k+1),
                 point_index(mesh,0,j+1,k+1),
                 point_index(mesh,0,j+1,k)},
                mesh.idx(0,j,k), -1,
                boundary_patch(mesh,0,j,k,0,0)});

        for(int j=0; j<ny; ++j) for(int k=0; k<nz; ++k)
            faces.push_back({
                {point_index(mesh,nx,j,k),
                 point_index(mesh,nx,j+1,k),
                 point_index(mesh,nx,j+1,k+1),
                 point_index(mesh,nx,j,k+1)},
                mesh.idx(nx-1,j,k), -1,
                boundary_patch(mesh,nx-1,j,k,0,1)});

        for(int i=0; i<nx; ++i) for(int k=0; k<nz; ++k)
            faces.push_back({
                {point_index(mesh,i,0,k),
                 point_index(mesh,i+1,0,k),
                 point_index(mesh,i+1,0,k+1),
                 point_index(mesh,i,0,k+1)},
                mesh.idx(i,0,k), -1,
                boundary_patch(mesh,i,0,k,1,0)});

        for(int i=0; i<nx; ++i) for(int k=0; k<nz; ++k)
            faces.push_back({
                {point_index(mesh,i,ny,k),
                 point_index(mesh,i,ny,k+1),
                 point_index(mesh,i+1,ny,k+1),
                 point_index(mesh,i+1,ny,k)},
                mesh.idx(i,ny-1,k), -1,
                boundary_patch(mesh,i,ny-1,k,1,1)});

        for(int i=0; i<nx; ++i) for(int j=0; j<ny; ++j)
            faces.push_back({
                {point_index(mesh,i,j,0),
                 point_index(mesh,i,j+1,0),
                 point_index(mesh,i+1,j+1,0),
                 point_index(mesh,i+1,j,0)},
                mesh.idx(i,j,0), -1,
                boundary_patch(mesh,i,j,0,2,0)});

        for(int i=0; i<nx; ++i) for(int j=0; j<ny; ++j)
            faces.push_back({
                {point_index(mesh,i,j,nz),
                 point_index(mesh,i+1,j,nz),
                 point_index(mesh,i+1,j+1,nz),
                 point_index(mesh,i,j+1,nz)},
                mesh.idx(i,j,nz-1), -1,
                boundary_patch(mesh,i,j,nz-1,2,1)});

        std::sort(
            faces.begin()+static_cast<std::ptrdiff_t>(internal_count),
            faces.end(),
            [](const FaceRecord& a, const FaceRecord& b) {
                if(a.patch != b.patch) return a.patch < b.patch;
                return a.owner < b.owner;
            });

        return faces;
    }

    static void require_stream(const std::ofstream& output,
                               const std::filesystem::path& path) {
        if(!output)
            throw std::runtime_error(
                "OpenFoamExporter: could not write '" + path.string() + "'.");
    }

    static void write_header(std::ofstream& output,
                             const char* class_name,
                             const char* object,
                             const char* location) {
        output <<
            "FoamFile\n"
            "{\n"
            "    format      ascii;\n"
            "    class       " << class_name << ";\n"
            "    location    \"" << location << "\";\n"
            "    object      " << object << ";\n"
            "}\n\n";
    }

    static void write_points(const Mesh& mesh,
                             const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"vectorField","points","constant/polyMesh");
        const auto& xb = mesh.get_x_bounds();
        const auto& yb = mesh.get_y_bounds();
        const auto& zb = mesh.get_z_bounds();
        output << xb.size()*yb.size()*zb.size() << "\n(\n";
        output.precision(17);
        for(double x : xb) for(double y : yb) for(double z : zb)
            output << '(' << x << ' ' << y << ' ' << z << ")\n";
        output << ")\n";
    }

    static void write_faces(const std::vector<FaceRecord>& faces,
                            const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"faceList","faces","constant/polyMesh");
        output << faces.size() << "\n(\n";
        for(const FaceRecord& face : faces)
            output << "4(" << face.points[0] << ' ' << face.points[1] << ' '
                   << face.points[2] << ' ' << face.points[3] << ")\n";
        output << ")\n";
    }

    static void write_owner(const std::vector<FaceRecord>& faces,
                            const std::filesystem::path& path,
                            std::size_t cell_count) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"labelList","owner","constant/polyMesh");
        output << "// nCells: " << cell_count
               << " nFaces: " << faces.size() << "\n";
        output << faces.size() << "\n(\n";
        for(const FaceRecord& face : faces) output << face.owner << '\n';
        output << ")\n";
    }

    static void write_neighbour(const std::vector<FaceRecord>& faces,
                                std::size_t internal_count,
                                const std::filesystem::path& path,
                                std::size_t cell_count) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"labelList","neighbour","constant/polyMesh");
        output << "// nCells: " << cell_count
               << " nInternalFaces: " << internal_count << "\n";
        output << internal_count << "\n(\n";
        for(std::size_t i=0; i<internal_count; ++i)
            output << faces[i].neighbour << '\n';
        output << ")\n";
    }

    static void write_boundary(const Mesh& mesh,
                               const std::vector<FaceRecord>& faces,
                               std::size_t internal_count,
                               const std::filesystem::path& path) {
        std::vector<PatchInfo> patches;
        patches.push_back({"rack_walls","wall",0,0});
        for(const auto& opening : mesh.get_openfoam_boundary_patches())
            patches.push_back(
                {foam_word(opening.name),"patch",0,0});
        for(std::size_t i=internal_count; i<faces.size(); ++i)
            ++patches[static_cast<std::size_t>(faces[i].patch)].count;
        std::size_t start = internal_count;
        for(PatchInfo& patch : patches) {
            patch.start = start;
            start += patch.count;
        }

        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"polyBoundaryMesh","boundary","constant/polyMesh");
        output << patches.size() << "\n(\n";
        for(const PatchInfo& patch : patches) {
            output << patch.name << "\n{\n"
                   << "    type       " << patch.type << ";\n"
                   << "    nFaces     " << patch.count << ";\n"
                   << "    startFace  " << patch.start << ";\n"
                   << "}\n";
        }
        output << ")\n";
    }

    static std::string foam_word(std::string name) {
        for(char& c : name)
            if(!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                c = '_';
        while(!name.empty() && name.front() == '_') name.erase(name.begin());
        if(name.empty() ||
           std::isdigit(static_cast<unsigned char>(name.front())))
            name = "region_" + name;
        return name;
    }

    static void write_zone(std::ofstream& output, const std::string& name,
                           const std::vector<std::size_t>& labels) {
        output << name << "\n{\n"
               << "    type cellZone;\n"
               << "    cellLabels List<label>\n"
               << "    " << labels.size() << "\n    (\n";
        for(std::size_t label : labels) output << "        " << label << '\n';
        output << "    );\n}\n";
    }

    static void write_cell_zones(const Mesh& mesh,
                                 const std::filesystem::path& path) {
        const auto& metadata = mesh.get_openfoam_cell_metadata();
        const auto& components = mesh.get_openfoam_component_regions();
        std::vector<std::size_t> fluid;
        std::vector<std::vector<std::size_t>> solids(components.size());
        for(std::size_t cell=0; cell<metadata.size(); ++cell) {
            const auto& item = metadata[cell];
            if(item.region_type ==
               Mesh::OpenFoamCellMetadata::RegionType::Fluid) {
                fluid.push_back(cell);
            } else {
                if(item.component_id < 0 ||
                   static_cast<std::size_t>(item.component_id) >=
                       solids.size())
                    throw std::runtime_error(
                        "OpenFoamExporter: solid cell has no component.");
                solids[static_cast<std::size_t>(item.component_id)]
                    .push_back(cell);
            }
        }

        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"regIOobject","cellZones","constant/polyMesh");
        output << 1 + components.size() << "\n(\n";
        write_zone(output,"fluid",fluid);
        for(std::size_t i=0; i<components.size(); ++i)
            write_zone(
                output,
                foam_word(components[i].name) + "_" + std::to_string(i),
                solids[i]);
        output << ")\n";
    }

    static void write_heat_source_sets(
        const Mesh& mesh,
        const std::filesystem::path& case_directory) {
        const auto& sources = mesh.get_openfoam_heat_source_regions();
        const auto& metadata = mesh.get_openfoam_cell_metadata();
        const auto& cells = mesh.get_cells();
        const auto& components = mesh.get_openfoam_component_regions();
        const std::filesystem::path sets_directory =
            case_directory/"constant"/"polyMesh"/"sets";
        std::filesystem::create_directories(sets_directory);

        std::ofstream properties(
            case_directory/"constant"/"openfoamExportProperties");
        require_stream(
            properties,
            case_directory/"constant"/"openfoamExportProperties");
        write_header(
            properties,"dictionary","openfoamExportProperties","constant");
        properties.precision(17);
        properties << "heatSources\n(\n";

        for(const auto& source : sources) {
            std::vector<std::size_t> labels;
            double selected_volume = 0.0;
            for(std::size_t cell=0; cell<metadata.size(); ++cell) {
                if(metadata[cell].heat_source_id != source.id) continue;
                labels.push_back(cell);
                selected_volume += cells[cell].volume();
            }
            if(labels.empty() || selected_volume <= 0.0)
                throw std::runtime_error(
                    "OpenFoamExporter: heat source '" + source.name +
                    "' contains no selected cells.");
            if(source.component_id < 0 ||
               static_cast<std::size_t>(source.component_id) >=
                   components.size())
                throw std::runtime_error(
                    "OpenFoamExporter: heat source has no component region.");

            const std::string set_name =
                foam_word(source.name)+"_"+std::to_string(source.id);
            const std::filesystem::path set_path =
                sets_directory/set_name;
            std::ofstream set_output(set_path);
            require_stream(set_output,set_path);
            write_header(
                set_output,"cellSet",set_name.c_str(),
                "constant/polyMesh/sets");
            set_output << labels.size() << "\n(\n";
            for(std::size_t label : labels)
                set_output << label << '\n';
            set_output << ")\n";

            properties
                << "{\n"
                << "    name             " << set_name << ";\n"
                << "    componentRegion  "
                << foam_word(components[
                       static_cast<std::size_t>(source.component_id)].name)
                << '_' << source.component_id << ";\n"
                << "    solverRegion     "
                << (source.fluid ? "fluid" : "solid") << ";\n"
                << "    watts            " << source.watts << ";\n"
                << "    selectedVolume   " << selected_volume << ";\n"
                << "    volumetricPower  "
                << source.watts/selected_volume << ";\n"
                << "}\n";
        }
        properties << ");\n";
    }

    static void write_region_properties(
        const Mesh& mesh,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","regionProperties","constant");
        output << "regions\n(\n"
               << "    fluid       (fluid)\n"
               << "    solid       (";
        const auto& components = mesh.get_openfoam_component_regions();
        for(std::size_t i=0; i<components.size(); ++i) {
            if(i) output << ' ';
            output << foam_word(components[i].name) << '_' << i;
        }
        output << ")\n);\n";
    }

    static std::string heat_source_set_name(
        const Mesh::OpenFoamHeatSourceRegion& source) {
        return foam_word(source.name)+"_"+std::to_string(source.id);
    }

    static std::string heat_source_mask_name(
        const Mesh::OpenFoamHeatSourceRegion& source) {
        return "heatSourceMask_"+std::to_string(source.id);
    }

    static void write_heat_source_masks(
        const Mesh& mesh,
        const std::filesystem::path& case_directory) {
        const auto& sources = mesh.get_openfoam_heat_source_regions();
        const auto& metadata = mesh.get_openfoam_cell_metadata();
        const std::filesystem::path zero_directory = case_directory/"0";
        std::filesystem::create_directories(zero_directory);

        for(const auto& source : sources) {
            const std::string field_name = heat_source_mask_name(source);
            const std::filesystem::path path = zero_directory/field_name;
            std::ofstream output(path);
            require_stream(output,path);
            write_header(output,"volScalarField",field_name.c_str(),"0");
            output <<
                "dimensions      [0 0 0 0 0 0 0];\n"
                "internalField   nonuniform List<scalar>\n";
            output << metadata.size() << "\n(\n";
            for(const auto& item : metadata)
                output << (item.heat_source_id == source.id ? 1 : 0) << '\n';
            output <<
                ")\n;\n"
                "boundaryField\n"
                "{\n"
                "    \".*\"\n"
                "    {\n"
                "        type  calculated;\n"
                "        value uniform 0;\n"
                "    }\n"
                "}\n";
        }
    }

    static void write_heat_source_toposet_dicts(
        const Mesh& mesh,
        const std::filesystem::path& case_directory) {
        const std::filesystem::path system_directory =
            case_directory/"system";
        for(const auto& source : mesh.get_openfoam_heat_source_regions()) {
            const std::string set_name = heat_source_set_name(source);
            const std::filesystem::path path =
                system_directory/("topoSetDict_"+set_name);
            std::ofstream output(path);
            require_stream(output,path);
            write_header(
                output,"dictionary",path.filename().string().c_str(),"system");
            output <<
                "actions\n"
                "(\n"
                "    {\n"
                "        name    " << set_name << ";\n"
                "        type    cellSet;\n"
                "        action  new;\n"
                "        source  fieldToCell;\n"
                "        field   " << heat_source_mask_name(source) << ";\n"
                "        min     0.5;\n"
                "        max     1.5;\n"
                "    }\n"
                "    {\n"
                "        name    " << set_name << ";\n"
                "        type    cellZoneSet;\n"
                "        action  new;\n"
                "        source  setToCellZone;\n"
                "        set     " << set_name << ";\n"
                "    }\n"
                ");\n";
        }
    }

    static std::string internal_device_name(
        const Mesh::OpenFoamInternalFlowDevice& device) {
        return "internal_" + foam_word(device.name) + "_" +
            std::to_string(device.id);
    }

    static bool is_external_source_device(
        const Mesh::OpenFoamBoundaryPatch& patch,
        const OpenFoamExportOptions& options) {
        return
            (options.use_vent_pressure_loss &&
             patch.kind == Mesh::OpenFoamBoundaryPatch::Kind::Vent);
    }

    static std::string external_device_name(
        const Mesh::OpenFoamBoundaryPatch& patch) {
        return "external_" + foam_word(patch.name) + "_" +
            std::to_string(patch.id);
    }

    static void write_external_device_files(
        const Mesh& mesh, const OpenFoamExportOptions& options,
        const std::filesystem::path& case_directory) {
        for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
            if(!is_external_source_device(patch,options)) continue;
            const std::string name=external_device_name(patch);
            const std::string mask_name=name+"_mask";
            const auto mask_path=case_directory/"0"/mask_name;
            std::ofstream mask(mask_path);
            require_stream(mask,mask_path);
            write_header(mask,"volScalarField",mask_name.c_str(),"0");
            std::vector<unsigned char> selected(mesh.get_cell_count(),0);
            for(std::size_t cell : patch.adjacent_cells)
                selected.at(cell)=1;
            mask << "dimensions [0 0 0 0 0 0 0];\n"
                 << "internalField nonuniform List<scalar>\n"
                 << selected.size() << "\n(\n";
            for(unsigned char value : selected)
                mask << static_cast<int>(value) << '\n';
            mask << ")\n;\nboundaryField\n{\n \".*\"\n"
                    " { type calculated; value uniform 0; }\n}\n";

            const auto dict_path=
                case_directory/"system"/("topoSetDict_"+name);
            std::ofstream dict(dict_path);
            require_stream(dict,dict_path);
            write_header(
                dict,"dictionary",dict_path.filename().string().c_str(),
                "system");
            dict << "actions\n(\n"
                 << "{ name " << name << "; type cellSet; action new;\n"
                 << "  source fieldToCell; field " << mask_name
                 << "; min 0.5; max 1.5; }\n"
                 << "{ name " << name << "; type cellZoneSet; action new;\n"
                 << "  source setToCellZone; set " << name << "; }\n";
            dict << ");\n";
        }
    }

    static void write_boundary_fan_baffle_files(
        const Mesh& mesh, const OpenFoamExportOptions& options,
        const std::filesystem::path& case_directory) {
        bool have_fans=false;
        for(const auto& patch : mesh.get_openfoam_boundary_patches())
            have_fans = have_fans ||
                (options.use_fan_curves && patch.fan_has_curve);
        if(!have_fans) return;

        const auto topo_path=
            case_directory/"system"/"topoSetDict_boundary_fans";
        std::ofstream topo(topo_path);
        require_stream(topo,topo_path);
        write_header(
            topo,"dictionary","topoSetDict_boundary_fans","system");
        topo.precision(17);
        topo << "actions\n(\n";

        const double minimum_width=std::min({
            mesh.get_dx(),mesh.get_dy(),mesh.get_dz()});
        const double epsilon=minimum_width*1e-4;
        bool first_duct_wall=true;
        for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
            if(!(options.use_fan_curves && patch.fan_has_curve)) continue;
            const auto geometry=
                boundary_fan_interface_geometry(mesh,patch.id);
            for(int tangent=0;tangent<3;++tangent) {
                if(tangent==geometry.axis) continue;
                for(int side=0;side<2;++side) {
                    auto wall_min=geometry.minimum;
                    auto wall_max=geometry.maximum;
                    wall_min[geometry.axis]=std::min(
                        geometry.boundary_plane,geometry.plane)-epsilon;
                    wall_max[geometry.axis]=std::max(
                        geometry.boundary_plane,geometry.plane)+epsilon;
                    const double coordinate=side==0
                        ? geometry.minimum[tangent]
                        : geometry.maximum[tangent];
                    wall_min[tangent]=coordinate-epsilon;
                    wall_max[tangent]=coordinate+epsilon;
                    topo << "{ name boundary_fan_duct_walls_set; "
                         << "type faceSet; action "
                         << (first_duct_wall ? "new" : "add") << ";\n"
                         << "  source boxToFace; box ("
                         << wall_min[0]<<' '<<wall_min[1]<<' '
                         << wall_min[2]<<") ("
                         << wall_max[0]<<' '<<wall_max[1]<<' '
                         << wall_max[2]<<"); }\n";
                    first_duct_wall=false;
                }
            }
        }
        topo << "{ name boundary_fan_duct_walls; type faceZoneSet; "
             << "action new;\n"
             << "  source setToFaceZone; faceSet "
             << "boundary_fan_duct_walls_set; }\n";
        topo << ");\n";

        const auto baffles_path=
            case_directory/"system"/"createBafflesDict_boundary_fans";
        std::ofstream baffles(baffles_path);
        require_stream(baffles,baffles_path);
        write_header(
            baffles,"dictionary","createBafflesDict_boundary_fans",
            "system");
        baffles.precision(17);
        baffles << "internalFacesOnly true;\nbaffles\n{\n";
        const double pressure=
            options.temperature_dependent_air
                ? mesh.get_env().get_ambient_pressure() : 101325.0;
        baffles << "boundary_fan_duct_walls\n"
                << "{\n type faceZone;\n"
                << " zoneName boundary_fan_duct_walls;\n"
                << " patchPairs\n {\n  type wall;\n"
                << "  patchFields\n  {\n"
                << "   U { type noSlip; }\n"
                << "   p { type zeroGradient; }\n"
                << "   p_rgh { type fixedFluxPressure; "
                   "gradient uniform 0; value uniform "
                << pressure << "; }\n"
                << "   T { type zeroGradient; }\n"
                << "   k { type kqRWallFunction; value uniform 1e-06; }\n"
                << "   omega { type omegaWallFunction; "
                   "value uniform 1; }\n"
                << "   nut { type nutkWallFunction; value uniform 0; }\n"
                << "   alphat { type compressible::alphatWallFunction; "
                   "Prt 0.85; value uniform 0; }\n"
                << "  }\n }\n}\n";
        baffles << "}\n";
    }

    static void write_internal_device_files(
        const Mesh& mesh,
        const std::filesystem::path& case_directory) {
        const auto& devices = mesh.get_openfoam_internal_flow_devices();
        for(const auto& device : devices) {
            const std::string name = internal_device_name(device);
            const std::string mask_name = name + "_mask";
            const auto mask_path = case_directory/"0"/mask_name;
            std::ofstream mask(mask_path);
            require_stream(mask,mask_path);
            write_header(mask,"volScalarField",mask_name.c_str(),"0");
            std::vector<unsigned char> selected(mesh.get_cell_count(),0);
            for(std::size_t cell : device.cells) selected.at(cell)=1;
            mask << "dimensions [0 0 0 0 0 0 0];\n"
                 << "internalField nonuniform List<scalar>\n"
                 << selected.size() << "\n(\n";
            for(unsigned char value : selected)
                mask << static_cast<int>(value) << '\n';
            mask << ")\n;\nboundaryField\n{\n \".*\"\n"
                    " { type calculated; value uniform 0; }\n}\n";

            const auto dict_path =
                case_directory/"system"/("topoSetDict_"+name);
            std::ofstream dict(dict_path);
            require_stream(dict,dict_path);
            write_header(
                dict,"dictionary",dict_path.filename().string().c_str(),
                "system");
            dict << "actions\n(\n"
                 << "{ name " << name << "; type cellSet; action new;\n"
                 << "  source fieldToCell; field " << mask_name
                 << "; min 0.5; max 1.5; }\n"
                 << "{ name " << name << "; type cellZoneSet; action new;\n"
                 << "  source setToCellZone; set " << name << "; }\n";
            if(device.kind ==
               Mesh::OpenFoamInternalFlowDevice::Kind::Fan) {
                dict << "{ name " << name
                     << "_faces; type faceSet; action new;\n"
                     << "  source cellToFace; option outside; zone "
                     << name << "; }\n"
                     << "{ name " << name
                     << "_faces; type faceZoneSet; action new;\n"
                     << "  source setToFaceZone; faceSet " << name
                     << "_faces; }\n";
            }
            dict << ");\n";
        }
    }

    static void validate_positive_finite(
        double value, const char* name) {
        if(!std::isfinite(value) || value <= 0.0)
            throw std::invalid_argument(
                std::string("OpenFoamExporter: ") + name +
                " must be finite and positive.");
    }

    static int effective_pimple_outer_correctors(
        const OpenFoamExportOptions& options) {
        return options.pimple_outer_correctors > 0
            ? options.pimple_outer_correctors
            : (options.use_fan_curves ? 3 : 1);
    }

    static int effective_pimple_pressure_correctors(
        const OpenFoamExportOptions& options) {
        return options.pimple_pressure_correctors > 0
            ? options.pimple_pressure_correctors
            : (options.use_fan_curves ? 3 : 2);
    }

    static void validate_pimple_correctors(
        const OpenFoamExportOptions& options) {
        if(options.pimple_outer_correctors < 0)
            throw std::invalid_argument(
                "OpenFoamExporter: pimple_outer_correctors must be zero "
                "(automatic) or positive.");
        if(options.pimple_pressure_correctors < 0)
            throw std::invalid_argument(
                "OpenFoamExporter: pimple_pressure_correctors must be zero "
                "(automatic) or positive.");
    }

    static void validate_time_controls(
        const OpenFoamExportOptions& options) {
        validate_positive_finite(options.end_time,"end_time");
        validate_positive_finite(
            options.initial_time_step,"initial_time_step");
        validate_positive_finite(
            options.maximum_time_step,"maximum_time_step");
        validate_positive_finite(
            options.maximum_courant_number,"maximum_courant_number");
        validate_positive_finite(
            options.field_write_interval,"field_write_interval");
        if(options.saved_time_directories < 2)
            throw std::invalid_argument(
                "OpenFoamExporter: saved_time_directories must be at "
                "least 2 so a prior restart state is retained.");
        validate_positive_finite(
            options.report_interval,"report_interval");
        if(options.use_k_omega_sst) {
            validate_positive_finite(
                options.inlet_turbulence_intensity,
                "inlet_turbulence_intensity");
            validate_positive_finite(
                options.turbulence_length_scale,
                "turbulence_length_scale");
            validate_positive_finite(
                options.turbulent_prandtl_number,
                "turbulent_prandtl_number");
            if(options.inlet_turbulence_intensity >= 1.0)
                throw std::invalid_argument(
                    "OpenFoamExporter: inlet_turbulence_intensity "
                    "must be less than 1.");
        }
        if(options.temperature_dependent_air)
            validate_positive_finite(
                options.sutherland_temperature,
                "sutherland_temperature");
        if(options.initial_time_step > options.maximum_time_step)
            throw std::invalid_argument(
                "OpenFoamExporter: initial_time_step must not exceed "
                "maximum_time_step.");
        if(options.use_multirate_thermal) {
            validate_positive_finite(
                options.airflow_warmup_time,"airflow_warmup_time");
            validate_positive_finite(
                options.initial_airflow_check_interval,
                "initial_airflow_check_interval");
            validate_positive_finite(
                options.minimum_initial_airflow_duration,
                "minimum_initial_airflow_duration");
            if(!std::isfinite(options.minimum_initial_air_exchange_fraction) ||
               options.minimum_initial_air_exchange_fraction < 0.0)
                throw std::invalid_argument(
                    "OpenFoamExporter: minimum_initial_air_exchange_fraction "
                    "must be finite and nonnegative.");
            validate_positive_finite(
                options.airflow_maximum_time_step,
                "airflow_maximum_time_step");
            if(options.minimum_initial_airflow_duration >
               options.airflow_warmup_time)
                throw std::invalid_argument(
                    "OpenFoamExporter: minimum_initial_airflow_duration "
                    "must not exceed airflow_warmup_time.");
            if(options.initial_airflow_check_interval >
               options.airflow_warmup_time)
                throw std::invalid_argument(
                    "OpenFoamExporter: initial_airflow_check_interval "
                    "must not exceed airflow_warmup_time.");
            if(options.use_fan_startup_ramp) {
                validate_positive_finite(
                    options.fan_startup_ramp_time,
                    "fan_startup_ramp_time");
                if(options.fan_startup_ramp_steps < 2)
                    throw std::invalid_argument(
                        "OpenFoamExporter: fan_startup_ramp_steps must "
                        "be at least 2.");
                if(options.fan_startup_ramp_time >
                   options.airflow_warmup_time)
                    throw std::invalid_argument(
                        "OpenFoamExporter: fan_startup_ramp_time must not "
                        "exceed airflow_warmup_time.");
            }
            validate_positive_finite(
                options.frozen_flow_maximum_time_step,
                "frozen_flow_maximum_time_step");
            validate_positive_finite(
                options.frozen_flow_maximum_courant_number,
                "frozen_flow_maximum_courant_number");
            validate_positive_finite(
                options.airflow_refresh_interval,
                "airflow_refresh_interval");
            validate_positive_finite(
                options.airflow_refresh_duration,
                "airflow_refresh_duration");
            validate_positive_finite(
                options.airflow_refresh_check_interval,
                "airflow_refresh_check_interval");
            validate_positive_finite(
                options.airflow_refresh_maximum_courant_number,
                "airflow_refresh_maximum_courant_number");
            validate_positive_finite(
                options.maximum_airflow_refresh_duration,
                "maximum_airflow_refresh_duration");
            validate_positive_finite(
                options.maximum_mass_imbalance_fraction,
                "maximum_mass_imbalance_fraction");
            validate_positive_finite(
                options.maximum_device_flow_change_fraction,
                "maximum_device_flow_change_fraction");
            validate_positive_finite(
                options.maximum_velocity_rms_change_fraction,
                "maximum_velocity_rms_change_fraction");
            validate_positive_finite(
                options.maximum_accepted_velocity_rms_change_fraction,
                "maximum_accepted_velocity_rms_change_fraction");
            validate_positive_finite(
                options.minimum_tracked_boundary_flow_fraction,
                "minimum_tracked_boundary_flow_fraction");
            if(options.minimum_tracked_boundary_flow_fraction >= 1.0)
                throw std::invalid_argument(
                    "OpenFoamExporter: minimum_tracked_boundary_flow_fraction "
                    "must be less than 1.");
            if(options.stop_when_thermally_converged) {
                validate_positive_finite(
                    options.minimum_thermal_convergence_time,
                    "minimum_thermal_convergence_time");
                validate_positive_finite(
                    options.thermal_convergence_reference_interval,
                    "thermal_convergence_reference_interval");
                validate_positive_finite(
                    options.maximum_temperature_change,
                    "maximum_temperature_change");
                validate_positive_finite(
                    options.maximum_component_average_temperature_change,
                    "maximum_component_average_temperature_change");
                if(options.thermal_convergence_required_checkpoints < 1)
                    throw std::invalid_argument(
                        "OpenFoamExporter: "
                        "thermal_convergence_required_checkpoints must "
                        "be positive.");
                if(!options.use_adaptive_airflow_refresh)
                    throw std::invalid_argument(
                        "OpenFoamExporter: thermal convergence stopping "
                        "requires use_adaptive_airflow_refresh=true so the "
                        "final airflow operating point is validated.");
            }
            if(options.maximum_airflow_refresh_duration <
               options.airflow_refresh_duration)
                throw std::invalid_argument(
                    "OpenFoamExporter: maximum_airflow_refresh_duration "
                    "must not be less than airflow_refresh_duration.");
            if(options.airflow_refresh_check_interval >
               options.maximum_airflow_refresh_duration)
                throw std::invalid_argument(
                    "OpenFoamExporter: airflow_refresh_check_interval must "
                    "not exceed maximum_airflow_refresh_duration.");
            if(options.airflow_warmup_time>=options.end_time)
                throw std::invalid_argument(
                    "OpenFoamExporter: airflow_warmup_time must be less "
                    "than end_time.");
            if(options.airflow_refresh_duration>=
               options.airflow_refresh_interval)
                throw std::invalid_argument(
                    "OpenFoamExporter: airflow_refresh_duration must be "
                    "less than airflow_refresh_interval.");
        }
    }

    static void write_control_dict(
        const Mesh& mesh, const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","controlDict","system");
        output.precision(17);
        output <<
            "application     chtMultiRegionFoam;\n"
            "startFrom       latestTime;\n"
            "startTime       0;\n"
            "stopAt          endTime;\n"
            "endTime         " << options.end_time << ";\n"
            "deltaT          " << options.initial_time_step << ";\n"
            "adjustTimeStep  yes;\n"
            "maxCo           " << options.maximum_courant_number << ";\n"
            "maxDeltaT       " << options.maximum_time_step << ";\n"
            "timePrecision   17;\n"
            "writeControl    adjustableRunTime;\n"
            "writeInterval   " << options.field_write_interval << ";\n\n"
            "purgeWrite      " << options.saved_time_directories << ";\n"
            "writeFormat     binary;\n\n"
            "functions\n{\n";
        output <<
            "    fluid_temperature_range\n"
            "    {\n"
            "        type fieldMinMax;\n"
            "        libs (fieldFunctionObjects);\n"
            "        region fluid;\n"
            "        writeControl adjustableRunTime;\n"
            "        writeInterval " << options.report_interval << ";\n"
            "        fields (T);\n"
            "        location true;\n"
            "    }\n"
            "    fluid_temperature_average\n"
            "    {\n"
            "        type volFieldValue;\n"
            "        libs (fieldFunctionObjects);\n"
            "        region fluid;\n"
            "        writeControl adjustableRunTime;\n"
            "        writeInterval " << options.report_interval << ";\n"
            "        regionType all;\n"
            "        operation volAverage;\n"
            "        writeFields false;\n"
            "        fields (T);\n"
            "    }\n"
            "    fluid_temperature_internal_maximum\n"
            "    {\n"
            "        type volFieldValue;\n"
            "        libs (fieldFunctionObjects);\n"
            "        region fluid;\n"
            "        writeControl adjustableRunTime;\n"
            "        writeInterval " << options.report_interval << ";\n"
            "        regionType all;\n"
            "        operation max;\n"
            "        writeFields false;\n"
            "        fields (T);\n"
            "    }\n";
        if(options.use_k_omega_sst)
            output <<
                "    fluid_y_plus\n"
                "    {\n"
                "        type yPlus;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region fluid;\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "    }\n";
        for(const auto& component :
            mesh.get_openfoam_component_regions()) {
            const std::string region = component_region_name(component);
            output <<
                "    " << region << "_temperature_range\n"
                "    {\n"
                "        type fieldMinMax;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region " << region << ";\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "        fields (T);\n"
                "        location true;\n"
                "    }\n"
                "    " << region << "_temperature_average\n"
                "    {\n"
                "        type volFieldValue;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region " << region << ";\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "        regionType all;\n"
                "        operation volAverage;\n"
                "        writeFields false;\n"
                "        fields (T);\n"
                "    }\n"
                "    " << region << "_temperature_internal_maximum\n"
                "    {\n"
                "        type volFieldValue;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region " << region << ";\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "        regionType all;\n"
                "        operation max;\n"
                "        writeFields false;\n"
                "        fields (T);\n"
                "    }\n";
        }
        for(const auto& device :
            mesh.get_openfoam_internal_flow_devices()) {
            const std::string name=internal_device_name(device);
            output <<
                "    " << name << "_temperature_average\n"
                "    {\n"
                "        type volFieldValue;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region fluid;\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "        regionType cellZone;\n"
                "        name " << name << ";\n"
                "        operation volAverage;\n"
                "        writeFields false;\n"
                "        fields (T);\n"
                "    }\n";
        }
        for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
            output <<
                "    " << foam_word(patch.name) << "_mass_flow\n"
                "    {\n"
                "        type surfaceFieldValue;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region fluid;\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "        regionType patch;\n"
                "        name " << foam_word(patch.name) << ";\n"
                "        operation sum;\n"
                "        writeFields false;\n"
                "        fields (phi);\n"
                "    }\n"
                "    " << foam_word(patch.name)
                << "_mass_weighted_temperature\n"
                "    {\n"
                "        type surfaceFieldValue;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region fluid;\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "        regionType patch;\n"
                "        name " << foam_word(patch.name) << ";\n"
                "        operation weightedAverage;\n"
                "        weightField phi;\n"
                "        writeFields false;\n"
                "        fields (T);\n"
                "    }\n";
        }
        if(!mesh.get_openfoam_boundary_patches().empty()) {
            std::string ambient_patches;
            for(std::size_t i=0;
                i<mesh.get_openfoam_boundary_patches().size();++i) {
                if(i) ambient_patches+=" ";
                ambient_patches+=foam_word(
                    mesh.get_openfoam_boundary_patches()[i].name);
            }
            output <<
                "    ambient_net_mass_flow\n"
                "    {\n"
                "        type surfaceFieldValue;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region fluid;\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "        regionType patch;\n"
                "        names (" << ambient_patches << ");\n"
                "        operation sum;\n"
                "        writeFields false;\n"
                "        fields (phi);\n"
                "    }\n";
        }
        output << "}\n";
    }

    static void write_spatial_convergence_dict(
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","spatialConvergenceDict","system");
        output << R"(
functions
{
    readVelocityFields
    {
        type        readFields;
        libs        (fieldFunctionObjects);
        region      fluid;
        fields      (U UPrevious);
    }
    velocityDelta
    {
        type        subtract;
        libs        (fieldFunctionObjects);
        region      fluid;
        fields      (U UPrevious);
        result      velocityDelta;
    }
    velocityDeltaSquared
    {
        type        magSqr;
        libs        (fieldFunctionObjects);
        region      fluid;
        field       velocityDelta;
        result      velocityDeltaSquared;
    }
    velocitySquared
    {
        type        magSqr;
        libs        (fieldFunctionObjects);
        region      fluid;
        field       U;
        result      velocitySquared;
    }
    velocityRmsValues
    {
        type            volFieldValue;
        libs            (fieldFunctionObjects);
        region          fluid;
        regionType      all;
        operation       volAverage;
        postOperation   sqrt;
        writeFields     false;
        fields          (velocityDeltaSquared velocitySquared);
    }
}
)";
    }

    static void write_decompose_par_dict(
        const Mesh& mesh, int process_count,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","decomposeParDict","system");
        output << "numberOfSubdomains " << process_count << ";\n"
               << "method scotch;\n"
               << "regions\n{\n";
        for(const auto& component :
            mesh.get_openfoam_component_regions())
            output << "    " << component_region_name(component) << "\n"
                   << "    {\n"
                   << "        numberOfSubdomains 1;\n"
                   << "        method simple;\n"
                   << "        coeffs { n (1 1 1); }\n"
                   << "    }\n";
        output << "}\n";
    }

    static double kelvin(double temperature) {
        return temperature < 170.0 ? temperature + 273.15 : temperature;
    }

    static std::string component_region_name(
        const Mesh::OpenFoamComponentRegion& component) {
        return foam_word(component.name)+"_"+std::to_string(component.id);
    }

    static std::array<double,3> patch_velocity(
        const Mesh& mesh, int patch_id) {
        std::array<double,3> velocity{0.0,0.0,0.0};
        std::size_t count = 0;
        for(int i=0; i<mesh.get_nx(); ++i)
            for(int j=0; j<mesh.get_ny(); ++j)
                for(int k=0; k<mesh.get_nz(); ++k)
                    for(int axis=0; axis<3; ++axis)
                        for(int side=0; side<2; ++side)
                            if(mesh.get_openfoam_boundary_patch_id(
                                   i,j,k,axis,side) == patch_id) {
                                const Cell& cell = mesh.at(i,j,k);
                                velocity[0] += cell.get_vx();
                                velocity[1] += cell.get_vy();
                                velocity[2] += cell.get_vz();
                                ++count;
                            }
        if(count)
            for(double& value : velocity)
                value /= static_cast<double>(count);
        return velocity;
    }

    static double fluid_initial_temperature(const Mesh& mesh) {
        return kelvin(mesh.get_env().get_T_ambient());
    }

    static double solid_initial_temperature(
        const Mesh& mesh, int component_id) {
        const auto& cells = mesh.get_cells();
        const auto& metadata = mesh.get_openfoam_cell_metadata();
        double sum = 0.0;
        std::size_t count = 0;
        for(std::size_t i=0; i<metadata.size(); ++i)
            if(metadata[i].component_id == component_id) {
                sum += kelvin(cells[i].get_T());
                ++count;
            }
        return count ? sum/static_cast<double>(count)
                     : fluid_initial_temperature(mesh);
    }

    static void write_fluid_fields(
        const Mesh& mesh, const OpenFoamExportOptions& options,
        const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);
        const double temperature = fluid_initial_temperature(mesh);

        {
            const auto path = directory/"T";
            std::ofstream output(path);
            require_stream(output,path);
            write_header(output,"volScalarField","T","0/fluid");
            output.precision(17);
            output << "dimensions [0 0 0 1 0 0 0];\n"
                   << "internalField uniform " << temperature << ";\n"
                   << "boundaryField\n{\n"
                   << "rack_walls\n{\n type zeroGradient;\n}\n";
            for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
                output << foam_word(patch.name) << "\n{\n";
                if(patch.kind == Mesh::OpenFoamBoundaryPatch::Kind::Inlet)
                    output << " type fixedValue;\n value uniform "
                           << temperature << ";\n";
                else
                    output << " type inletOutlet;\n inletValue uniform "
                           << temperature << ";\n value uniform "
                           << temperature << ";\n";
                output << "}\n";
            }
            for(const auto& component :
                mesh.get_openfoam_component_regions())
                output << "fluid_to_" << component_region_name(component)
                       << "\n{\n"
                       << " type compressible::"
                          "turbulentTemperatureRadCoupledMixed;\n"
                       << " Tnbr T;\n kappaMethod fluidThermo;\n"
                       << " useImplicit false;\n qrNbr none;\n qr none;\n"
                       << " value uniform " << temperature << ";\n}\n";
            output << "}\n";
        }
        {
            const auto path = directory/"U";
            std::ofstream output(path);
            require_stream(output,path);
            write_header(output,"volVectorField","U","0/fluid");
            output.precision(17);
            std::array<double,3> initial_velocity{0.0,0.0,0.0};
            if(options.use_fan_curves) {
                for(const auto& patch :
                    mesh.get_openfoam_boundary_patches()) {
                    if(patch.kind ==
                           Mesh::OpenFoamBoundaryPatch::Kind::Inlet &&
                       patch.fan_has_curve) {
                        initial_velocity = patch_velocity(mesh,patch.id);
                        break;
                    }
                }
            }
            output << "dimensions [0 1 -1 0 0 0 0];\n"
                   << "internalField uniform ("
                   << initial_velocity[0] << ' ' << initial_velocity[1]
                   << ' ' << initial_velocity[2] << ");\n"
                   << "boundaryField\n{\n"
                   << "rack_walls\n{\n type noSlip;\n}\n";
            for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
                output << foam_word(patch.name) << "\n{\n";
                if(patch.kind == Mesh::OpenFoamBoundaryPatch::Kind::Inlet &&
                   !(options.use_fan_curves && patch.fan_has_curve)) {
                    const auto velocity = patch_velocity(mesh,patch.id);
                    output << " type fixedValue;\n value uniform ("
                           << velocity[0] << ' ' << velocity[1] << ' '
                           << velocity[2] << ");\n";
                } else {
                    output << " type pressureInletOutletVelocity;\n"
                           << " value uniform (0 0 0);\n";
                }
                output << "}\n";
            }
            for(const auto& component :
                mesh.get_openfoam_component_regions())
                output << "fluid_to_" << component_region_name(component)
                       << "\n{\n type noSlip;\n}\n";
            output << "}\n";
        }
        for(const std::string field : {"p","p_rgh"}) {
            const auto path = directory/field;
            std::ofstream output(path);
            require_stream(output,path);
            write_header(
                output,"volScalarField",field.c_str(),"0/fluid");
            const double reference_pressure =
                options.temperature_dependent_air
                    ? mesh.get_env().get_ambient_pressure()
                    : 101325.0;
            output << "dimensions [1 -1 -2 0 0 0 0];\n"
                   << "internalField uniform " << reference_pressure << ";\n"
                   << "boundaryField\n{\n";
            auto pressure_patch = [&](const std::string& name,
                                      bool outlet,
                                      const Mesh::OpenFoamBoundaryPatch*
                                          patch = nullptr) {
                const bool resisted_vent =
                    patch &&
                    patch->kind ==
                        Mesh::OpenFoamBoundaryPatch::Kind::Vent &&
                    options.use_vent_pressure_loss;
                const bool curve_fan =
                    patch && patch->fan_has_curve &&
                    options.use_fan_curves;
                output << name << "\n{\n";
                if(field == "p" && (resisted_vent || curve_fan)) {
                    output << " type calculated;\n"
                           << " value uniform " << reference_pressure << ";\n";
                } else if(field == "p") {
                    output << (outlet ? " type fixedValue;\n"
                                         " value uniform " +
                                             std::to_string(reference_pressure) +
                                             ";\n"
                                      : " type zeroGradient;\n");
                } else if(curve_fan) {
                    const double ambient_density=
                        options.temperature_dependent_air
                            ? reference_pressure*28.97/
                                (8314.46261815324*
                                 (mesh.get_env().get_T_ambient()+273.15))
                            : mesh.get_env().get_rho();
                    const double scale=
                        ambient_density/patch->fan_rated_density;
                    const double a=scale*patch->fan_curve_a;
                    const double b=scale*patch->fan_curve_b;
                    const double c=scale*patch->fan_curve_c;
                    double q_zero=patch->fan_reference_flow_m3s;
                    if(c>0.0)
                        q_zero=(-b+std::sqrt(b*b+4*c*a))/(2*c);
                    else if(b>0.0) q_zero=a/b;
                    validate_positive_finite(
                        q_zero,"ambient fan curve flow");
                    output << " type fanPressure;\n"
                           << " direction "
                           << (patch->kind ==
                                   Mesh::OpenFoamBoundaryPatch::Kind::Inlet
                                   ? "in" : "out")
                           << ";\n fanCurve\n {\n"
                           << "  type table;\n"
                           << "  outOfBounds clamp;\n"
                           << "  values\n  (\n";
                    constexpr int points=20;
                    for(int i=0;i<=points;++i) {
                        const double q=
                            options.fan_curve_extension_multiplier*q_zero*
                            static_cast<double>(i)/points;
                        output << "   (" << q << ' '
                               << std::max(0.0,a-b*q-c*q*q) << ")\n";
                    }
                    output << "  );\n }\n"
                           << " p0 uniform " << reference_pressure << ";\n"
                           << " value uniform " << reference_pressure << ";\n";
                } else if(resisted_vent) {
                    output << " type prghPressure;\n"
                           << " p uniform " << reference_pressure << ";\n"
                           << " value uniform " << reference_pressure << ";\n";
                } else if(
                    outlet && options.temperature_dependent_air) {
                    output << " type prghPressure;\n"
                           << " p uniform " << reference_pressure << ";\n"
                           << " value uniform " << reference_pressure << ";\n";
                } else {
                    output << (outlet ? " type fixedValue;\n"
                                         " value uniform " +
                                             std::to_string(reference_pressure) +
                                             ";\n"
                                      : " type fixedFluxPressure;\n"
                                         " value uniform " +
                                             std::to_string(reference_pressure) +
                                             ";\n");
                }
                output << "}\n";
            };
            pressure_patch("rack_walls",false,nullptr);
            for(const auto& patch : mesh.get_openfoam_boundary_patches())
                pressure_patch(
                    foam_word(patch.name),
                    patch.kind !=
                        Mesh::OpenFoamBoundaryPatch::Kind::Inlet,
                    &patch);
            for(const auto& component :
                mesh.get_openfoam_component_regions())
                pressure_patch(
                    "fluid_to_"+component_region_name(component),false,nullptr);
            output << "}\n";
        }
        double reference_speed = 0.0;
        for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
            if(patch.kind != Mesh::OpenFoamBoundaryPatch::Kind::Inlet)
                continue;
            const auto velocity = patch_velocity(mesh,patch.id);
            reference_speed = std::max(
                reference_speed,
                std::sqrt(
                    velocity[0]*velocity[0] +
                    velocity[1]*velocity[1] +
                    velocity[2]*velocity[2]));
        }
        const double reference_k = std::max(
            1e-10,1.5*std::pow(
                reference_speed*options.inlet_turbulence_intensity,2));
        const double reference_omega =
            std::sqrt(reference_k)/
            (std::pow(0.09,0.25)*options.turbulence_length_scale);

        if(options.use_k_omega_sst) {
            for(const std::string field : {"k","omega"}) {
                const auto path = directory/field;
                std::ofstream output(path);
                require_stream(output,path);
                write_header(
                    output,"volScalarField",field.c_str(),"0/fluid");
                output.precision(17);
                const bool is_k = field == "k";
                const double internal =
                    is_k ? reference_k : reference_omega;
                output << "dimensions "
                       << (is_k ? "[0 2 -2 0 0 0 0]"
                                : "[0 0 -1 0 0 0 0]")
                       << ";\ninternalField uniform " << internal
                       << ";\nboundaryField\n{\n"
                       << "rack_walls\n{\n type "
                       << (is_k ? "kqRWallFunction"
                                : "omegaWallFunction")
                       << ";\n value uniform " << internal << ";\n}\n";
                for(const auto& patch :
                    mesh.get_openfoam_boundary_patches()) {
                    output << foam_word(patch.name) << "\n{\n";
                    if(patch.kind ==
                       Mesh::OpenFoamBoundaryPatch::Kind::Inlet) {
                        const auto velocity =
                            patch_velocity(mesh,patch.id);
                        const double speed = std::sqrt(
                            velocity[0]*velocity[0] +
                            velocity[1]*velocity[1] +
                            velocity[2]*velocity[2]);
                        const double patch_k = std::max(
                            1e-10,1.5*std::pow(
                                speed*
                                options.inlet_turbulence_intensity,2));
                        const double patch_omega =
                            std::sqrt(patch_k)/
                            (std::pow(0.09,0.25)*
                             options.turbulence_length_scale);
                        output << " type fixedValue;\n value uniform "
                               << (is_k ? patch_k : patch_omega) << ";\n";
                    } else {
                        output << " type inletOutlet;\n"
                               << " inletValue uniform " << internal << ";\n"
                               << " value uniform " << internal << ";\n";
                    }
                    output << "}\n";
                }
                for(const auto& component :
                    mesh.get_openfoam_component_regions())
                    output << "fluid_to_"
                           << component_region_name(component)
                           << "\n{\n type "
                           << (is_k ? "kqRWallFunction"
                                    : "omegaWallFunction")
                           << ";\n value uniform " << internal << ";\n}\n";
                output << "}\n";
            }
            {
                const auto path = directory/"nut";
                std::ofstream output(path);
                require_stream(output,path);
                write_header(output,"volScalarField","nut","0/fluid");
                output << "dimensions [0 2 -1 0 0 0 0];\n"
                       << "internalField uniform 0;\n"
                       << "boundaryField\n{\n"
                       << "rack_walls\n{\n type nutkWallFunction;\n"
                          " value uniform 0;\n}\n";
                for(const auto& patch :
                    mesh.get_openfoam_boundary_patches())
                    output << foam_word(patch.name)
                           << "\n{\n type calculated;\n"
                              " value uniform 0;\n}\n";
                for(const auto& component :
                    mesh.get_openfoam_component_regions())
                    output << "fluid_to_"
                           << component_region_name(component)
                           << "\n{\n type nutkWallFunction;\n"
                              " value uniform 0;\n}\n";
                output << "}\n";
            }
        }
        {
            const auto path = directory/"alphat";
            std::ofstream output(path);
            require_stream(output,path);
            write_header(output,"volScalarField","alphat","0/fluid");
            output.precision(17);
            output << "dimensions [1 -1 -1 0 0 0 0];\n"
                   << "internalField uniform 0;\n"
                   << "boundaryField\n{\n";
            if(options.use_k_omega_sst)
                output << "rack_walls\n{\n"
                       << " type compressible::alphatWallFunction;\n"
                       << " Prt " << options.turbulent_prandtl_number << ";\n"
                       << " value uniform 0;\n}\n";
            else
                output << "rack_walls\n{\n type calculated;\n"
                          " value uniform 0;\n}\n";
            for(const auto& patch :
                mesh.get_openfoam_boundary_patches())
                output << foam_word(patch.name)
                       << "\n{\n type calculated;\n"
                          " value uniform 0;\n}\n";
            for(const auto& component :
                mesh.get_openfoam_component_regions()) {
                output << "fluid_to_" << component_region_name(component)
                       << "\n{\n type ";
                if(options.use_k_omega_sst)
                    output << "compressible::alphatWallFunction;\n"
                           << " Prt "
                           << options.turbulent_prandtl_number << ";\n";
                else
                    output << "calculated;\n";
                output << " value uniform 0;\n}\n";
            }
            output << "}\n";
        }
    }

    static void write_solid_temperature(
        const Mesh& mesh,
        const Mesh::OpenFoamComponentRegion& component,
        const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);
        const auto path = directory/"T";
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"volScalarField","T",
            ("0/"+component_region_name(component)).c_str());
        const double temperature =
            solid_initial_temperature(mesh,component.id);
        output.precision(17);
        output << "dimensions [0 0 0 1 0 0 0];\n"
               << "internalField uniform " << temperature << ";\n"
               << "boundaryField\n{\n"
               << component_region_name(component) << "_to_fluid\n{\n"
               << " type compressible::"
                  "turbulentTemperatureRadCoupledMixed;\n"
               << " Tnbr T;\n kappaMethod solidThermo;\n"
               << " useImplicit false;\n qrNbr none;\n qr none;\n"
               << " value uniform " << temperature << ";\n"
               << "}\n"
               << "\".*\"\n{\n"
               << " type zeroGradient;\n"
               << "}\n}\n";

        const auto pressure_path = directory/"p";
        std::ofstream pressure(pressure_path);
        require_stream(pressure,pressure_path);
        write_header(
            pressure,"volScalarField","p",
            ("0/"+component_region_name(component)).c_str());
        pressure << "dimensions [1 -1 -2 0 0 0 0];\n"
                 << "internalField uniform 101325;\n"
                 << "boundaryField\n{\n"
                 << component_region_name(component) << "_to_fluid\n{\n"
                 << " type calculated;\n value uniform 101325;\n"
                 << "}\n"
                 << "\".*\"\n{\n"
                 << " type calculated;\n value uniform 101325;\n"
                 << "}\n}\n";
    }

    static void write_fv_schemes(const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","fvSchemes","system");
        output <<
            "ddtSchemes\n{\n    default Euler;\n}\n"
            "gradSchemes\n{\n    default Gauss linear;\n}\n"
            "divSchemes\n{\n    default none;\n}\n"
            "laplacianSchemes\n"
            "{\n    default Gauss linear corrected;\n}\n"
            "interpolationSchemes\n{\n    default linear;\n}\n"
            "snGradSchemes\n{\n    default corrected;\n}\n"
            "wallDist\n{\n    method meshWave;\n}\n";
    }

    static void write_fv_solution(
        const Mesh& mesh,
        const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","fvSolution","system");
        const double reference_pressure =
            options.temperature_dependent_air
                ? mesh.get_env().get_ambient_pressure() : 101325.0;
        output << "PIMPLE\n{\n    nOuterCorrectors "
               << effective_pimple_outer_correctors(options) << ";\n"
               << "    pRefCell 0;\n"
               << "    pRefValue " << reference_pressure << ";\n}\n";
    }

    static void write_fluid_thermophysical_properties(
        const Mesh& mesh, const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
        const Environment& env = mesh.get_env();
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","thermophysicalProperties",
            "constant/fluid");
        output.precision(17);
        output <<
            "thermoType\n{\n"
            " type heRhoThermo;\n mixture pureMixture;\n"
            " transport "
            << (options.temperature_dependent_air ? "sutherland" : "const")
            << ";\n thermo "
            << (options.temperature_dependent_air ? "janaf" : "hConst")
            << ";\n"
            " equationOfState "
            << (options.temperature_dependent_air ? "perfectGas" : "rhoConst")
            << ";\n specie specie;\n"
            " energy sensibleEnthalpy;\n}\n"
            // Rack ventilation is low-Mach. Excluding transient pressure
            // work prevents fan pressure corrections from producing
            // non-physical temperature changes in short inlet ducts.
            "dpdt no;\n"
            "mixture\n{\n"
            " specie { molWeight 28.97; }\n";
        if(options.temperature_dependent_air) {
            const double reference_temperature =
                env.get_T_ambient() + 273.15;
            constexpr double universal_gas_constant = 8314.46261815324;
            constexpr double mol_weight = 28.97;
            const double cp_over_r =
                env.get_cp()/(universal_gas_constant/mol_weight);
            const double as = env.get_mu() *
                (1.0 + options.sutherland_temperature/reference_temperature) /
                std::sqrt(reference_temperature);
            output <<
                " thermodynamics\n"
                " {\n"
                "  Tlow 200; Thigh 6000; Tcommon 1000;\n"
                "  lowCpCoeffs (" << cp_over_r << " 0 0 0 0 0 0);\n"
                "  highCpCoeffs (" << cp_over_r << " 0 0 0 0 0 0);\n"
                " }\n"
                " transport { As " << as << "; Ts "
                << options.sutherland_temperature << "; Pr "
                << env.get_pr() << "; }\n";
        } else
            output <<
                " equationOfState { rho " << env.get_rho() << "; }\n"
                " thermodynamics { Cp " << env.get_cp() << "; Hf 0; }\n"
                " transport { mu " << env.get_mu() << "; Pr "
                << env.get_pr() << "; }\n";
        output << "}\n";
    }

    static void write_solid_thermophysical_properties(
        const Mesh::OpenFoamComponentRegion& component,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","thermophysicalProperties",
            ("constant/"+component_region_name(component)).c_str());
        output.precision(17);
        output <<
            "thermoType\n{\n"
            " type heSolidThermo;\n mixture pureMixture;\n"
            " transport constIso;\n thermo hConst;\n"
            " equationOfState rhoConst;\n specie specie;\n"
            " energy sensibleEnthalpy;\n}\n"
            "mixture\n{\n"
            " specie { molWeight 50; }\n"
            " transport { kappa " << component.conductivity << "; }\n"
            " thermodynamics { Hf 0; Cp " << component.cp << "; }\n"
            " equationOfState { rho " << component.rho << "; }\n}\n";
    }

    static void write_radiation_off(
        const std::filesystem::path& path,
        const std::string& location) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","radiationProperties",location.c_str());
        output << "radiation off;\nradiationModel none;\n";
    }

    static void write_fluid_region_schemes(
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","fvSchemes","system/fluid");
        output <<
            "ddtSchemes { default Euler; }\n"
            "gradSchemes { default Gauss linear; }\n"
            "divSchemes\n{\n"
            " default none;\n"
            " div(phi,U) Gauss upwind;\n"
            " div(phi,K) Gauss linear;\n"
            " div(phi,h) Gauss upwind;\n"
            " turbulence Gauss upwind;\n"
            " div(phi,k) $turbulence;\n"
            " div(phi,omega) $turbulence;\n"
            " div(((rho*nuEff)*dev2(T(grad(U))))) Gauss linear;\n"
            "}\n"
            "laplacianSchemes { default Gauss linear corrected; }\n"
            "interpolationSchemes { default linear; }\n"
            "snGradSchemes { default corrected; }\n"
            "wallDist { method meshWave; }\n";
    }

    static void write_fluid_region_solution(
        const Mesh& mesh,
        const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","fvSolution","system/fluid");
        output <<
            "solvers\n{\n"
            " rho { solver PCG; preconditioner DIC; "
                "tolerance 1e-7; relTol 0.1; }\n"
            " rhoFinal { $rho; relTol 0; }\n"
            " p_rgh { solver GAMG; tolerance 1e-7; relTol 0.01; "
                "smoother GaussSeidel; }\n"
            " p_rghFinal { $p_rgh; relTol 0; }\n"
            " \"(U|h|k|omega)\" { solver PBiCGStab; preconditioner DILU; "
                "tolerance 1e-7; relTol 0.1; }\n"
            " \"(U|h|k|omega)Final\" { $U; relTol 0; }\n"
            "}\n"
            "PIMPLE\n{\n momentumPredictor yes;\n nCorrectors "
            << effective_pimple_pressure_correctors(options) << ";\n"
            " nNonOrthogonalCorrectors 0;\n"
            " pRefCell 0;\n pRefValue "
            << (options.temperature_dependent_air
                    ? mesh.get_env().get_ambient_pressure() : 101325.0)
            << ";\n"
            " frozenFlow false;\n semiFrozenFlow false;\n"
            " thermalOnlyFlow false;\n}\n"
            "relaxationFactors\n{\n equations { \"h.*\" 1; \"U.*\" "
            << (options.use_fan_curves ? 0.7 : 1.0)
            << "; \"(k|omega).*\" "
            << (options.use_fan_curves ? 0.7 : 1.0) << "; }\n}\n";
    }

    static void write_solid_region_schemes(
        const std::filesystem::path& path,
        const std::string& region) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","fvSchemes",("system/"+region).c_str());
        output <<
            "ddtSchemes { default Euler; }\n"
            "gradSchemes { default Gauss linear; }\n"
            "divSchemes { default none; }\n"
            "laplacianSchemes\n{\n"
            " default none;\n laplacian(alpha,h) Gauss linear corrected;\n"
            "}\n"
            "interpolationSchemes { default linear; }\n"
            "snGradSchemes { default corrected; }\n";
    }

    static void write_solid_region_solution(
        const std::filesystem::path& path,
        const std::string& region) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","fvSolution",("system/"+region).c_str());
        output <<
            "solvers\n{\n"
            " h { solver PCG; preconditioner DIC; "
                "tolerance 1e-6; relTol 0.1; }\n"
            " hFinal { $h; relTol 0; }\n"
            "}\n"
            "PIMPLE { nNonOrthogonalCorrectors 0; }\n";
    }

    static void write_solid_fv_options(
        const Mesh& mesh,
        const Mesh::OpenFoamComponentRegion& component,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        const std::string region = component_region_name(component);
        write_header(
            output,"dictionary","fvOptions",("constant/"+region).c_str());
        bool wrote_source = false;
        for(const auto& source : mesh.get_openfoam_heat_source_regions()) {
            if(source.fluid || source.component_id != component.id) continue;
            wrote_source = true;
            const std::string set_name = heat_source_set_name(source);
            output << set_name << "_energy\n{\n"
                   << " type scalarSemiImplicitSource;\n"
                   << " active true;\n"
                   << " selectionMode cellZone;\n"
                   << " cellZone " << set_name << ";\n"
                   << " volumeMode absolute;\n"
                   << " sources { h (" << source.watts << " 0); }\n"
                   << "}\n";
        }
        if(!wrote_source) output << "// No stamped heat sources.\n";
    }

    static void write_fluid_fv_options(
        const Mesh& mesh, const OpenFoamExportOptions& options,
        const std::filesystem::path& path,
        const bool include_heat_sources=true) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","fvOptions","constant/fluid");
        output.precision(17);
        const double pressure = options.temperature_dependent_air
            ? mesh.get_env().get_ambient_pressure() : 101325.0;
        const double rho = options.temperature_dependent_air
            ? pressure*28.97/
                (8314.46261815324*
                 (mesh.get_env().get_T_ambient()+273.15))
            : mesh.get_env().get_rho();
        if(include_heat_sources) {
            for(const auto& source : mesh.get_openfoam_heat_source_regions()) {
                if(!source.fluid) continue;
                const std::string set_name = heat_source_set_name(source);
                output << set_name << "_energy\n{\n"
                       << " type scalarSemiImplicitSource;\n"
                       << " active true;\n"
                       << " selectionMode cellZone;\n"
                       << " cellZone " << set_name << ";\n"
                       << " volumeMode absolute;\n"
                       << " sources { h (" << source.watts << " 0); }\n"
                       << "}\n";
            }
        }
        for(const auto& device :
            mesh.get_openfoam_internal_flow_devices()) {
            const std::string name = internal_device_name(device);
            std::array<double,3> e1 = device.direction;
            const double magnitude = std::sqrt(
                e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]);
            for(double& value : e1) value /= magnitude;
            const std::array<double,3> e3 =
                std::abs(e1[2]) < 0.9
                    ? std::array<double,3>{0.0,0.0,1.0}
                    : std::array<double,3>{0.0,1.0,0.0};
            if(device.kind ==
               Mesh::OpenFoamInternalFlowDevice::Kind::Fan) {
                const double scale = rho/device.rated_density;
                const double a = scale*device.curve_a;
                const double b = scale*device.curve_b;
                const double c = scale*device.curve_c;
                double q_zero = device.reference_flow_m3s;
                if(c > 0.0)
                    q_zero=(-b+std::sqrt(b*b+4*c*a))/(2*c);
                else if(b > 0.0) q_zero=a/b;
                validate_positive_finite(q_zero,"internal fan curve flow");
                output << name << "\n{\n"
                       << " type fanMomentumSource;\n"
                       << " selectionMode cellZone;\n"
                       << " cellZone " << name << ";\n"
                       << " faceZone " << name << "_faces;\n"
                       << " flowDir (" << e1[0] << ' ' << e1[1] << ' '
                       << e1[2] << ");\n"
                       << " thickness " << device.thickness << ";\n"
                       << " fanCurve\n {\n  type table;\n"
                       << "  outOfBounds clamp;\n  values\n  (\n";
                constexpr int points=20;
                for(int i=0;i<=points;++i) {
                    const double q=
                        options.fan_curve_extension_multiplier*q_zero*
                        static_cast<double>(i)/points;
                    output << "   (" << q << ' '
                           << std::max(0.0,a-b*q-c*q*q) << ")\n";
                }
                output << "  );\n }\n}\n";
            } else {
                double volume=0.0;
                for(std::size_t cell : device.cells)
                    volume += mesh.get_cells().at(cell).volume();
                const double gross_area=volume/device.thickness;
                const double inertial=
                    gross_area*gross_area/
                    (device.discharge_coefficient*
                     device.discharge_coefficient*
                     device.free_area_m2*device.free_area_m2*
                     device.thickness);
                output << name << "\n{\n"
                       << " type explicitPorositySource;\n"
                       << " explicitPorositySourceCoeffs\n {\n"
                       << "  selectionMode cellZone;\n"
                       << "  cellZone " << name << ";\n"
                       << "  type DarcyForchheimer;\n"
                       << "  d (0 -1000 -1000);\n"
                       << "  f (" << inertial << " -1000 -1000);\n"
                       << "  coordinateSystem\n  {\n"
                       << "   origin (0 0 0);\n"
                       << "   e1 (" << e1[0] << ' ' << e1[1] << ' '
                       << e1[2] << ");\n"
                       << "   e3 (" << e3[0] << ' ' << e3[1] << ' '
                       << e3[2] << ");\n"
                       << "  }\n }\n}\n";
            }
        }
        bool wrote_external=false;
        for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
            if(!is_external_source_device(patch,options)) continue;
            wrote_external=true;
            const std::string name=external_device_name(patch);
            std::array<double,3> e1=patch.direction;
            const double magnitude=std::sqrt(
                e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]);
            validate_positive_finite(magnitude,"external device direction");
            for(double& value : e1) value/=magnitude;
            const std::array<double,3> e3=
                std::abs(e1[2])<0.9
                    ? std::array<double,3>{0.0,0.0,1.0}
                    : std::array<double,3>{0.0,1.0,0.0};
            validate_positive_finite(
                patch.source_zone_thickness,
                "external source-zone thickness");
            if(patch.adjacent_cells.empty())
                throw std::invalid_argument(
                    "OpenFoamExporter: external source zone has no cells.");
            if(patch.fan_has_curve && options.use_fan_curves) {
                const double scale=rho/patch.fan_rated_density;
                const double a=scale*patch.fan_curve_a;
                const double b=scale*patch.fan_curve_b;
                const double c=scale*patch.fan_curve_c;
                double q_zero=patch.fan_reference_flow_m3s;
                if(c>0.0)
                    q_zero=(-b+std::sqrt(b*b+4*c*a))/(2*c);
                else if(b>0.0) q_zero=a/b;
                validate_positive_finite(q_zero,"external fan curve flow");
                output << name << "\n{\n"
                       << " type fanMomentumSource;\n"
                       << " selectionMode cellZone;\n"
                       << " cellZone " << name << ";\n"
                       << " faceZone " << name << "_faces;\n"
                       << " flowDir (" << e1[0] << ' ' << e1[1] << ' '
                       << e1[2] << ");\n"
                       << " thickness " << patch.source_zone_thickness
                       << ";\n fanCurve\n {\n  type table;\n"
                       << "  outOfBounds clamp;\n  values\n  (\n";
                constexpr int points=20;
                for(int i=0;i<=points;++i) {
                    const double q=
                        options.fan_curve_extension_multiplier*q_zero*
                        static_cast<double>(i)/points;
                    output << "   (" << q << ' '
                           << std::max(0.0,a-b*q-c*q*q) << ")\n";
                }
                output << "  );\n }\n}\n";
            } else {
                double volume=0.0;
                for(std::size_t cell : patch.adjacent_cells)
                    volume+=mesh.get_cells().at(cell).volume();
                const double gross_area=
                    volume/patch.source_zone_thickness;
                const double inertial=
                    gross_area*gross_area/
                    (patch.vent_discharge_coefficient*
                     patch.vent_discharge_coefficient*
                     patch.vent_free_area_m2*
                     patch.vent_free_area_m2*
                     patch.source_zone_thickness);
                output << name << "\n{\n"
                       << " type explicitPorositySource;\n"
                       << " explicitPorositySourceCoeffs\n {\n"
                       << "  selectionMode cellZone;\n"
                       << "  cellZone " << name << ";\n"
                       << "  type DarcyForchheimer;\n"
                       << "  d (0 -1000 -1000);\n"
                       << "  f (" << inertial << " -1000 -1000);\n"
                       << "  coordinateSystem\n  {\n"
                       << "   origin (0 0 0);\n"
                       << "   e1 (" << e1[0] << ' ' << e1[1] << ' '
                       << e1[2] << ");\n"
                       << "   e3 (" << e3[0] << ' ' << e3[1] << ' '
                       << e3[2] << ");\n"
                       << "  }\n }\n}\n";
            }
        }
        if(mesh.get_openfoam_internal_flow_devices().empty() &&
           !wrote_external)
            output << "// No stamped flow devices.\n";
    }

    static void write_cht_case_files(
        const Mesh& mesh,
        const OpenFoamExportOptions& options,
        const std::filesystem::path& case_directory) {
        std::filesystem::create_directories(case_directory/"constant"/"fluid");
        std::filesystem::create_directories(case_directory/"system"/"fluid");
        write_fluid_fields(mesh,options,case_directory/"0"/"fluid");
        write_fluid_thermophysical_properties(
            mesh,options,case_directory/"constant"/"fluid"/
                "thermophysicalProperties");
        write_radiation_off(
            case_directory/"constant"/"fluid"/"radiationProperties",
            "constant/fluid");
        write_fluid_fv_options(
            mesh,options,case_directory/"constant"/"fluid"/"fvOptions");
        std::filesystem::copy_file(
            case_directory/"constant"/"fluid"/"fvOptions",
            case_directory/"constant"/"fluid"/"fvOptions.fullFan",
            std::filesystem::copy_options::overwrite_existing);
        write_fluid_fv_options(
            mesh,options,
            case_directory/"constant"/"fluid"/"fvOptions.flowOnly",
            false);
        {
            const auto path =
                case_directory/"constant"/"fluid"/"turbulenceProperties";
            std::ofstream output(path);
            require_stream(output,path);
            write_header(
                output,"dictionary","turbulenceProperties",
                "constant/fluid");
            if(options.use_k_omega_sst)
                output <<
                    "simulationType RAS;\n"
                    "RAS\n{\n"
                    " RASModel kOmegaSST;\n"
                    " turbulence on;\n"
                    " printCoeffs on;\n"
                    "}\n";
            else
                output << "simulationType laminar;\n";
        }
        write_fluid_region_schemes(
            case_directory/"system"/"fluid"/"fvSchemes");
        write_fluid_region_solution(
            mesh,options,
            case_directory/"system"/"fluid"/"fvSolution");
        write_region_decompose_include(
            case_directory/"system"/"fluid"/"decomposeParDict");
        write_fluid_decompose_include(
            case_directory/"system"/"fluid"/"decomposeParDict");

        for(const auto& component :
            mesh.get_openfoam_component_regions()) {
            const std::string region = component_region_name(component);
            std::filesystem::create_directories(
                case_directory/"constant"/region);
            std::filesystem::create_directories(
                case_directory/"system"/region);
            write_solid_temperature(
                mesh,component,case_directory/"0"/region);
            write_solid_thermophysical_properties(
                component,case_directory/"constant"/region/
                    "thermophysicalProperties");
            write_radiation_off(
                case_directory/"constant"/region/"radiationProperties",
                "constant/"+region);
            write_solid_fv_options(
                mesh,component,case_directory/"constant"/region/"fvOptions");
            write_solid_region_schemes(
                case_directory/"system"/region/"fvSchemes",region);
            write_solid_region_solution(
                case_directory/"system"/region/"fvSolution",region);
            write_region_decompose_include(
                case_directory/"system"/region/"decomposeParDict");
        }
        {
            const auto path = case_directory/"constant"/"g";
            std::ofstream output(path);
            require_stream(output,path);
            write_header(output,"uniformDimensionedVectorField","g","constant");
            output << "dimensions [0 1 -2 0 0 0 0];\nvalue ("
                   << options.gravity[0] << ' ' << options.gravity[1] << ' '
                   << options.gravity[2] << ");\n";
        }
    }

    static void write_region_decompose_include(
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","decomposeParDict",
            ("system/"+path.parent_path().filename().string()).c_str());
        output << "#include \"../decomposeParDict\"\n";
    }

    static void write_fluid_decompose_include(
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","decomposeParDict","system/fluid");
        output <<
            "#include \"../decomposeParDict\"\n"
            "constraints\n"
            "{\n"
            "    coupledInterfaces\n"
            "    {\n"
            "        type singleProcessorFaceSets;\n"
            "        sets ((chtCoupledInterfaces 0));\n"
            "        enabled true;\n"
            "    }\n"
            "}\n";
    }

    static void write_interface_toposet_dict(
        const Mesh& mesh,
        const std::filesystem::path& case_directory) {
        const auto path =
            case_directory/"system"/"topoSetDict_fluid_interfaces";
        std::ofstream output(path);
        require_stream(output,path);
        write_header(
            output,"dictionary","topoSetDict_fluid_interfaces","system");
        output << "actions\n(\n";
        bool first = true;
        for(const auto& component :
            mesh.get_openfoam_component_regions()) {
            output <<
                "    {\n"
                "        name chtCoupledInterfaces;\n"
                "        type faceSet;\n"
                "        action " << (first ? "new" : "add") << ";\n"
                "        source patchToFace;\n"
                "        patch fluid_to_"
                << component_region_name(component) << ";\n"
                "    }\n";
            first = false;
        }
        output << ");\n";
    }

    static void write_region_preparation_script(
        const Mesh& mesh, const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
        std::ofstream output(path,std::ios::binary);
        require_stream(output,path);
        output <<
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n\n"
            "case_dir=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
            "foam_launcher=\"${OPENFOAM_LAUNCHER:-openfoam2606}\"\n\n"
            "if [[ -f \"$case_dir/.openfoam_regions_prepared\" ]]; then\n"
            "    echo \"Region meshes already prepared; reusing existing "
                "topology.\"\n"
            "    exit 0\n"
            "fi\n\n"
            "\"$foam_launcher\" splitMeshRegions "
                "-case \"$case_dir\" -cellZonesOnly -overwrite\n"
            "\n";
        output <<
            "\"$foam_launcher\" topoSet "
                "-case \"$case_dir\" -region fluid "
                "-latestTime "
                "-dict \"$case_dir/system/topoSetDict_fluid_interfaces\"\n";
        const auto& components = mesh.get_openfoam_component_regions();
        for(const auto& source : mesh.get_openfoam_heat_source_regions()) {
            if(source.component_id < 0 ||
               static_cast<std::size_t>(source.component_id) >=
                   components.size())
                throw std::runtime_error(
                    "OpenFoamExporter: heat source has no component region.");
            const std::string region = source.fluid
                ? std::string("fluid")
                : foam_word(components[
                    static_cast<std::size_t>(source.component_id)].name)
                    +"_"+std::to_string(source.component_id);
            output <<
                "\"$foam_launcher\" topoSet "
                    "-case \"$case_dir\" -region " << region << " -time 0 "
                << "-dict \"$case_dir/system/topoSetDict_"
                << heat_source_set_name(source) << "\"\n";
        }
        for(const auto& device :
            mesh.get_openfoam_internal_flow_devices()) {
            const std::string name = internal_device_name(device);
            output <<
                "\"$foam_launcher\" topoSet "
                    "-case \"$case_dir\" -region fluid -time 0 "
                << "-dict \"$case_dir/system/topoSetDict_"
                << name << "\"\n";
        }
        for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
            if(!is_external_source_device(patch,options)) continue;
            const std::string name=external_device_name(patch);
            output <<
                "\"$foam_launcher\" topoSet "
                    "-case \"$case_dir\" -region fluid -time 0 "
                << "-dict \"$case_dir/system/topoSetDict_"
                << name << "\"\n";
        }
        output <<
            "\n"
            "check_mesh_log=\"$case_dir/checkMesh.prepare.log\"\n"
            "\"$foam_launcher\" checkMesh "
                "-case \"$case_dir\" -allRegions "
                "-allGeometry -allTopology 2>&1 | tee \"$check_mesh_log\"\n\n"
            "touch \"$case_dir/.openfoam_regions_prepared\"\n"
            "if grep -Eq 'Failed [1-9][0-9]* mesh checks' "
                "\"$check_mesh_log\"; then\n"
            "    echo \"WARNING: region meshes were prepared, but full "
                "checkMesh reported failures. Review $check_mesh_log before "
                "treating results as validated.\" >&2\n"
            "else\n"
            "    echo \"Region meshes prepared successfully.\"\n"
            "fi\n";
    }

    static void write_run_script(const std::filesystem::path& path) {
        std::ofstream output(path,std::ios::binary);
        require_stream(output,path);
        output <<
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n\n"
            "case_dir=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
            "foam_launcher=\"${OPENFOAM_LAUNCHER:-openfoam2606}\"\n"
            "run_lock=\"$case_dir/.thermal_solver_run.lock\"\n"
            "if ! command -v flock >/dev/null 2>&1; then\n"
            "    echo \"Required command 'flock' is unavailable.\" >&2\n"
            "    exit 4\n"
            "fi\n"
            "exec 9>>\"$run_lock\"\n"
            "if ! flock -n 9; then\n"
            "    owner=\"$(tail -n 1 \"$run_lock\" 2>/dev/null || true)\"\n"
            "    echo \"Another thermal solver is already writing this case${owner:+ (PID $owner)}.\" >&2\n"
            "    exit 3\n"
            "fi\n"
            ": >\"$run_lock\"\n"
            "printf '%s\\n' \"$$\" >&9\n\n"
            "bash \"$case_dir/prepare_regions.sh\"\n"
            "\"$foam_launcher\" chtMultiRegionFoam -case \"$case_dir\" \"$@\"\n";
    }

    static void write_parallel_run_script(
        const Mesh& mesh,
        const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
        double fluid_volume_m3=0.0;
        const auto& cells=mesh.get_cells();
        const auto& metadata=mesh.get_openfoam_cell_metadata();
        if(cells.size()!=metadata.size())
            throw std::logic_error(
                "OpenFoamExporter: missing cell metadata while writing "
                "parallel runner.");
        for(std::size_t cell=0;cell<cells.size();++cell)
            if(metadata[cell].region_type==
               Mesh::OpenFoamCellMetadata::RegionType::Fluid)
                fluid_volume_m3+=cells[cell].volume();
        validate_positive_finite(fluid_volume_m3,"fluid volume");
        std::ofstream output(path,std::ios::binary);
        require_stream(output,path);
        output.precision(17);
        output <<
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n\n"
            "case_dir=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
            "foam_launcher=\"${OPENFOAM_LAUNCHER:-openfoam2606}\"\n"
            "processes=\"${1:-" << options.parallel_processes << "}\"\n"
            "mode=\"${2:-run}\"\n"
            "requested_end=\"${3:-"
            << (options.use_multirate_thermal
                    ? options.end_time : 10.0) << "}\"\n\n"
            "airflow_refresh_interval=\"${4:-"
            << options.airflow_refresh_interval << "}\"\n\n"
            "if ! [[ \"$processes\" =~ ^[1-9][0-9]*$ ]] || "
                "(( processes < 2 )); then\n"
            "    echo \"Process count must be an integer of at least two "
                "for OpenFOAM parallel mode.\" >&2\n"
            "    exit 2\n"
            "fi\n"
            "if [[ \"$mode\" != \"run\" && \"$mode\" != \"--warm-start\" "
                "&& \"$mode\" != \"--multirate\" ]]; then\n"
            "    echo \"Usage: $0 [processes] "
                "[--warm-start|--multirate [end-time] "
                "[airflow-refresh-interval]]\" >&2\n"
            "    exit 2\n"
            "fi\n"
            "if [[ \"$mode\" != \"run\" ]] && { "
                "! [[ \"$requested_end\" =~ ^[0-9]+([.][0-9]+)?$ ]] || "
                "! awk -v v=\"$requested_end\" "
                "'BEGIN { exit !(v>0) }'; }; then\n"
            "    echo \"Requested end time must be a positive number.\" >&2\n"
            "    exit 2\n"
            "fi\n\n"
            "if [[ \"$mode\" == \"--multirate\" ]] && "
                "! [[ \"$airflow_refresh_interval\" =~ "
                "^[0-9]+([.][0-9]+)?$ ]] || "
                "[[ \"$mode\" == \"--multirate\" ]] && "
                "! awk -v v=\"$airflow_refresh_interval\" "
                "'BEGIN { exit !(v>0) }'; then\n"
            "    echo \"Airflow refresh interval must be a positive "
                "number.\" >&2\n"
            "    exit 2\n"
            "fi\n\n"
            "if [[ \"${THERMAL_SOLVER_OPENFOAM_ENV_READY:-0}\" != 1 && "
                "\"$foam_launcher\" != env ]]; then\n"
            "    echo \"Initializing OpenFOAM environment once with "
                "$foam_launcher.\"\n"
            "    exec \"$foam_launcher\" env "
                "THERMAL_SOLVER_OPENFOAM_ENV_READY=1 "
                "OPENFOAM_LAUNCHER=env "
                "\"$case_dir/$(basename \"$0\")\" \"$@\"\n"
            "fi\n\n"
            "run_lock=\"$case_dir/.thermal_solver_run.lock\"\n"
            "if ! command -v flock >/dev/null 2>&1; then\n"
            "    echo \"Required command 'flock' is unavailable.\" >&2\n"
            "    exit 4\n"
            "fi\n"
            "exec 9>>\"$run_lock\"\n"
            "if ! flock -n 9; then\n"
            "    owner=\"$(tail -n 1 \"$run_lock\" 2>/dev/null || true)\"\n"
            "    echo \"Another thermal solver is already writing this case${owner:+ (PID $owner)}.\" >&2\n"
            "    exit 3\n"
            "fi\n"
            ": >\"$run_lock\"\n"
            "printf '%s\\n' \"$$\" >&9\n"
            "summary_log=\"$case_dir/run_summary.log\"\n"
            "summary()\n"
            "{\n"
            "    printf '%s | %s\\n' \"$(date --iso-8601=seconds)\" \"$*\" >> \"$summary_log\"\n"
            "}\n"
            "summary \"run_start mode=$mode processes=$processes requestedEnd=$requested_end airflowRefreshInterval=$airflow_refresh_interval\"\n\n"
            "# Distinct directory spellings can represent the same numeric\n"
            "# OpenFOAM time (for example 730000.1 and\n"
            "# 730000.09999999998). Different utilities may select different\n"
            "# copies, so reject the ambiguity before reading or writing fields.\n"
            "mapfile -t root_time_dirs < <(find \"$case_dir\" -mindepth 1 "
                "-maxdepth 1 -type d -printf '%f\\n' | "
                "awk '$0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' "
                "| sort -g)\n"
            "for ((time_index=1; time_index<${#root_time_dirs[@]}; "
                "++time_index)); do\n"
            "    previous_time=\"${root_time_dirs[$((time_index-1))]}\"\n"
            "    candidate_time=\"${root_time_dirs[$time_index]}\"\n"
            "    if [[ \"$previous_time\" != \"$candidate_time\" ]] && "
                "awk -v a=\"$previous_time\" -v b=\"$candidate_time\" "
                "'BEGIN { d=a-b; if(d<0)d=-d; s=(a<0?-a:a); "
                "t=(b<0?-b:b); if(t>s)s=t; if(s<1)s=1; "
                "exit !(d<=1e-12*s) }'; then\n"
            "        echo \"Ambiguous duplicate OpenFOAM root times: "
                "$previous_time and $candidate_time. Keep only the mapped/"
                "authoritative directory before restarting.\" >&2\n"
            "        exit 8\n"
            "    fi\n"
            "done\n\n"
            "processor_time_complete()\n"
            "{\n"
            "    local candidate=\"$1\" rank field time_dir region_dir region\n"
            "    for ((rank=0; rank<processes; ++rank)); do\n"
            "        time_dir=\"$case_dir/processor${rank}/${candidate}\"\n"
            "        [[ -d \"$time_dir/fluid\" ]] || return 1\n"
            "        for field in T U p p_rgh phi rho k omega nut alphat; do\n"
            "            [[ -f \"$time_dir/fluid/$field\" ]] || return 1\n"
            "        done\n"
            "        for region_dir in \"$case_dir/processor${rank}/constant\"/*; do\n"
            "            [[ -d \"$region_dir/polyMesh\" ]] || continue\n"
            "            region=\"${region_dir##*/}\"\n"
            "            [[ \"$region\" == fluid ]] && continue\n"
            "            [[ -f \"$time_dir/$region/T\" ]] || return 1\n"
            "        done\n"
            "    done\n"
            "    return 0\n"
            "}\n\n"
            "latest_complete_processor_time()\n"
            "{\n"
            "    local candidate\n"
            "    [[ -d \"$case_dir/processor0\" ]] || { echo 0; return; }\n"
            "    while IFS= read -r candidate; do\n"
            "        if processor_time_complete \"$candidate\"; then\n"
            "            echo \"$candidate\"\n"
            "            return\n"
            "        fi\n"
            "    done < <(find \"$case_dir/processor0\" -mindepth 1 -maxdepth 1 "
                "-type d -printf '%f\\n' | awk "
                "'$0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' "
                "| sort -gr)\n"
            "    echo 0\n"
            "}\n\n"
            "discard_incomplete_processor_tail()\n"
            "{\n"
            "    local accepted=\"$1\" rank candidate time_dir\n"
            "    for ((rank=0; rank<processes; ++rank)); do\n"
            "        while IFS= read -r time_dir; do\n"
            "            candidate=\"${time_dir##*/}\"\n"
            "            if awk -v t=\"$candidate\" -v a=\"$accepted\" "
                "'BEGIN { exit !(t>a+1e-12) }'; then\n"
            "                rm -rf -- \"$time_dir\"\n"
            "                echo \"Discarded incomplete processor${rank} "
                "checkpoint: $candidate\"\n"
            "            fi\n"
            "        done < <(find \"$case_dir/processor${rank}\" -mindepth 1 "
                "-maxdepth 1 -type d -printf '%p\\n' | awk -F/ "
                "'$NF ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/')\n"
            "    done\n"
            "}\n\n"
            "# mapFields maps volume fields but not the face-flux field phi.\n"
            "# A nonzero mapped case must establish phi with a coupled warm start\n"
            "# before thermal-only multirate operation. Accept reconstructed or\n"
            "# current processor phi from an ordinary valid restart.\n"
            "if [[ \"$mode\" == \"--multirate\" ]]; then\n"
            "    mapped_root_latest=$(\"$foam_launcher\" foamListTimes "
                "-case \"$case_dir\" -latestTime 2>/dev/null || echo 0)\n"
            "    mapped_root_latest=\"${mapped_root_latest##*$'\\n'}\"\n"
            "    mapped_root_latest=\"${mapped_root_latest:-0}\"\n"
            "    mapped_phi_present=false\n"
            "    if [[ -f \"$case_dir/$mapped_root_latest/fluid/phi\" ]]; then\n"
            "        mapped_phi_present=true\n"
            "    elif [[ -d \"$case_dir/processor0\" ]]; then\n"
            "        mapped_processor_latest=$(latest_complete_processor_time)\n"
            "        if [[ -n \"$mapped_processor_latest\" && "
                "-f \"$case_dir/processor0/$mapped_processor_latest/fluid/phi\" "
                "]]; then\n"
            "            mapped_phi_present=true\n"
            "        fi\n"
            "    fi\n"
            "    if awk -v t=\"$mapped_root_latest\" "
                "'BEGIN { exit !(t>0) }' && "
                "[[ \"$mapped_phi_present\" != true ]]; then\n"
            "        echo \"Mapped/nonzero checkpoint t=$mapped_root_latest has "
                "no face-flux field fluid/phi. Run a short coupled --warm-start "
                "to an end time greater than $mapped_root_latest before "
                "--multirate.\" >&2\n"
            "        exit 9\n"
            "    fi\n"
            "fi\n\n"
            "# Reuse a complete, current decomposition. Reconstruct and "
                "repartition only when processor state is missing, stale, or "
                "uses a different process count.\n"
            "reuse_decomposition=false\n"
            "processor_dirs=(\"$case_dir\"/processor[0-9]*)\n"
            "if [[ -f \"$case_dir/.openfoam_regions_prepared\" && "
                "${#processor_dirs[@]} -eq \"$processes\" ]]; then\n"
            "    reuse_decomposition=true\n"
            "    for ((rank=0; rank<processes; ++rank)); do\n"
            "        if [[ ! -d \"$case_dir/processor${rank}\" ]]; then\n"
            "            reuse_decomposition=false\n"
            "            break\n"
            "        fi\n"
            "    done\n"
            "fi\n"
            "if [[ \"$reuse_decomposition\" == true ]]; then\n"
            "    root_latest=$(\"$foam_launcher\" foamListTimes "
                "-case \"$case_dir\" -latestTime 2>/dev/null || echo 0)\n"
            "    root_latest=\"${root_latest##*$'\\n'}\"\n"
            "    raw_processor_latest=$(\"$foam_launcher\" foamListTimes "
                "-case \"$case_dir\" -processor -latestTime "
                "2>/dev/null || echo 0)\n"
            "    raw_processor_latest=\"${raw_processor_latest##*$'\\n'}\"\n"
            "    processor_latest=$(latest_complete_processor_time)\n"
            "    if awk -v raw=\"${raw_processor_latest:-0}\" "
                "-v complete=\"${processor_latest:-0}\" "
                "'BEGIN { exit !(raw>complete+1e-12) }'; then\n"
            "        discard_incomplete_processor_tail \"$processor_latest\"\n"
            "    fi\n"
            "    if awk -v p=\"${processor_latest:-0}\" "
                "-v r=\"${root_latest:-0}\" "
                "'BEGIN { exit !(p+1e-9<r) }'; then\n"
            "        reuse_decomposition=false\n"
            "    fi\n"
            "fi\n"
            "if [[ \"$reuse_decomposition\" == true ]]; then\n"
            "    echo \"Reusing $processes valid processor partitions at "
                "t=${processor_latest:-0}.\"\n"
            "else\n"
            "    if [[ -d \"$case_dir/processor0\" ]]; then\n"
            "        root_latest=$(\"$foam_launcher\" foamListTimes "
                "-case \"$case_dir\" -latestTime 2>/dev/null || echo 0)\n"
            "        root_latest=\"${root_latest##*$'\\n'}\"\n"
            "        processor_latest=$(latest_complete_processor_time)\n"
            "        if awk -v p=\"${processor_latest:-0}\" "
                "-v r=\"${root_latest:-0}\" "
                "'BEGIN { exit !(p>r) }'; then\n"
            "        echo \"Reconstructing interrupted parallel time "
                "$processor_latest before redecomposition.\"\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" -entry deltaT -set "
            << options.initial_time_step << "\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" -entry writeInterval -set "
            << options.field_write_interval << "\n"
            "        \"$foam_launcher\" reconstructPar -case \"$case_dir\" "
                "-allRegions -latestTime\n"
            "        fi\n"
            "    fi\n"
            "    # decomposePar -force rewrites requested ranks but does not\n"
            "    # remove surplus processor directories when the rank count\n"
            "    # decreases. They would make reconstructPar read stale data.\n"
            "    for processor_dir in \"$case_dir\"/processor[0-9]*; do\n"
            "        [[ -d \"$processor_dir\" ]] || continue\n"
            "        rm -rf -- \"$processor_dir\"\n"
            "    done\n"
            "    bash \"$case_dir/prepare_regions.sh\"\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/decomposeParDict\" "
                "-entry numberOfSubdomains -set \"$processes\"\n"
            "    \"$foam_launcher\" decomposePar -case \"$case_dir\" "
                "-allRegions -latestTime -force\n\n"
            "fi\n\n"
            "full_fan_options=\"$case_dir/constant/fluid/"
                "fvOptions.fullFan\"\n"
            "flow_only_options=\"$case_dir/constant/fluid/"
                "fvOptions.flowOnly\"\n"
            "mapped_state_marker=\"$case_dir/.mapped_initial_state\"\n"
            "fan_options_source=\"$full_fan_options\"\n"
            "install_fluid_options()\n"
            "{\n"
            "    local source=\"$1\" processor_dir\n"
            "    if [[ ! -f \"$source\" ]]; then\n"
            "        echo \"Missing fluid options dictionary: $source\" >&2\n"
            "        return 2\n"
            "    fi\n"
            "    cp \"$source\" \"$case_dir/constant/fluid/fvOptions\"\n"
            "    for processor_dir in \"$case_dir\"/processor[0-9]*; do\n"
            "        [[ -d \"$processor_dir\" ]] || continue\n"
            "        mkdir -p \"$processor_dir/constant/fluid\"\n"
            "        cp \"$source\" \"$processor_dir/constant/fluid/fvOptions\"\n"
            "    done\n"
            "}\n"
            "restore_full_fan_options()\n"
            "{\n"
            "    if [[ -f \"$full_fan_options\" ]]; then\n"
            "        install_fluid_options \"$full_fan_options\"\n"
            "    fi\n"
            "}\n"
            "trap restore_full_fan_options EXIT INT TERM\n"
            "set_fan_scale()\n"
            "{\n"
            "    local scale=\"$1\" processor_dir full_pressure "
                "scaled_pressure measured_scale\n"
            "    awk -v scale=\"$scale\" '\n"
            "        NF==2 && substr($1,1,1)==\"(\" && "
                "index($2,\")\")>0 {\n"
            "            q=$1; dp=$2; gsub(/[()]/,\"\",q); "
                "gsub(/[()\\r]/,\"\",dp);\n"
            "            printf \"   (%s %.17g)\\n\", q, dp*scale; next\n"
            "        }\n"
            "        { print }\n"
            "    ' \"$fan_options_source\" > "
                "\"$case_dir/constant/fluid/fvOptions\"\n"
            "    full_pressure=$(awk '$1==\"(0\" "
                "{ gsub(/[()]/,\"\",$2); print $2; exit }' "
                "\"$fan_options_source\")\n"
            "    scaled_pressure=$(awk '$1==\"(0\" "
                "{ gsub(/[()]/,\"\",$2); print $2; exit }' "
                "\"$case_dir/constant/fluid/fvOptions\")\n"
            "    if [[ -z \"$full_pressure\" ]]; then\n"
            "        echo \"No curve-driven fan sources require scaling.\"\n"
            "        return 0\n"
            "    fi\n"
            "    measured_scale=$(awk -v scaled=\"$scaled_pressure\" "
                "-v full=\"$full_pressure\" "
                "'BEGIN { if(full==0) print 1; else print scaled/full }')\n"
            "    if ! awk -v actual=\"$measured_scale\" -v expected=\"$scale\" "
                "'BEGIN { d=actual-expected; if(d<0)d=-d; "
                "exit !(d<=1e-6) }'; then\n"
            "        echo \"Fan ramp scaling verification failed: "
                "requested=$scale measured=$measured_scale.\" >&2\n"
            "        return 4\n"
            "    fi\n"
            "    echo \"Applied fan pressure scale $scale "
                "(first shutoff pressure $scaled_pressure Pa).\"\n"
            "    for processor_dir in \"$case_dir\"/processor[0-9]*; do\n"
            "        [[ -d \"$processor_dir\" ]] || continue\n"
            "        mkdir -p \"$processor_dir/constant/fluid\"\n"
            "        cp \"$case_dir/constant/fluid/fvOptions\" "
                "\"$processor_dir/constant/fluid/fvOptions\"\n"
            "    done\n"
            "}\n"
            "prune_processor_times()\n"
            "{\n"
            "    local keep=\""
            << options.saved_time_directories
            << "\" processor_root candidate target rank remove_count index\n"
            "    local -a times=()\n"
            "    processor_root=\"$case_dir/processor0\"\n"
            "    [[ -d \"$processor_root\" ]] || return 0\n"
            "    mapfile -t times < <(find \"$processor_root\" "
                "-mindepth 1 -maxdepth 1 -type d -printf '%f\\n' | "
                "awk '$0 != \"0\" && "
                "$0 ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' | "
                "sort -g)\n"
            "    remove_count=$((${#times[@]}-keep))\n"
            "    ((remove_count>0)) || return 0\n"
            "    for ((index=0; index<remove_count; ++index)); do\n"
            "        candidate=\"${times[$index]}\"\n"
            "        for ((rank=0; rank<processes; ++rank)); do\n"
            "            processor_root=\"$case_dir/processor${rank}\"\n"
            "            target=\"$processor_root/$candidate\"\n"
            "            if [[ \"$target\" != \"$processor_root/\"* || "
                "! \"$candidate\" =~ "
                "^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]]; then\n"
            "                echo \"Refusing unsafe checkpoint prune target: "
                "$target\" >&2\n"
            "                return 1\n"
            "            fi\n"
            "            [[ -d \"$target\" ]] && rm -rf -- \"$target\"\n"
            "        done\n"
            "        echo \"Pruned completed processor checkpoint: "
                "$candidate\"\n"
            "    done\n"
            "}\n"
            "run_fan_ramp()\n"
            "{\n"
            "    local solver=\"$1\" start=\"$2\" limit=\"$3\" "
                "step scale target interval ramp_cap ramp_plan ramp_dt "
                "ramp_steps saved_time saved_time_file rank\n"
            "    ramp_current=\"$start\"\n"
            "    if [[ ! -f \"$full_fan_options\" ]]; then\n"
            "        echo \"Missing pristine fan options: "
                "$full_fan_options\" >&2\n"
            "        return 2\n"
            "    fi\n"
            "    echo \"Ramping fan pressure from 0 to 100% over "
            << options.fan_startup_ramp_time << " s in "
            << options.fan_startup_ramp_steps << " stages.\"\n"
            "    for step in $(seq 1 "
            << options.fan_startup_ramp_steps << "); do\n"
            "        target=$(awk -v duration=\""
            << options.fan_startup_ramp_time
            << "\" -v i=\"$step\" -v n=\""
            << options.fan_startup_ramp_steps
            << "\" -v limit=\"$limit\" "
                "'BEGIN { x=duration*i/n; print (x<limit?x:limit) }')\n"
            "        if ! awk -v a=\"$target\" -v b=\"$ramp_current\" "
                "'BEGIN { exit !(a>b) }'; then continue; fi\n"
            "        scale=$(awk -v target=\"$target\" -v duration=\""
            << options.fan_startup_ramp_time
            << "\" 'BEGIN { x=target/duration; print (x<1?x:1) }')\n"
            "        interval=$(awk -v a=\"$target\" -v b=\"$ramp_current\" "
                "'BEGIN { print a-b }')\n"
            "        # Startup has no established flow field for a Courant\n"
            "        # preflight. Seed below the requested Courant limit, then\n"
            "        # let OpenFOAM adapt while clipping each ramp endpoint.\n"
            "        ramp_cap=$(awk -v flow_max=\""
            << options.airflow_maximum_time_step
            << "\" -v co=\""
            << options.maximum_courant_number
            << "\" 'BEGIN { scale=(co<2?co/2:1); print flow_max*scale }')\n"
            "        ramp_plan=$(awk -v maximum=\"$ramp_cap\" "
                "-v remaining=\"$interval\" 'BEGIN { "
                "n=int(remaining/maximum); if(n*maximum<remaining-1e-12)n++; "
                "if(n<1)n=1; printf \"%.17g %d\", remaining/n,n }')\n"
            "        read -r ramp_dt ramp_steps <<<\"$ramp_plan\"\n"
            "        set_fan_scale \"$scale\"\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry startFrom -set latestTime\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry endTime -set \"$target\"\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry adjustTimeStep -set true\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry deltaT -set \"$ramp_dt\"\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry maxDeltaT -set \"$ramp_cap\"\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry maxCo -set "
            << options.maximum_courant_number << "\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry writeControl -set adjustableRunTime\n"
            "        \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry writeInterval -set \"$interval\"\n"
            "        saved_time=$(\"$foam_launcher\" foamListTimes "
                "-case \"$case_dir\" -processor -latestTime 2>/dev/null || true)\n"
            "        saved_time=\"${saved_time##*$'\\n'}\"\n"
            "        if [[ -n \"$saved_time\" ]]; then\n"
            "            for ((rank=0; rank<processes; ++rank)); do\n"
            "                saved_time_file=\"$case_dir/processor"
                "${rank}/${saved_time}/uniform/time\"\n"
            "                [[ -f \"$saved_time_file\" ]] || continue\n"
            "                \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$saved_time_file\" -entry deltaT -set \"$ramp_dt\"\n"
            "                \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$saved_time_file\" -entry deltaT0 -set \"$ramp_dt\"\n"
            "            done\n"
            "        fi\n"
            "        echo \"Fan ramp stage $step/"
            << options.fan_startup_ramp_steps
            << ": scale=$scale, t=$ramp_current -> $target, "
                "deltaT=$ramp_dt, steps=$ramp_steps\"\n"
            "        \"$foam_launcher\" mpirun -np \"$processes\" "
                "\"$solver\" -case \"$case_dir\" -parallel\n"
            "        prune_processor_times\n"
            "        ramp_current=\"$target\"\n"
            "    done\n"
            "    set_fan_scale 1\n"
            "}\n\n";
        if(options.use_multirate_thermal) {
            output <<
                "if [[ \"$mode\" == \"--multirate\" ]]; then\n"
                "    current=$(\"$foam_launcher\" foamListTimes "
                    "-case \"$case_dir\" -processor -latestTime "
                    "2>/dev/null || echo 0)\n"
                "    current=\"${current##*$'\\n'}\"\n"
                "    current=\"${current:-0}\"\n"
                "    initial_convergence_marker="
                    "\"$case_dir/.initial_airflow_converged\"\n"
                "    initial_pending_marker="
                    "\"$case_dir/.initial_airflow_pending\"\n"
                "    refresh_pending_marker="
                    "\"$case_dir/.airflow_refresh_pending\"\n"
                "    if [[ ! -f \"$initial_convergence_marker\" ]]; then\n"
                "        if [[ -f \"$mapped_state_marker\" ]]; then\n"
                "            fan_options_source=\"$full_fan_options\"\n"
                "            restore_full_fan_options\n"
                "            echo \"Mapped initial airflow retains full fluid heat sources.\"\n"
                "        else\n"
                "            fan_options_source=\"$flow_only_options\"\n"
                "            install_fluid_options \"$flow_only_options\"\n"
                "            echo \"Initial airflow uses fans and vents with fluid "
                    "heat sources disabled.\"\n"
                "        fi\n"
                "    fi\n";
            if(options.use_fan_startup_ramp) {
                output <<
                    "    if [[ ! -f \"$mapped_state_marker\" ]] && "
                        "awk -v a=\"$current\" -v end=\""
                    << options.fan_startup_ramp_time
                    << "\" 'BEGIN { exit !(a<end) }'; then\n"
                    "        run_fan_ramp semiFrozenChtMultiRegionFoam "
                        "\"$current\" \"$requested_end\"\n"
                    "        current=\"$ramp_current\"\n"
                    "    fi\n";
            }
            output << "    boundary_flow_names=(";
            for(const auto& patch : mesh.get_openfoam_boundary_patches())
                output << '"' << foam_word(patch.name) << "\" ";
            output <<
                ")\n"
                "    declare -A boundary_flow_lookup=()\n"
                "    for name in \"${boundary_flow_names[@]}\"; do\n"
                "        boundary_flow_lookup[\"$name\"]=1\n"
                "    done\n"
                "    tracked_flow_names=(";
            for(const auto& patch : mesh.get_openfoam_boundary_patches())
                output << '"' << foam_word(patch.name) << "\" ";
            output << ")\n    internal_fan_names=(";
            for(const auto& device :
                mesh.get_openfoam_internal_flow_devices()) {
                if(device.kind ==
                   Mesh::OpenFoamInternalFlowDevice::Kind::Fan)
                    output << '"' << internal_device_name(device) << "\" ";
            }
            output << ")\n    stability_flow_names=(";
            for(const auto& patch : mesh.get_openfoam_boundary_patches())
                output << '"' << foam_word(patch.name) << "\" ";
            for(const auto& device :
                mesh.get_openfoam_internal_flow_devices())
                if(device.kind ==
                   Mesh::OpenFoamInternalFlowDevice::Kind::Fan)
                    output << '"' << internal_device_name(device) << "\" ";
            output << ")\n    component_region_names=(";
            for(const auto& component :
                mesh.get_openfoam_component_regions())
                output << '"' << component_region_name(component) << "\" ";
            output << ")\n    fan_direction_rules=(";
            for(const auto& patch : mesh.get_openfoam_boundary_patches()) {
                if(patch.kind == Mesh::OpenFoamBoundaryPatch::Kind::Inlet)
                    output << '"' << foam_word(patch.name) << ":-1\" ";
                else if(
                    patch.kind == Mesh::OpenFoamBoundaryPatch::Kind::Outlet)
                    output << '"' << foam_word(patch.name) << ":1\" ";
            }
            output <<
                ")\n"
                "    declare -A internal_fan_lookup=()\n"
                "    for name in \"${internal_fan_names[@]}\"; do\n"
                "        internal_fan_lookup[\"$name\"]=1\n"
                "    done\n"
                "    declare -A previous_flows=()\n"
                "    declare -A previous_smoothed_internal_flows=()\n"
                "    airflow_convergence_state=\"$case_dir/.airflow_convergence_state\"\n"
                "    velocity_convergence_state=\"$case_dir/.velocity_convergence_state\"\n"
                "    load_airflow_convergence_state()\n"
                "    {\n"
                "        local state_time state_count name raw smoothed "
                    "index=0 expected_name\n"
                "        [[ -f \"$airflow_convergence_state\" ]] || return 1\n"
                "        read -r state_time state_count < \"$airflow_convergence_state\" || return 1\n"
                "        if ! [[ \"$state_count\" =~ ^[0-9]+$ ]] || "
                    "(( state_count != ${#stability_flow_names[@]} )); then\n"
                "            return 1\n"
                "        fi\n"
                "        if ! awk -v saved=\"$state_time\" -v now=\"$current\" "
                    "'BEGIN { if(saved !~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) exit 1; "
                    "scale=(now<0?-now:now); if(scale<1)scale=1; "
                    "exit !(saved<=now+1e-9*scale) }'; then\n"
                "            return 1\n"
                "        fi\n"
                "        while read -r name raw smoothed; do\n"
                "            (( index < ${#stability_flow_names[@]} )) || return 1\n"
                "            expected_name=\"${stability_flow_names[$index]}\"\n"
                "            [[ \"$name\" == \"$expected_name\" ]] || return 1\n"
                "            if ! awk -v value=\"$raw\" 'BEGIN { "
                    "exit !(value ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) }'; then\n"
                "                return 1\n"
                "            fi\n"
                "            if [[ \"$smoothed\" != - ]] && ! awk -v value=\"$smoothed\" "
                    "'BEGIN { exit !(value ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) }'; then\n"
                "                return 1\n"
                "            fi\n"
                "            previous_flows[\"$name\"]=\"$raw\"\n"
                "            if [[ \"$smoothed\" != - ]]; then\n"
                "                previous_smoothed_internal_flows[\"$name\"]=\"$smoothed\"\n"
                "            fi\n"
                "            index=$((index+1))\n"
                "        done < <(tail -n +2 \"$airflow_convergence_state\")\n"
                "        (( index == ${#stability_flow_names[@]} )) || return 1\n"
                "        echo \"Restored airflow convergence baseline from t=$state_time s.\"\n"
                "        return 0\n"
                "    }\n"
                "    if ! load_airflow_convergence_state; then\n"
                "        previous_flows=()\n"
                "        previous_smoothed_internal_flows=()\n"
                "        if [[ -f \"$airflow_convergence_state\" ]]; then\n"
                "            echo \"Ignoring incompatible, malformed, or future airflow convergence state.\"\n"
                "        fi\n"
                "    fi\n"
                "    latest_air_exchange_time=\"\"\n"
                "    previous_velocity_relative_rms=\"\"\n"
                "    latest_velocity_relative_rms=\"\"\n"
                "    load_velocity_convergence_state()\n"
                "    {\n"
                "        local state_time saved_latest saved_previous\n"
                "        [[ -f \"$velocity_convergence_state\" ]] || return 1\n"
                "        read -r state_time saved_latest saved_previous < "
                    "\"$velocity_convergence_state\" || return 1\n"
                "        if ! awk -v saved=\"$state_time\" -v now=\"$current\" 'BEGIN { "
                    "if(saved !~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) exit 1; "
                    "scale=(now<0?-now:now); if(scale<1)scale=1; "
                    "exit !(saved<=now+1e-9*scale) }'; then return 1; fi\n"
                "        if ! awk -v value=\"$saved_latest\" 'BEGIN { "
                    "exit !(value ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) }'; "
                    "then return 1; fi\n"
                "        latest_velocity_relative_rms=\"$saved_latest\"\n"
                "        if awk -v value=\"$saved_previous\" 'BEGIN { "
                    "exit !(value ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) }'; then\n"
                "            previous_velocity_relative_rms=\"$saved_previous\"\n"
                "        fi\n"
                "        echo \"Restored spatial velocity convergence state from t=$state_time s.\"\n"
                "        return 0\n"
                "    }\n"
                "    if ! load_velocity_convergence_state; then\n"
                "        latest_velocity_relative_rms=\"\"\n"
                "        previous_velocity_relative_rms=\"\"\n"
                "        if [[ -f \"$velocity_convergence_state\" ]]; then\n"
                "            echo \"Ignoring incompatible, malformed, or future spatial velocity convergence state.\"\n"
                "        fi\n"
                "    fi\n"
                "    accepted_airflow_reference=\"$case_dir/.accepted_airflow_reference\"\n"
                "    accepted_airflow_relative_rms=\"\"\n"
                "    record_accepted_airflow_reference()\n"
                "    {\n"
                "        local latest rank source target\n"
                "        latest=$(\"$foam_launcher\" foamListTimes -case \"$case_dir\" "
                    "-processor -latestTime 2>/dev/null || true)\n"
                "        latest=\"${latest##*$'\\n'}\"\n"
                "        [[ -n \"$latest\" ]] || return 1\n"
                "        rm -rf -- \"$accepted_airflow_reference.tmp\"\n"
                "        mkdir -p \"$accepted_airflow_reference.tmp\"\n"
                "        for ((rank=0; rank<processes; ++rank)); do\n"
                "            source=\"$case_dir/processor${rank}/${latest}/fluid/U\"\n"
                "            target=\"$accepted_airflow_reference.tmp/processor${rank}\"\n"
                "            [[ -f \"$source\" ]] || return 1\n"
                "            mkdir -p \"$target\"\n"
                "            cp -p \"$source\" \"$target/U\"\n"
                "        done\n"
                "        printf '%s\\n' \"$latest\" > \"$accepted_airflow_reference.tmp/time\"\n"
                "        rm -rf -- \"$accepted_airflow_reference\"\n"
                "        mv \"$accepted_airflow_reference.tmp\" \"$accepted_airflow_reference\"\n"
                "    }\n"
                "    validate_accepted_airflow_drift()\n"
                "    {\n"
                "        local latest reference_time rank source target spatial_output "
                    "spatial_status=0 velocity_rms_delta velocity_rms_reference\n"
                "        accepted_airflow_relative_rms=\"\"\n"
                "        if [[ ! -f \"$accepted_airflow_reference/time\" ]]; then\n"
                "            record_accepted_airflow_reference || return 1\n"
                "            echo \"Accepted airflow baseline recorded; one more refresh is required for coupled convergence.\"\n"
                "            return 1\n"
                "        fi\n"
                "        reference_time=$(<\"$accepted_airflow_reference/time\")\n"
                "        latest=$(\"$foam_launcher\" foamListTimes -case \"$case_dir\" "
                    "-processor -latestTime 2>/dev/null || true)\n"
                "        latest=\"${latest##*$'\\n'}\"\n"
                "        [[ -n \"$latest\" ]] || return 1\n"
                "        for ((rank=0; rank<processes; ++rank)); do\n"
                "            source=\"$accepted_airflow_reference/processor${rank}/U\"\n"
                "            target=\"$case_dir/processor${rank}/${latest}/fluid/UPrevious\"\n"
                "            if [[ ! -f \"$source\" ]]; then\n"
                "                echo \"Accepted airflow reference is incomplete; rebuilding it.\" >&2\n"
                "                record_accepted_airflow_reference || return 1\n"
                "                return 1\n"
                "            fi\n"
                "            cp -p \"$source\" \"$target\"\n"
                "            \"$foam_launcher\" foamDictionary -precision 17 \"$target\" "
                    "-entry FoamFile/object -set UPrevious >/dev/null 2>&1\n"
                "        done\n"
                "        if ! spatial_output=$(\"$foam_launcher\" mpirun -np \"$processes\" "
                    "semiFrozenChtMultiRegionFoam -case \"$case_dir\" -parallel "
                    "-postProcess -latestTime -dict system/spatialConvergenceDict 2>&1); then\n"
                "            spatial_status=1\n"
                "        fi\n"
                "        velocity_rms_delta=$(printf '%s\\n' \"$spatial_output\" | "
                    "awk '/velocityDeltaSquared =/{value=$NF} END{print value}')\n"
                "        velocity_rms_reference=$(printf '%s\\n' \"$spatial_output\" | "
                    "awk '/velocitySquared =/{value=$NF} END{print value}')\n"
                "        for ((rank=0; rank<processes; ++rank)); do\n"
                "            for field in UPrevious velocityDelta velocityDeltaSquared velocitySquared; do\n"
                "                rm -f -- \"$case_dir/processor${rank}/${latest}/fluid/${field}\"\n"
                "            done\n"
                "        done\n"
                "        if [[ \"$spatial_status\" != 0 || -z \"$velocity_rms_delta\" || "
                    "-z \"$velocity_rms_reference\" ]]; then\n"
                "            printf '%s\\n' \"$spatial_output\" >&2\n"
                "            echo \"Accepted airflow drift calculation failed at t=$latest.\" >&2\n"
                "            return 1\n"
                "        fi\n"
                "        accepted_airflow_relative_rms=$(awk -v delta=\"$velocity_rms_delta\" "
                    "-v reference=\"$velocity_rms_reference\" 'BEGIN { "
                    "print (reference>1e-12?delta/reference:(delta<=1e-12?0:1e30)) }')\n"
                "        echo \"Accepted airflow drift: referenceTime=$reference_time, "
                    "currentTime=$latest, rmsDelta=$velocity_rms_delta m/s, "
                    "rmsVelocity=$velocity_rms_reference m/s, "
                    "relativeRms=$accepted_airflow_relative_rms\"\n"
                "        if ! awk -v value=\"$accepted_airflow_relative_rms\" -v limit=\""
                << options.maximum_accepted_velocity_rms_change_fraction
                << "\" 'BEGIN { exit !(value<=limit) }'; then\n"
                "            record_accepted_airflow_reference || return 1\n"
                "            return 1\n"
                "        fi\n"
                "        return 0\n"
                "    }\n"
                "    airflow_metrics_converged()\n"
                "    {\n"
                "        local report name value rule expected net=0 "
                    "sum_abs=0 flow_time properties air_exchange_time\n"
                "        local imbalance stable=1 directions_ok=1 "
                    "maximum_change=0 maximum_change_name=none change "
                    "boundary_flow_floor=0 flow_floor=0 comparison_value "
                    "comparison_reference airflow_state_tmp\n"
                "        if ! report=$(\"$foam_launcher\" mpirun -np "
                    "\"$processes\" postProcess -case \"$case_dir\" "
                    "-parallel -region fluid -latestTime -field phi 2>&1); "
                    "then\n"
                "            echo \"$report\" >&2\n"
                "            echo \"Unable to evaluate airflow refresh "
                    "convergence.\" >&2\n"
                "            return 1\n"
                "        fi\n"
                "        declare -A flows=()\n"
                "        declare -A current_smoothed_internal_flows=()\n"
                "        for name in \"${tracked_flow_names[@]}\"; do\n"
                "            value=$(awk -v pattern=\"sum(${name}) of phi =\" "
                    "'index($0,pattern) { value=$NF; print value; exit }' "
                    "<<<\"$report\")\n"
                "            if [[ -z \"$value\" ]]; then\n"
                "                echo \"Missing mass-flow result for $name.\" "
                    ">&2\n"
                "                return 1\n"
                "            fi\n"
                "            flows[\"$name\"]=\"$value\"\n"
                "        done\n"
                "        flow_time=$(\"$foam_launcher\" foamListTimes "
                    "-case \"$case_dir\" -processor -latestTime "
                    "2>/dev/null || echo 0)\n"
                "        flow_time=\"${flow_time##*$'\\n'}\"\n"
                "        for name in \"${internal_fan_names[@]}\"; do\n"
                "            properties=\"$case_dir/processor0/$flow_time/"
                    "fluid/uniform/${name}Properties\"\n"
                "            value=$(awk '$1==\"flow_rate\" "
                    "{ gsub(/;/,\"\",$2); print $2; exit }' "
                    "\"$properties\" 2>/dev/null || true)\n"
                "            if [[ -z \"$value\" ]]; then\n"
                "                echo \"Missing fan operating-point output "
                    "for $name at t=$flow_time.\" >&2\n"
                "                return 1\n"
                "            fi\n"
                "            flows[\"$name\"]=\"$value\"\n"
                "            if ! awk -v v=\"$value\" "
                    "'BEGIN { exit !(v>0) }'; then\n"
                "                directions_ok=0\n"
                "                echo \"Internal fan not producing positive "
                    "through-flow: $name flow_rate=$value m3/s\" >&2\n"
                "            fi\n"
                "        done\n"
                "        for name in \"${boundary_flow_names[@]}\"; do\n"
                "            value=\"${flows[$name]}\"\n"
                "            net=$(awk -v a=\"$net\" -v b=\"$value\" "
                    "'BEGIN { print a+b }')\n"
                "            sum_abs=$(awk -v a=\"$sum_abs\" -v b=\"$value\" "
                    "'BEGIN { if(b<0)b=-b; print a+b }')\n"
                "        done\n"
                "        boundary_flow_floor=$(awk -v s=\"$sum_abs\" "
                    "-v f=\""
                << options.minimum_tracked_boundary_flow_fraction
                << "\" 'BEGIN { print 0.5*s*f }')\n"
                "        for name in \"${stability_flow_names[@]}\"; do\n"
                "            value=\"${flows[$name]}\"\n"
                "            comparison_value=\"$value\"\n"
                "            comparison_reference=\"\"\n"
                "            flow_floor=0\n"
                "            if [[ -n "
                    "\"${boundary_flow_lookup[$name]+set}\" ]]; then\n"
                "                flow_floor=\"$boundary_flow_floor\"\n"
                "            fi\n"
                "            if [[ -n \"${internal_fan_lookup[$name]+set}\" ]]; then\n"
                "                if [[ -n \"${previous_flows[$name]+set}\" ]]; then\n"
                "                    comparison_value=$(awk -v a=\"$value\" -v b=\"${previous_flows[$name]}\" 'BEGIN { print 0.5*(a+b) }')\n"
                "                    current_smoothed_internal_flows[\"$name\"]=\"$comparison_value\"\n"
                "                    comparison_reference=\"${previous_smoothed_internal_flows[$name]-}\"\n"
                "                fi\n"
                "            elif [[ -n \"${previous_flows[$name]+set}\" ]]; then\n"
                "                comparison_reference=\"${previous_flows[$name]}\"\n"
                "            fi\n"
                "            if [[ -n \"$comparison_reference\" ]]; then\n"
                "                change=$(awk -v a=\"$comparison_value\" "
                    "-v b=\"$comparison_reference\" "
                    "-v floor=\"$flow_floor\" "
                    "'BEGIN { d=a-b; if(d<0)d=-d; aa=a; if(aa<0)aa=-aa; "
                    "bb=b; if(bb<0)bb=-bb; "
                    "if(floor>0 && aa<floor && bb<floor) { print 0; exit } "
                    "s=bb; if(s<floor)s=floor; if(s<1e-12)s=1e-12; "
                    "print d/s }')\n"
                "                if awk -v a=\"$change\" -v b=\"$maximum_change\" 'BEGIN { exit !(a>b) }'; then\n"
                "                    maximum_change=\"$change\"\n"
                "                    maximum_change_name=\"$name\"\n"
                "                fi\n"
                "            else\n"
                "                stable=0\n"
                "            fi\n"
                "        done\n"
                "        if [[ ${#boundary_flow_names[@]} -eq 0 ]]; then\n"
                "            # A sealed domain has no ambient mass-flow balance to "
                    "evaluate.\n"
                "            imbalance=0\n"
                "        else\n"
                "            imbalance=$(awk -v n=\"$net\" -v s=\"$sum_abs\" "
                    "'BEGIN { if(n<0)n=-n; d=0.5*s; "
                    "print (d>1e-12?n/d:1e30) }')\n"
                "        fi\n"
                "        air_exchange_time=$(awk -v volume=\""
                << fluid_volume_m3 << "\" -v rho=\""
                << mesh.get_env().get_rho()
                << "\" -v s=\"$sum_abs\" 'BEGIN { one_way=0.5*s; "
                    "print (one_way>1e-12?volume*rho/one_way:1e30) }')\n"
                "        latest_air_exchange_time=\"$air_exchange_time\"\n"
                "        for rule in \"${fan_direction_rules[@]}\"; do\n"
                "            name=\"${rule%%:*}\"\n"
                "            expected=\"${rule##*:}\"\n"
                "            value=\"${flows[$name]}\"\n"
                "            if ! awk -v v=\"$value\" -v e=\"$expected\" "
                    "'BEGIN { exit !((e<0 && v<0)||(e>0 && v>0)) }'; "
                    "then\n"
                "                directions_ok=0\n"
                "                echo \"Fan direction not settled: $name "
                    "phi=$value\" >&2\n"
                "            fi\n"
                "        done\n"
                "        for name in \"${stability_flow_names[@]}\"; do\n"
                "            previous_flows[\"$name\"]=\"${flows[$name]}\"\n"
                "            if [[ -n \"${current_smoothed_internal_flows[$name]+set}\" ]]; then\n"
                "                previous_smoothed_internal_flows[\"$name\"]=\"${current_smoothed_internal_flows[$name]}\"\n"
                "            fi\n"
                "        done\n"
                "        airflow_state_tmp=\"${airflow_convergence_state}.tmp.$$\"\n"
                "        {\n"
                "            printf '%s %s\\n' \"$flow_time\" \"${#stability_flow_names[@]}\"\n"
                "            for name in \"${stability_flow_names[@]}\"; do\n"
                "                printf '%s %s %s\\n' \"$name\" "
                    "\"${previous_flows[$name]}\" "
                    "\"${previous_smoothed_internal_flows[$name]:--}\"\n"
                "            done\n"
                "        } > \"$airflow_state_tmp\"\n"
                "        mv -f \"$airflow_state_tmp\" \"$airflow_convergence_state\"\n"
                "        if ! awk -v v=\"$imbalance\" -v limit=\""
                << options.maximum_mass_imbalance_fraction
                << "\" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi\n"
                "        if ! awk -v v=\"$maximum_change\" -v limit=\""
                << options.maximum_device_flow_change_fraction
                << "\" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi\n"
                "        if [[ -z \"$latest_velocity_relative_rms\" || "
                    "-z \"$previous_velocity_relative_rms\" ]] || "
                    "! awk -v v=\"$latest_velocity_relative_rms\" -v limit=\""
                << options.maximum_velocity_rms_change_fraction
                << "\" 'BEGIN { exit !(v<=limit) }' || "
                    "! awk -v v=\"$previous_velocity_relative_rms\" -v limit=\""
                << options.maximum_velocity_rms_change_fraction
                << "\" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi\n"
                "        echo \"Airflow refresh metrics: imbalance=$imbalance, "
                    "maxFlowChange=$maximum_change, maxFlowDevice="
                    "$maximum_change_name, boundaryFlowFloor="
                    "$boundary_flow_floor, directionsOK="
                    "$directions_ok, velocityRelativeRms="
                    "${latest_velocity_relative_rms:-unavailable}, "
                    "previousVelocityRelativeRms="
                    "${previous_velocity_relative_rms:-unavailable}, estimatedAirExchangeTime="
                    "$air_exchange_time s\"\n"
                "        summary \"airflow time=$current imbalance=$imbalance maxFlowChange=$maximum_change maxFlowDevice=$maximum_change_name directionsOK=$directions_ok velocityRelativeRms=${latest_velocity_relative_rms:-unavailable} previousVelocityRelativeRms=${previous_velocity_relative_rms:-unavailable} estimatedAirExchangeTime=$air_exchange_time\"\n"
                "        [[ \"$stable\" == 1 && \"$directions_ok\" == 1 ]]\n"
                "    }\n";
            if(options.stop_when_thermally_converged) {
                output <<
                "    thermal_convergence_state="
                    "\"$case_dir/.thermal_convergence_state\"\n"
                "    thermal_convergence_streak="
                    "\"$case_dir/.thermal_convergence_streak\"\n"
                "    if [[ -f \"$thermal_convergence_state\" ]]; then\n"
                "        stored_checkpoint=$(awk 'NF { print $1; exit }' "
                    "\"$thermal_convergence_state\")\n"
                "        if [[ -n \"$stored_checkpoint\" ]] && "
                    "awk -v saved=\"$stored_checkpoint\" -v now=\"$current\" "
                    "'BEGIN { s=(now<0?-now:now); if(s<1)s=1; "
                    "exit !(saved>now+1e-9*s) }'; then\n"
                "            echo \"Discarding future thermal-convergence state "
                    "at t=$stored_checkpoint after restart from t=$current.\"\n"
                "            rm -f \"$thermal_convergence_state\" "
                    "\"$thermal_convergence_streak\"\n"
                "        fi\n"
                "    fi\n"
                "    thermal_metrics_converged()\n"
                "    {\n"
                "        local maximum_root average_root average_file "
                    "region line sample_time checkpoint_time peak previous_time "
                    "previous_peak elapsed delta scaled_delta value previous "
                    "index=0 maximum_peak_delta=0 maximum_average_delta=0 "
                    "scaled_average_delta expected_state_values "
                    "controlling_peak_region=fluid controlling_average_region=none\n"
                "        local -a maxima=() averages=() state_values=()\n"
                "        maximum_root=\"$case_dir/postProcessing/fluid/"
                    "fluid_temperature_internal_maximum\"\n"
                "        line=$(find \"$maximum_root\" -type f "
                    "-name 'volFieldValue*.dat' -exec awk "
                    "'!/^#/ && NF>=2 { print $1, $2 }' {} + "
                    "2>/dev/null | sort -g -k1,1 | tail -1)\n"
                "        checkpoint_time=$(awk '{print $1}' <<<\"$line\")\n"
                "        peak=$(awk '{print $2}' <<<\"$line\")\n"
                "        if [[ -z \"$checkpoint_time\" || -z \"$peak\" ]] || "
                    "! awk -v sample=\"$checkpoint_time\" "
                    "-v checkpoint=\"$current\" 'BEGIN { "
                    "scale=(checkpoint<0?-checkpoint:checkpoint); "
                    "if(scale<1)scale=1; delta=sample-checkpoint; "
                    "if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then\n"
                "            echo \"Refreshing thermal reports at solver "
                    "checkpoint t=$current.\"\n"
                "            if ! \"$foam_launcher\" mpirun -np \"$processes\" "
                    "semiFrozenChtMultiRegionFoam -case \"$case_dir\" "
                    "-parallel -postProcess -latestTime; then\n"
                "                echo \"Unable to refresh multi-region thermal "
                    "convergence reports.\" >&2\n"
                "                return 1\n"
                "            fi\n"
                "            line=$(find \"$maximum_root\" -type f "
                    "-name 'volFieldValue*.dat' -exec awk "
                    "'!/^#/ && NF>=2 { print $1, $2 }' {} + "
                    "2>/dev/null | sort -g -k1,1 | tail -1)\n"
                "            checkpoint_time=$(awk '{print $1}' <<<\"$line\")\n"
                "            peak=$(awk '{print $2}' <<<\"$line\")\n"
                "        fi\n"
                "        if [[ -z \"$checkpoint_time\" || -z \"$peak\" ]]; then\n"
                "            echo \"Thermal convergence data is missing or "
                    "contains no completed fluid internal-maximum sample.\" >&2\n"
                "            return 1\n"
                "        fi\n"
                "        if ! awk -v sample=\"$checkpoint_time\" "
                    "-v checkpoint=\"$current\" 'BEGIN { "
                    "scale=(checkpoint<0?-checkpoint:checkpoint); "
                    "if(scale<1)scale=1; delta=sample-checkpoint; "
                    "if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then\n"
                "            echo \"Thermal convergence fluid report does not "
                    "match the current solver checkpoint: sample=$checkpoint_time "
                    "checkpoint=$current.\" >&2\n"
                "            return 1\n"
                "        fi\n"
                "        for region in \"${component_region_names[@]}\"; do\n"
                "            maximum_root=\"$case_dir/postProcessing/$region/"
                    "${region}_temperature_internal_maximum\"\n"
                "            line=$(find \"$maximum_root\" -type f "
                    "-name 'volFieldValue*.dat' -exec awk "
                    "'!/^#/ && NF>=2 { print $1, $2 }' {} + "
                    "2>/dev/null | sort -g -k1,1 | tail -1)\n"
                "            sample_time=$(awk '{ print $1 }' <<<\"$line\")\n"
                "            value=$(awk '{ print $2 }' <<<\"$line\")\n"
                "            if [[ -z \"$value\" ]]; then\n"
                "                echo \"Thermal convergence maximum is missing "
                    "for component region $region.\" >&2\n"
                "                return 1\n"
                "            fi\n"
                "            if ! awk -v sample=\"$sample_time\" "
                    "-v checkpoint=\"$checkpoint_time\" 'BEGIN { "
                    "scale=(checkpoint<0?-checkpoint:checkpoint); "
                    "if(scale<1)scale=1; delta=sample-checkpoint; "
                    "if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then\n"
                "                echo \"Thermal convergence maximum for "
                    "component region $region is stale: sample=$sample_time "
                    "checkpoint=$checkpoint_time.\" >&2\n"
                "                return 1\n"
                "            fi\n"
                "            maxima+=(\"$value\")\n"
                "            average_root=\"$case_dir/postProcessing/$region/"
                    "${region}_temperature_average\"\n"
                "            line=$(find \"$average_root\" -type f "
                    "-name 'volFieldValue*.dat' -exec awk "
                    "'!/^#/ && NF>=2 { print $1, $2 }' {} + "
                    "2>/dev/null | sort -g -k1,1 | tail -1)\n"
                "            sample_time=$(awk '{ print $1 }' <<<\"$line\")\n"
                "            value=$(awk '{ print $2 }' <<<\"$line\")\n"
                "            if [[ -z \"$value\" ]]; then\n"
                "                echo \"Thermal convergence data is missing "
                    "for component region $region.\" >&2\n"
                "                return 1\n"
                "            fi\n"
                "            if ! awk -v sample=\"$sample_time\" "
                    "-v checkpoint=\"$checkpoint_time\" 'BEGIN { "
                    "scale=(checkpoint<0?-checkpoint:checkpoint); "
                    "if(scale<1)scale=1; delta=sample-checkpoint; "
                    "if(delta<0)delta=-delta; exit !(delta<=1e-9*scale) }'; then\n"
                "                echo \"Thermal convergence average for "
                    "component region $region is stale: sample=$sample_time "
                    "checkpoint=$checkpoint_time.\" >&2\n"
                "                return 1\n"
                "            fi\n"
                "            averages+=(\"$value\")\n"
                "        done\n"
                "        if [[ ! -f \"$thermal_convergence_state\" ]]; then\n"
                "            printf '%s %s' \"$checkpoint_time\" \"$peak\" > "
                    "\"$thermal_convergence_state\"\n"
                "            printf ' %s' \"${maxima[@]}\" >> "
                    "\"$thermal_convergence_state\"\n"
                "            printf ' %s' \"${averages[@]}\" >> "
                    "\"$thermal_convergence_state\"\n"
                "            printf '\\n' >> \"$thermal_convergence_state\"\n"
                "            echo \"Thermal convergence baseline recorded at "
                    "t=$checkpoint_time s.\"\n"
                "            return 1\n"
                "        fi\n"
                "        read -ra state_values < "
                    "\"$thermal_convergence_state\"\n"
                "        expected_state_values=$((2 + 2*${#component_region_names[@]}))\n"
                "        if (( ${#state_values[@]} != expected_state_values )); then\n"
                "            printf '%s %s' \"$checkpoint_time\" \"$peak\" > "
                    "\"$thermal_convergence_state\"\n"
                "            printf ' %s' \"${maxima[@]}\" >> "
                    "\"$thermal_convergence_state\"\n"
                "            printf ' %s' \"${averages[@]}\" >> "
                    "\"$thermal_convergence_state\"\n"
                "            printf '\\n' >> \"$thermal_convergence_state\"\n"
                "            echo \"Thermal convergence state format changed; "
                    "new per-component peak baseline recorded at "
                    "t=$checkpoint_time s.\"\n"
                "            return 1\n"
                "        fi\n"
                "        previous_time=\"${state_values[0]:-}\"\n"
                "        previous_peak=\"${state_values[1]:-}\"\n"
                "        printf '%s %s' \"$checkpoint_time\" \"$peak\" > "
                    "\"$thermal_convergence_state\"\n"
                "        printf ' %s' \"${maxima[@]}\" >> "
                    "\"$thermal_convergence_state\"\n"
                "        printf ' %s' \"${averages[@]}\" >> "
                    "\"$thermal_convergence_state\"\n"
                "        printf '\\n' >> \"$thermal_convergence_state\"\n"
                "        elapsed=$(awk -v a=\"$checkpoint_time\" "
                    "-v b=\"$previous_time\" 'BEGIN { print a-b }')\n"
                "        if ! awk -v v=\"$elapsed\" "
                    "'BEGIN { exit !(v>0) }'; then\n"
                "            echo \"Thermal convergence checkpoint did not "
                    "advance: previous=$previous_time "
                    "current=$checkpoint_time.\" >&2\n"
                "            return 1\n"
                "        fi\n"
                "        delta=$(awk -v a=\"$peak\" -v b=\"$previous_peak\" "
                    "'BEGIN { d=a-b; if(d<0)d=-d; print d }')\n"
                "        scaled_delta=$(awk -v d=\"$delta\" -v reference=\""
                    << options.thermal_convergence_reference_interval
                    << "\" -v elapsed=\"$elapsed\" "
                    "'BEGIN { print d*reference/elapsed }')\n"
                "        maximum_peak_delta=\"$delta\"\n"
                "        for value in \"${maxima[@]}\"; do\n"
                "            previous=\"${state_values[$((index+2))]:-}\"\n"
                "            if [[ -z \"$previous\" ]]; then return 1; fi\n"
                "            delta=$(awk -v a=\"$value\" -v b=\"$previous\" "
                    "'BEGIN { d=a-b; if(d<0)d=-d; print d }')\n"
                "            if awk -v a=\"$delta\" -v b=\"$maximum_peak_delta\" "
                    "'BEGIN { exit !(a>b) }'; then\n"
                "                maximum_peak_delta=\"$delta\"\n"
                "                controlling_peak_region=\"${component_region_names[$index]}\"\n"
                "            fi\n"
                "            index=$((index+1))\n"
                "        done\n"
                "        scaled_delta=$(awk -v d=\"$maximum_peak_delta\" -v reference=\""
                    << options.thermal_convergence_reference_interval
                    << "\" -v elapsed=\"$elapsed\" "
                    "'BEGIN { print d*reference/elapsed }')\n"
                "        for value in \"${averages[@]}\"; do\n"
                "            previous=\"${state_values[$((index+2))]:-}\"\n"
                "            if [[ -z \"$previous\" ]]; then return 1; fi\n"
                "            delta=$(awk -v a=\"$value\" -v b=\"$previous\" "
                    "'BEGIN { d=a-b; if(d<0)d=-d; print d }')\n"
                "            if awk -v a=\"$delta\" -v b=\"$maximum_average_delta\" "
                    "'BEGIN { exit !(a>b) }'; then\n"
                "                maximum_average_delta=\"$delta\"\n"
                "                controlling_average_region=\"${component_region_names[$((index-${#component_region_names[@]}))]}\"\n"
                "            fi\n"
                "            index=$((index+1))\n"
                "        done\n"
                "        scaled_average_delta=$(awk "
                    "-v d=\"$maximum_average_delta\" -v reference=\""
                    << options.thermal_convergence_reference_interval
                    << "\" -v elapsed=\"$elapsed\" "
                    "'BEGIN { print d*reference/elapsed }')\n"
                "        echo \"Thermal convergence metrics: "
                    "maxInternalCellChange=$scaled_delta K/"
                    << options.thermal_convergence_reference_interval
                    << "s, maxComponentAverageChange=$scaled_average_delta K/"
                    << options.thermal_convergence_reference_interval
                    << "s, controllingPeakRegion=$controlling_peak_region, "
                    "controllingAverageRegion=$controlling_average_region, elapsed=$elapsed s\"\n"
                "        summary \"thermal time=$checkpoint_time maxInternalCellChange=$scaled_delta maxComponentAverageChange=$scaled_average_delta controllingPeakRegion=$controlling_peak_region controllingAverageRegion=$controlling_average_region elapsed=$elapsed\"\n"
                "        if ! awk -v t=\"$checkpoint_time\" -v minimum=\""
                    << options.minimum_thermal_convergence_time
                    << "\" 'BEGIN { scale=(minimum<0?-minimum:minimum); if(scale<1)scale=1; tolerance=1e-9*scale; exit !(t>=minimum-tolerance) }'; then return 1; fi\n"
                "        if ! awk -v v=\"$scaled_delta\" -v limit=\""
                    << options.maximum_temperature_change
                    << "\" 'BEGIN { exit !(v<=limit) }'; then return 1; fi\n"
                "        if ! awk -v v=\"$scaled_average_delta\" -v limit=\""
                    << options.maximum_component_average_temperature_change
                    << "\" 'BEGIN { exit !(v<=limit) }'; then return 1; fi\n"
                "        return 0\n"
                "    }\n";
            }
            output <<
                "    stage()\n"
                "    {\n"
                "        local thermal_only=\"$1\" target=\"$2\" max_co=\"$3\" "
                    "max_dt=\"$4\" label=\"$5\"\n"
                "        local interval actual_time saved_time canonical_time restart_dt "
                    "saved_time_file rank stage_steps stage_dt stage_max_dt "
                    "stage_write_control stage_write_interval field "
                    "source_field target_field courant_output observed_co "
                    "courant_safe_dt airflow_hard_cap postflight_output "
                    "postflight_co spatial_output spatial_status velocity_rms_delta "
                    "velocity_rms_reference stage_wall_start stage_wall_end "
                    "stage_wall_seconds\n"
                "        interval=$(awk -v end=\"$target\" -v start=\"$current\" "
                    "'BEGIN { printf \"%.17g\", end-start }')\n"
                "        if awk -v d=\"$interval\" -v target=\"$target\" "
                    "'BEGIN { s=(target<0?-target:target); if(s<1)s=1; "
                    "exit !(d<=1e-9*s) }'; then\n"
                "            current=\"$target\"\n"
                "            return 0\n"
                "        fi\n"
                "        stage_wall_start=$(date +%s%N)\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/fluid/fvSolution\" "
                    "-entry PIMPLE/frozenFlow -set false\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/fluid/fvSolution\" "
                    "-entry PIMPLE/semiFrozenFlow -set false\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/fluid/fvSolution\" "
                    "-entry PIMPLE/thermalOnlyFlow -set \"$thermal_only\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/fluid/fvSolution\" "
                    "-entry PIMPLE/momentumPredictor -set true\n"
                "        if [[ \"$thermal_only\" == \"true\" ]]; then\n"
                "            adjust_time_step=false\n"
                "            echo \"Thermal-only maxCo=$max_co is diagnostic; "
                    "the fully implicit frozen-flow energy timestep is "
                    "limited by maxDeltaT=$max_dt s.\"\n"
                "            stage_max_dt=\"$max_dt\"\n"
                "            stage_write_control=timeStep\n"
                "            stage_plan=$(awk -v maximum=\"$max_dt\" "
                    "-v remaining=\"$interval\" "
                    "'BEGIN { n=int(remaining/maximum); "
                    "if(n*maximum<remaining-1e-12)n++; if(n<1)n=1; "
                    "printf \"%.17g %d\", remaining/n,n }')\n"
                "            read -r stage_dt stage_steps <<<\"$stage_plan\"\n"
                "            stage_write_interval=\"$stage_steps\"\n"
                "        else\n"
                "            # Keep exact, fixed, divisible live-flow steps.\n"
                "            # adjustableRunTime can enlarge a step to align an\n"
                "            # adjustable write, bypassing maxDeltaT. Begin with\n"
                "            # a conservative maxCo-scaled fallback; the saved\n"
                "            # flow field below then supplies a tighter limit.\n"
                "            adjust_time_step=false\n"
                "            airflow_hard_cap=$(awk -v maximum=\"$max_dt\" "
                    "-v flow_max=\""
                << options.airflow_maximum_time_step
                    << "\" 'BEGIN { print (flow_max<maximum?flow_max:maximum) }')\n"
                "            stage_max_dt=$(awk -v hard=\"$airflow_hard_cap\" "
                    "-v co=\"$max_co\" 'BEGIN { "
                    "scale=(co<10?co/10:1); "
                    "print hard*scale }')\n"
                "            stage_plan=$(awk -v maximum=\"$stage_max_dt\" "
                    "-v remaining=\"$interval\" 'BEGIN { "
                    "n=int(remaining/maximum); if(n*maximum<remaining-1e-12)n++; "
                    "if(n<1)n=1; printf \"%.17g %d\", remaining/n,n }')\n"
                "            read -r stage_dt stage_steps <<<\"$stage_plan\"\n"
                "            stage_max_dt=\"$stage_dt\"\n"
                "            stage_write_control=timeStep\n"
                "            stage_write_interval=\"$stage_steps\"\n"
                "        fi\n"
                "        restart_dt=\"$stage_dt\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry adjustTimeStep -set \"$adjust_time_step\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry deltaT -set \"$restart_dt\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry startFrom -set latestTime\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry startTime -set \"$current\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry endTime -set \"$target\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry writeControl -set \"$stage_write_control\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry writeInterval -set \"$stage_write_interval\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry maxCo -set \"$max_co\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry maxDeltaT -set \"$stage_max_dt\"\n"
                "        saved_time=$(\"$foam_launcher\" foamListTimes "
                    "-case \"$case_dir\" -processor -latestTime "
                    "2>/dev/null || true)\n"
                "        saved_time=\"${saved_time##*$'\\n'}\"\n"
                "        if [[ -n \"$saved_time\" ]]; then\n"
                "            # Older cases may have directory names written at lower\n"
                "            # precision. OpenFOAM reconstructs the name at the current\n"
                "            # timePrecision, so normalize it before attempting restart.\n"
                "            canonical_time=$(awk -v t=\"$saved_time\" "
                    "'BEGIN { printf \"%.17g\", t }')\n"
                "            if [[ \"$canonical_time\" != \"$saved_time\" ]]; then\n"
                "                for ((rank=0; rank<processes; ++rank)); do\n"
                "                    if [[ -e \"$case_dir/processor${rank}/$canonical_time\" ]]; then\n"
                "                        echo \"Cannot normalize checkpoint: target exists: "
                    "$case_dir/processor${rank}/$canonical_time\" >&2\n"
                "                        return 1\n"
                "                    fi\n"
                "                done\n"
                "                for ((rank=0; rank<processes; ++rank)); do\n"
                "                    mv -- \"$case_dir/processor${rank}/$saved_time\" "
                    "\"$case_dir/processor${rank}/$canonical_time\"\n"
                "                done\n"
                "                echo \"Normalized legacy checkpoint directory: "
                    "$saved_time -> $canonical_time\"\n"
                "                saved_time=\"$canonical_time\"\n"
                "            fi\n"
                "            for ((rank=0; rank<processes; ++rank)); do\n"
                "                saved_time_file=\"$case_dir/processor"
                    "${rank}/${saved_time}/uniform/time\"\n"
                "                if [[ -f \"$saved_time_file\" ]]; then\n"
                "                    # Align timeStep writes with this stage's final step.\n"
                "                    \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$saved_time_file\" "
                    "-entry index -set 0\n"
                "                    \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$saved_time_file\" -entry deltaT "
                    "-set \"$restart_dt\"\n"
                "                    \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$saved_time_file\" -entry deltaT0 "
                    "-set \"$restart_dt\"\n"
                "                fi\n"
                "            done\n"
                "        fi\n"
                "        if [[ \"$thermal_only\" == \"false\" && "
                    "-n \"$saved_time\" ]]; then\n"
                "            # CourantNo uses the checkpoint's stored deltaT.\n"
                "            # The restart metadata now contains stage_dt, so the\n"
                "            # reported Co predicts the proposed first live step.\n"
                "            if ! courant_output=$(\"$foam_launcher\" mpirun "
                    "-np \"$processes\" postProcess -case \"$case_dir\" "
                    "-parallel -region fluid -latestTime -fields '(phi rho)' "
                    "-funcs '(CourantNo fieldMinMax(Co))' 2>&1); then\n"
                "                printf '%s\\n' \"$courant_output\" >&2\n"
                "                echo \"Courant preflight failed at t=$saved_time.\" >&2\n"
                "                return 6\n"
                "            fi\n"
                "            observed_co=$(printf '%s\\n' \"$courant_output\" | "
                    "awk '/max\\(Co\\) =/{value=$3} END{print value}')\n"
                "            if [[ -z \"$observed_co\" ]]; then\n"
                "                printf '%s\\n' \"$courant_output\" >&2\n"
                "                echo \"Courant preflight did not report max(Co).\" >&2\n"
                "                return 6\n"
                "            fi\n"
                "            courant_safe_dt=$(awk -v dt=\"$stage_dt\" "
                    "-v observed=\"$observed_co\" -v limit=\"$max_co\" "
                    "-v hard=\"$airflow_hard_cap\" 'BEGIN { "
                    "safe=(observed>0?dt*0.8*limit/observed:hard); "
                    "print (safe<hard?safe:hard) }')\n"
                "            if awk -v safe=\"$courant_safe_dt\" "
                    "'BEGIN { exit !(safe>0) }'; then\n"
                "                stage_plan=$(awk -v maximum=\"$courant_safe_dt\" "
                    "-v remaining=\"$interval\" 'BEGIN { "
                    "n=int(remaining/maximum); if(n*maximum<remaining-1e-12)n++; "
                    "if(n<1)n=1; printf \"%.17g %d\", remaining/n,n }')\n"
                "                read -r stage_dt stage_steps <<<\"$stage_plan\"\n"
                "                stage_max_dt=\"$stage_dt\"\n"
                "                stage_write_interval=\"$stage_steps\"\n"
                "                restart_dt=\"$stage_dt\"\n"
                "                \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" -entry deltaT "
                    "-set \"$restart_dt\"\n"
                "                \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" -entry maxDeltaT "
                    "-set \"$stage_max_dt\"\n"
                "                \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" -entry writeInterval "
                    "-set \"$stage_write_interval\"\n"
                "                for ((rank=0; rank<processes; ++rank)); do\n"
                "                    saved_time_file=\"$case_dir/processor"
                    "${rank}/${saved_time}/uniform/time\"\n"
                "                    [[ -f \"$saved_time_file\" ]] || continue\n"
                "                    \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$saved_time_file\" -entry deltaT -set \"$restart_dt\"\n"
                "                    \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$saved_time_file\" -entry deltaT0 -set \"$restart_dt\"\n"
                "                done\n"
                "            fi\n"
                "            echo \"Courant preflight: predictedMaxCo=$observed_co, "
                    "maxCo=$max_co, deltaT=$stage_dt, steps=$stage_steps\"\n"
                "        fi\n"
                "        echo \"$label: t=$current -> $target\"\n"
                "        \"$foam_launcher\" mpirun -np \"$processes\" "
                    "semiFrozenChtMultiRegionFoam "
                    "-case \"$case_dir\" -parallel\n"
                "        if [[ \"$thermal_only\" == \"false\" ]]; then\n"
                "            if ! postflight_output=$(\"$foam_launcher\" mpirun "
                    "-np \"$processes\" postProcess -case \"$case_dir\" "
                    "-parallel -region fluid -latestTime -fields '(phi rho)' "
                    "-funcs '(CourantNo fieldMinMax(Co))' 2>&1); then\n"
                "                printf '%s\\n' \"$postflight_output\" >&2\n"
                "                echo \"Courant postflight failed at target=$target.\" >&2\n"
                "                return 7\n"
                "            fi\n"
                "            postflight_co=$(printf '%s\\n' \"$postflight_output\" | "
                    "awk '/max\\(Co\\) =/{value=$3} END{print value}')\n"
                "            if [[ -z \"$postflight_co\" ]]; then\n"
                "                printf '%s\\n' \"$postflight_output\" >&2\n"
                "                echo \"Courant postflight did not report max(Co).\" >&2\n"
                "                return 7\n"
                "            fi\n"
                "            echo \"Courant postflight: actualMaxCo=$postflight_co, "
                    "maxCo=$max_co, deltaT=$stage_dt\"\n"
                "            if ! awk -v actual=\"$postflight_co\" "
                    "-v limit=\"$max_co\" 'BEGIN { exit !(actual<=limit*1.001) }'; then\n"
                "                echo \"Live-flow Courant limit exceeded: "
                    "actualMaxCo=$postflight_co maxCo=$max_co.\" >&2\n"
                "                return 7\n"
                "            fi\n"
                "        fi\n"
                "        actual_time=$(\"$foam_launcher\" foamListTimes "
                    "-case \"$case_dir\" -processor -latestTime "
                    "2>/dev/null || echo \"$current\")\n"
                "        actual_time=\"${actual_time##*$'\\n'}\"\n"
                "        if ! awk -v actual=\"$actual_time\" -v target=\"$target\" "
                    "'BEGIN { scale=(target<0?-target:target); "
                    "if(scale<1)scale=1; tolerance=1e-6*scale; "
                    "exit !(actual>=target-tolerance) }'; then\n"
                "            echo \"Solver stage failed to reach target time: "
                    "target=$target actual=$actual_time.\" >&2\n"
                "            return 5\n"
                "        fi\n"
                "        if [[ \"$thermal_only\" == \"false\" && "
                    "-n \"$saved_time\" && \"$saved_time\" != \"$actual_time\" ]]; then\n"
                "            for ((rank=0; rank<processes; ++rank)); do\n"
                "                source_field=\"$case_dir/processor${rank}/"
                    "${saved_time}/fluid/U\"\n"
                "                target_field=\"$case_dir/processor${rank}/"
                    "${actual_time}/fluid/UPrevious\"\n"
                "                if [[ ! -f \"$source_field\" ]]; then\n"
                "                    echo \"Previous velocity field is missing: $source_field\" >&2\n"
                "                    return 8\n"
                "                fi\n"
                "                cp -p \"$source_field\" \"$target_field\"\n"
                "                \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$target_field\" -entry FoamFile/object -set UPrevious >/dev/null 2>&1\n"
                "            done\n"
                "            spatial_status=0\n"
                "            if ! spatial_output=$(\"$foam_launcher\" mpirun -np \"$processes\" "
                    "semiFrozenChtMultiRegionFoam -case \"$case_dir\" -parallel "
                    "-postProcess -latestTime -dict system/spatialConvergenceDict 2>&1); then\n"
                "                spatial_status=1\n"
                "            fi\n"
                "            velocity_rms_delta=$(printf '%s\\n' \"$spatial_output\" | "
                    "awk '/velocityDeltaSquared =/{value=$NF} END{print value}')\n"
                "            velocity_rms_reference=$(printf '%s\\n' \"$spatial_output\" | "
                    "awk '/velocitySquared =/{value=$NF} END{print value}')\n"
                "            for ((rank=0; rank<processes; ++rank)); do\n"
                "                for field in UPrevious velocityDelta velocityDeltaSquared velocitySquared; do\n"
                "                    rm -f -- \"$case_dir/processor${rank}/${actual_time}/fluid/${field}\"\n"
                "                done\n"
                "            done\n"
                "            if [[ \"$spatial_status\" != 0 || -z \"$velocity_rms_delta\" || "
                    "-z \"$velocity_rms_reference\" ]]; then\n"
                "                printf '%s\\n' \"$spatial_output\" >&2\n"
                "                echo \"Spatial velocity convergence calculation failed at t=$actual_time.\" >&2\n"
                "                return 8\n"
                "            fi\n"
                "            previous_velocity_relative_rms=\"$latest_velocity_relative_rms\"\n"
                "            latest_velocity_relative_rms=$(awk -v delta=\"$velocity_rms_delta\" "
                    "-v reference=\"$velocity_rms_reference\" 'BEGIN { "
                    "print (reference>1e-12?delta/reference:(delta<=1e-12?0:1e30)) }')\n"
                "            echo \"Spatial velocity change: rmsDelta=$velocity_rms_delta m/s, "
                    "rmsVelocity=$velocity_rms_reference m/s, "
                    "relativeRms=$latest_velocity_relative_rms\"\n"
                "            printf '%s %s %s\\n' \"$actual_time\" "
                    "\"$latest_velocity_relative_rms\" "
                    "\"${previous_velocity_relative_rms:--}\" > "
                    "\"$velocity_convergence_state.tmp.$$\"\n"
                "            mv -f \"$velocity_convergence_state.tmp.$$\" "
                    "\"$velocity_convergence_state\"\n"
                "        fi\n"
                "        if [[ \"$thermal_only\" == \"true\" && "
                    "-n \"$saved_time\" && \"$saved_time\" != \"$actual_time\" ]]; then\n"
                "            for ((rank=0; rank<processes; ++rank)); do\n"
                "                for field in U p p_rgh phi rho k omega nut alphat; do\n"
                "                    source_field=\"$case_dir/processor${rank}/"
                    "${saved_time}/fluid/${field}\"\n"
                "                    target_field=\"$case_dir/processor${rank}/"
                    "${actual_time}/fluid/${field}\"\n"
                "                    if [[ -f \"$source_field\" && ! -f \"$target_field\" ]]; then\n"
                "                        cp -p \"$source_field\" \"$target_field\"\n"
                "                    fi\n"
                "                done\n"
                "            done\n"
                "        fi\n"
                "        prune_processor_times\n"
                "        stage_wall_end=$(date +%s%N)\n"
                "        stage_wall_seconds=$(awk -v start=\"$stage_wall_start\" "
                    "-v end=\"$stage_wall_end\" 'BEGIN { "
                    "printf \"%.3f\", (end-start)/1e9 }')\n"
                "        echo \"Stage wall time: label=$label, thermalOnly=$thermal_only, "
                    "start=$current, target=$actual_time, seconds=$stage_wall_seconds\"\n"
                "        summary \"stage label=$label thermalOnly=$thermal_only start=$current target=$actual_time seconds=$stage_wall_seconds\"\n"
                "        current=\"$actual_time\"\n"
                "    }\n\n";
            if(options.use_adaptive_airflow_refresh) {
                output <<
                    "    adaptive_airflow_refresh()\n"
                    "    {\n"
                    "        local refresh_start=\"$current\" "
                        "refresh_elapsed=0 refresh_target "
                        "pending_refresh_start long_lag_failed=0\n"
                    "        airflow_refresh_validated=0\n"
                    "        airflow_refresh_long_lag_validated=0\n"
                    "        if [[ -f \"$refresh_pending_marker\" ]]; then\n"
                    "            pending_refresh_start=$(awk 'NF { print $1; exit }' "
                        "\"$refresh_pending_marker\")\n"
                    "            if [[ \"$pending_refresh_start\" =~ "
                        "^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] && "
                        "awk -v start=\"$pending_refresh_start\" "
                        "-v now=\"$current\" 'BEGIN { "
                        "scale=(now<0?-now:now); if(scale<1)scale=1; "
                        "tolerance=1e-9*scale; exit !(start<=now+tolerance) }'; then\n"
                    "                if ! awk -v start=\"$pending_refresh_start\" "
                        "-v now=\"$current\" -v maximum=\""
                    << options.maximum_airflow_refresh_duration
                    << "\" 'BEGIN { scale=(now<0?-now:now); "
                        "if(scale<1)scale=1; tolerance=1e-9*scale; "
                        "exit !(now<=start+maximum+tolerance) }'; then\n"
                    "                    echo \"Pending airflow refresh already "
                        "exceeded the maximum duration from t=$pending_refresh_start s.\" >&2\n"
                    "                    return 3\n"
                    "                fi\n"
                    "                refresh_start=\"$pending_refresh_start\"\n"
                    "                echo \"Resuming airflow refresh observation "
                        "window from t=$refresh_start s.\"\n"
                    "            else\n"
                    "                echo \"Discarding incompatible airflow-refresh "
                        "pending state.\" >&2\n"
                    "                rm -f \"$refresh_pending_marker\"\n"
                    "            fi\n"
                    "        fi\n"
                    "        if [[ ! -f \"$refresh_pending_marker\" ]]; then\n"
                    "            printf '%s\n' \"$refresh_start\" > "
                        "\"$refresh_pending_marker\"\n"
                    "        fi\n"
                    "        # Retain the last accepted operating point. The "
                        "first live window must measure the airflow change "
                        "caused by the preceding thermal-only interval, not "
                        "silently establish a fresh baseline. A restarted "
                        "runner begins with an empty array and therefore "
                        "conservatively reacquires one.\n"
                    "        while true; do\n"
                    "            refresh_target=$(awk -v a=\"$current\" -v d=\""
                    << options.airflow_refresh_check_interval
                    << "\" -v start=\"$refresh_start\" -v maximum=\""
                    << options.maximum_airflow_refresh_duration
                    << "\" -v end=\"$requested_end\" "
                        "'BEGIN { x=a+d; limit=start+maximum; "
                        "if(x>limit)x=limit; if(x>end)x=end; "
                        "printf \"%.17g\", x }')\n"
                    "            stage false \"$refresh_target\" "
                    << options.airflow_refresh_maximum_courant_number << ' '
                    << options.maximum_time_step
                    << " \"Adaptive airflow refresh\"\n"
                    "            refresh_elapsed=$(awk -v a=\"$current\" "
                        "-v b=\"$refresh_start\" 'BEGIN { print a-b }')\n"
                    "            if awk -v a=\"$refresh_elapsed\" -v b=\""
                    << options.airflow_refresh_duration
                    << "\" 'BEGIN { exit !(a>=b) }'; then\n"
                    "                if airflow_metrics_converged; then\n"
                    "                    if validate_accepted_airflow_drift; then\n"
                    "                        if [[ \"$long_lag_failed\" == 0 ]]; then\n"
                    "                            airflow_refresh_long_lag_validated=1\n"
                    "                        else\n"
                    "                            record_accepted_airflow_reference || return 3\n"
                    "                            echo \"Airflow settled after an accepted-field shift; this thermal checkpoint remains ineligible for convergence.\"\n"
                    "                        fi\n"
                    "                    else\n"
                    "                        long_lag_failed=1\n"
                    "                        echo \"Accepted airflow shifted beyond the coupled-convergence limit; continuing live-flow settling at this thermal checkpoint.\"\n"
                    "                        continue\n"
                    "                    fi\n"
                    "                    echo \"Airflow refresh converged "
                        "after $refresh_elapsed s.\"\n"
                    "                    airflow_refresh_validated=1\n"
                    "                    rm -f \"$refresh_pending_marker\"\n"
                    "                    return 0\n"
                    "                fi\n"
                    "            fi\n"
                    "            if ! awk -v a=\"$current\" "
                        "-v b=\"$requested_end\" "
                        "'BEGIN { s=(b<0?-b:b); if(s<1)s=1; "
                        "tol=1e-9*s; exit !(a<b-tol) }'; then\n"
                    "                echo \"Airflow refresh reached requested "
                        "end time without convergence; checkpoint remains "
                        "unvalidated and the pending refresh will resume "
                        "before the next thermal-only stage.\"\n"
                    "                return 0\n"
                    "            fi\n"
                    "            if awk -v a=\"$refresh_elapsed\" -v b=\""
                    << options.maximum_airflow_refresh_duration
                    << "\" 'BEGIN { exit !(a>=b) }'; then\n"
                    "                echo \"Airflow refresh failed to converge "
                        "within "
                    << options.maximum_airflow_refresh_duration
                    << " s.\" >&2\n"
                    "                return 3\n"
                    "            fi\n"
                    "        done\n"
                    "    }\n\n";
                output <<
                    "    adaptive_initial_airflow()\n"
                    "    {\n"
                    "        local initial_start=\"$current\" "
                        "initial_elapsed=0 initial_target initial_limit "
                        "pending_initial_start exchange_horizon exchange_target=\n"
                    "        if [[ -s \"$initial_pending_marker\" ]]; then\n"
                    "            pending_initial_start=$(awk 'NF { print $1; exit }' "
                        "\"$initial_pending_marker\")\n"
                    "            if [[ \"$pending_initial_start\" =~ "
                        "^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]] && "
                        "awk -v start=\"$pending_initial_start\" "
                        "-v now=\"$current\" -v maximum=\""
                    << options.airflow_warmup_time
                    << "\" 'BEGIN { scale=(now<0?-now:now); "
                        "if(scale<1)scale=1; tolerance=1e-9*scale; "
                        "exit !(start<=now+tolerance && "
                        "now<=start+maximum+tolerance) }'; then\n"
                    "                initial_start=\"$pending_initial_start\"\n"
                    "                echo \"Resuming initial airflow observation "
                        "window from t=$initial_start s.\"\n"
                    "            else\n"
                    "                echo \"Discarding incompatible initial-airflow "
                        "pending state.\" >&2\n"
                    "                rm -f \"$initial_pending_marker\"\n"
                    "            fi\n"
                    "        fi\n"
                    "        if [[ ! -f \"$initial_pending_marker\" ]]; then\n"
                    "            printf '%s\\n' \"$initial_start\" > "
                        "\"$initial_pending_marker\"\n"
                    "            previous_flows=()\n"
                    "            previous_smoothed_internal_flows=()\n"
                    "            previous_velocity_relative_rms=\"\"\n"
                    "            latest_velocity_relative_rms=\"\"\n"
                    "            rm -f \"$velocity_convergence_state\"\n"
                    "        fi\n"
                    "        initial_limit=$(awk -v start=\"$initial_start\" -v maximum=\""
                    << options.airflow_warmup_time
                    << "\" -v end=\"$requested_end\" "
                        "'BEGIN { limit=start+maximum; print (limit<end?limit:end) }')\n"
                    "        while awk -v a=\"$current\" "
                        "-v b=\"$initial_limit\" "
                        "'BEGIN { s=(b<0?-b:b); if(s<1)s=1; "
                        "tol=1e-9*s; exit !(a<b-tol) }'; do\n"
                    "            initial_target=$(awk -v a=\"$current\" -v d=\""
                    << options.initial_airflow_check_interval
                    << "\" -v exchange_target=\"$exchange_target\" -v limit=\"$initial_limit\" "
                        "'BEGIN { x=a+d; if(exchange_target!=\"\" && exchange_target>x)x=exchange_target; if(x>limit)x=limit; "
                        "printf \"%.17g\", x }')\n"
                    "            stage false \"$initial_target\" "
                    << options.maximum_courant_number << ' '
                    << options.maximum_time_step
                    << " \"Adaptive initial airflow\"\n"
                    "            initial_elapsed=$(awk -v a=\"$current\" "
                        "-v b=\"$initial_start\" 'BEGIN { print a-b }')\n"
                    "            minimum_observation=\""
                    << options.minimum_initial_airflow_duration
                    << "\"\n"
                    "            if [[ -f \"$mapped_state_marker\" ]]; then\n"
                    "                minimum_observation=\""
                    << options.initial_airflow_check_interval
                    << "\"\n"
                    "            fi\n"
                    "            if awk -v a=\"$initial_elapsed\" -v b=\"$minimum_observation\" 'BEGIN { exit !(a>=b) }'; then\n"
                    "                if airflow_metrics_converged; then\n"
                    "                    if [[ -f \"$mapped_state_marker\" ]]; then\n"
                    "                        exchange_horizon=0\n"
                    "                        echo \"Mapped airflow skips the cold-start air-exchange horizon after live spatial and device validation.\"\n"
                    "                    else\n"
                    "                        exchange_horizon=$(awk -v exchange=\"$latest_air_exchange_time\" -v fraction=\""
                    << options.minimum_initial_air_exchange_fraction
                    << "\" 'BEGIN { print (exchange<1e29?exchange*fraction:0) }')\n"
                    "                    fi\n"
                    "                    if ! awk -v elapsed=\"$initial_elapsed\" -v required=\"$exchange_horizon\" 'BEGIN { exit !(elapsed>=required) }'; then\n"
                    "                        echo \"Initial airflow device flows pass, but spatial settling requires $exchange_horizon s ($initial_elapsed s completed; air-exchange time $latest_air_exchange_time s).\"\n"
                    "                        exchange_target=$(awk -v start=\"$initial_start\" -v required=\"$exchange_horizon\" -v limit=\"$initial_limit\" 'BEGIN { x=start+required; if(x>limit)x=limit; printf \"%.17g\", x }')\n"
                    "                        echo \"Advancing initial airflow directly to t=$exchange_target s before the next convergence check.\"\n"
                    "                        summary \"initial_air_exchange_advance current=$current target=$exchange_target requiredElapsed=$exchange_horizon\"\n"
                    "                        continue\n"
                    "                    fi\n"
                    "                    echo \"Initial airflow converged "
                        "after $initial_elapsed s beyond the fan ramp; "
                        "switching to thermal-only mode.\"\n"
                    "                    if ! record_accepted_airflow_reference; then\n"
                    "                        echo \"Unable to preserve the accepted initial airflow reference.\" >&2\n"
                    "                        return 3\n"
                    "                    fi\n"
                    "                    touch "
                        "\"$initial_convergence_marker\"\n"
                    "                    rm -f \"$initial_pending_marker\" "
                        "\"$mapped_state_marker\"\n"
                    "                    fan_options_source="
                        "\"$full_fan_options\"\n"
                    "                    restore_full_fan_options\n"
                    "                    echo \"Restored full fluid heat sources "
                        "for thermal evolution.\"\n"
                    "                    return 0\n"
                    "                fi\n"
                    "            fi\n"
                    "        done\n"
                    "        if ! awk -v a=\"$current\" "
                        "-v b=\"$requested_end\" "
                        "'BEGIN { s=(b<0?-b:b); if(s<1)s=1; "
                        "tol=1e-9*s; exit !(a>=b-tol) }'; then\n"
                    "            echo \"Initial airflow failed to converge "
                        "before the airflow_warmup_time safety limit of "
                    << options.airflow_warmup_time << " s.\" >&2\n"
                    "            return 3\n"
                    "        fi\n"
                    "    }\n\n";
            }
            if(options.use_adaptive_airflow_refresh) {
                output <<
                    "    if [[ ! -f \"$initial_convergence_marker\" ]] && "
                        "awk -v a=\"$current\" -v b=\"$requested_end\" "
                        "'BEGIN { exit !(a<b) }'; then\n"
                    "        echo \"Adaptively finding initial airflow "
                        "operating point.\"\n"
                    "        adaptive_initial_airflow\n"
                    "    fi\n"
                    "    if [[ -f \"$refresh_pending_marker\" ]] && "
                        "awk -v a=\"$current\" -v b=\"$requested_end\" "
                        "'BEGIN { exit !(a<b) }'; then\n"
                    "        echo \"Retrying interrupted airflow refresh.\"\n"
                    "        adaptive_airflow_refresh\n"
                    "    fi\n";
            } else {
                output <<
                    "    if awk -v a=\"$current\" -v b=\""
                    << options.airflow_warmup_time
                    << "\" 'BEGIN { exit !(a<b) }'; then\n"
                    "        warm_target=$(awk -v a=\""
                    << options.airflow_warmup_time
                    << "\" -v b=\"$requested_end\" "
                        "'BEGIN { print (a<b ? a : b) }')\n"
                    "        stage false \"$warm_target\" "
                    << options.maximum_courant_number << ' '
                    << options.maximum_time_step
                    << " \"Fixed airflow warm-up\"\n"
                    "    fi\n";
            }
            output <<
                "    while awk -v a=\"$current\" -v b=\"$requested_end\" "
                    "'BEGIN { s=(b<0?-b:b); if(s<1)s=1; "
                    "tol=1e-9*s; exit !(a<b-tol) }'; do\n"
                "        frozen_target=$(awk -v a=\"$current\" "
                    "-v d=\"$airflow_refresh_interval\" "
                    "-v b=\"$requested_end\" "
                    "'BEGIN { x=(int(a/d)+1)*d; "
                    "if(x<=a+1e-9)x+=d; "
                    "print (x<b ? x : b) }')\n"
                "        stage true \"$frozen_target\" "
                << options.frozen_flow_maximum_courant_number << ' '
                << options.frozen_flow_maximum_time_step
                << " \"Implicit thermal-only stage (airflow held)\"\n";
            if(options.stop_when_thermally_converged) {
                output <<
                "        thermal_candidate=0\n"
                "        airflow_validated=0\n"
                "        airflow_long_lag_validated=0\n"
                "        if thermal_metrics_converged; then\n"
                "            thermal_candidate=1\n"
                "        fi\n";
            }
            if(options.use_adaptive_airflow_refresh) {
                output <<
                    "        terminal_requested_end=\"\"\n"
                    "        if ! awk -v a=\"$current\" "
                        "-v b=\"$requested_end\" "
                        "'BEGIN { s=(b<0?-b:b); if(s<1)s=1; "
                        "tol=1e-9*s; exit !(a<b-tol) }'; then\n"
                    "            terminal_requested_end=\"$requested_end\"\n"
                    "            requested_end=$(awk -v a=\"$current\" -v d=\""
                    << options.maximum_airflow_refresh_duration
                    << "\" 'BEGIN { printf \"%.17g\", a+d }')\n"
                    "            echo \"Refreshing airflow at terminal thermal "
                        "checkpoint t=$current s before final reconstruction.\"\n"
                    "        fi\n"
                    "        adaptive_airflow_refresh\n";
                if(options.stop_when_thermally_converged)
                    output <<
                        "        airflow_validated=\"$airflow_refresh_validated\"\n"
                        "        airflow_long_lag_validated=\"$airflow_refresh_long_lag_validated\"\n";
                output <<
                    "        if [[ -n \"$terminal_requested_end\" ]]; then\n"
                    "            requested_end=\"$terminal_requested_end\"\n"
                    "        fi\n";
            } else {
                output <<
                    "        if awk -v a=\"$current\" "
                        "-v b=\"$requested_end\" "
                        "'BEGIN { s=(b<0?-b:b); if(s<1)s=1; "
                        "tol=1e-9*s; exit !(a<b-tol) }'; then\n"
                    "            refresh_target=$(awk -v a=\"$current\" -v d=\""
                    << options.airflow_refresh_duration
                    << "\" -v b=\"$requested_end\" "
                        "'BEGIN { x=a+d; if(x>b)x=b; "
                        "printf \"%.17g\", x }')\n"
                    "            stage false \"$refresh_target\" "
                    << options.maximum_courant_number << ' '
                    << options.maximum_time_step
                    << " \"Airflow refresh\"\n"
                    "        fi\n";
            }
            if(options.stop_when_thermally_converged) {
                output <<
                "        if [[ \"$thermal_candidate\" == 1 && "
                    "\"$airflow_validated\" == 1 && "
                    "\"$airflow_long_lag_validated\" == 1 ]]; then\n"
                "            streak=$(cat \"$thermal_convergence_streak\" "
                    "2>/dev/null || echo 0)\n"
                "            streak=$((streak+1))\n"
                "            printf '%s\\n' \"$streak\" > "
                    "\"$thermal_convergence_streak\"\n"
                "            echo \"Thermal convergence checkpoint "
                    "$streak/"
                    << options.thermal_convergence_required_checkpoints
                    << " accepted with airflow metrics converged.\"\n"
                "            summary \"checkpoint time=$current streak=$streak required="
                    << options.thermal_convergence_required_checkpoints
                    << " accepted=true\"\n"
                "            if (( streak >= "
                    << options.thermal_convergence_required_checkpoints
                    << " )); then\n"
                "                record_accepted_airflow_reference || return 3\n"
                "                echo \"Rebased accepted airflow reference at converged checkpoint t=$current s.\"\n"
                "                summary \"airflow_reference_rebased time=$current reason=convergedCheckpoint\"\n"
                "                echo \"Thermal and airflow convergence "
                    "criteria satisfied at validated checkpoint t=$current s "
                    "(requested end time $requested_end s).\"\n"
                "                break\n"
                "            fi\n"
                "        elif [[ \"$airflow_validated\" == 1 && "
                    "\"$airflow_long_lag_validated\" == 1 ]]; then\n"
                "            printf '0\\n' > \"$thermal_convergence_streak\"\n"
                "            record_accepted_airflow_reference || return 3\n"
                "        elif [[ \"$airflow_validated\" == 1 ]]; then\n"
                "            printf '0\\n' > \"$thermal_convergence_streak\"\n"
                "            echo \"Resetting thermal convergence streak: accepted airflow has not converged across refresh cycles.\"\n"
                "            summary \"checkpoint time=$current streak=0 accepted=false reason=acceptedAirflowLongLag\"\n"
                "        else\n"
                "            streak=$(cat \"$thermal_convergence_streak\" "
                    "2>/dev/null || echo 0)\n"
                "            echo \"Preserving thermal convergence streak "
                    "$streak: this terminal partial stage had no airflow "
                    "validation.\"\n"
                "        fi\n";
            }
            output <<
                "    done\n"
                "else\n"
                "    warm_current=$(\"$foam_launcher\" foamListTimes "
                    "-case \"$case_dir\" -processor -latestTime "
                    "2>/dev/null || echo 0)\n"
                "    warm_current=\"${warm_current##*$'\\n'}\"\n"
                "    warm_current=\"${warm_current:-0}\"\n"
                "    if [[ \"$mode\" == \"--warm-start\" ]] && "
                    "awk -v a=\"$warm_current\" 'BEGIN { exit !(a==0) }' && "
                    "grep -Eq '^[[:space:]]*internalField[[:space:]]+nonuniform' "
                    "\"$case_dir/0/fluid/U\"; then\n"
                "        touch \"$mapped_state_marker\"\n"
                "        fan_options_source=\"$full_fan_options\"\n"
                "        restore_full_fan_options\n"
                "        echo \"Detected mapped nonuniform velocity fields; retaining full heat sources and skipping the cold fan ramp.\"\n"
                "    fi\n"
                "    if [[ \"$mode\" == \"--warm-start\" ]]; then\n"
                "        warm_interval=$(awk -v end=\"$requested_end\" "
                    "-v start=\"$warm_current\" 'BEGIN { d=end-start; "
                    "if (d<=0) exit 1; printf \"%.17g\", d }') || {\n"
                "            echo \"Warm-start end time must be greater "
                    "than latest time $warm_current.\" >&2\n"
                "            exit 2\n"
                "        }\n"
                "        warm_restart_dt=$(awk -v interval=\"$warm_interval\" "
                    "-v maximum=\""
                << options.maximum_time_step
                << "\" 'BEGIN { print (interval<maximum?interval:maximum) }')\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry startFrom -set latestTime\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry endTime -set \"$requested_end\"\n"
                "        # Use the authoritative processor checkpoint so "
                    "the requested endpoint is always written.\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry writeControl -set adjustableRunTime\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry writeInterval -set \"$warm_interval\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry adjustTimeStep -set true\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry deltaT -set \"$warm_restart_dt\"\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry maxDeltaT -set "
                << options.maximum_time_step << "\n"
                "        echo \"Running airflow/thermal warm start to "
                    "t=$requested_end s from t=$warm_current s.\"\n"
                "    fi\n";
            if(options.use_fan_startup_ramp) {
                output <<
                    "    if [[ \"$mode\" == \"--warm-start\" ]] && "
                        "[[ ! -f \"$mapped_state_marker\" ]] && "
                        "awk -v a=\"$warm_current\" -v end=\""
                    << options.fan_startup_ramp_time
                    << "\" 'BEGIN { exit !(a<end) }'; then\n"
                    "        run_fan_ramp chtMultiRegionFoam "
                        "\"$warm_current\" \"$requested_end\"\n"
                    "        warm_current=\"$ramp_current\"\n"
                    "    fi\n";
            }
            output <<
                "    if awk -v a=\"$warm_current\" -v b=\"$requested_end\" "
                    "'BEGIN { exit !(a<b) }'; then\n"
                "        \"$foam_launcher\" foamDictionary -precision 17 "
                    "\"$case_dir/system/controlDict\" "
                    "-entry endTime -set \"$requested_end\"\n"
                "        \"$foam_launcher\" mpirun -np \"$processes\" "
                    "chtMultiRegionFoam -case \"$case_dir\" -parallel\n"
                "    fi\n"
                "fi\n";
        } else {
            output <<
                "if [[ \"$mode\" == \"--multirate\" ]]; then\n"
                "    echo \"Multirate mode was not enabled during export.\" "
                    ">&2\n"
                "    exit 2\n"
                "fi\n"
                "\"$foam_launcher\" mpirun -np \"$processes\" "
                    "chtMultiRegionFoam -case \"$case_dir\" -parallel\n";
        }
        output <<
            "reconstruct_time=$(\"$foam_launcher\" foamListTimes "
                "-case \"$case_dir\" -processor -latestTime "
                "2>/dev/null || echo 0)\n"
            "reconstruct_time=\"${reconstruct_time##*$'\\n'}\"\n"
            "\"$foam_launcher\" reconstructPar -case \"$case_dir\" "
                "-allRegions -time \"$reconstruct_time\"\n\n"
            "\"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/fluid/fvSolution\" "
                "-entry PIMPLE/frozenFlow -set false\n"
            "\"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/fluid/fvSolution\" "
                "-entry PIMPLE/semiFrozenFlow -set false\n"
            "\"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/fluid/fvSolution\" "
                "-entry PIMPLE/thermalOnlyFlow -set false\n"
            "\"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/fluid/fvSolution\" "
                "-entry PIMPLE/momentumPredictor -set true\n"
            "\"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry maxCo -set " << options.maximum_courant_number << "\n"
            "\"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry maxDeltaT -set " << options.maximum_time_step << "\n"
            "\"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry adjustTimeStep -set true\n"
            "if [[ \"$mode\" == \"--warm-start\" ]]; then\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry startFrom -set startTime\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry startTime -set \"$reconstruct_time\"\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry endTime -set " << options.end_time << "\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry writeControl -set adjustableRunTime\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry writeInterval -set "
                << options.field_write_interval << "\n"
            "    rm -rf -- \"$case_dir/.accepted_airflow_reference\" "
                "\"$case_dir/.accepted_airflow_reference.tmp\"\n"
            "    rm -f -- \"$case_dir/.airflow_convergence_state\" "
                "\"$case_dir/.velocity_convergence_state\" "
                "\"$case_dir/.thermal_convergence_state\" "
                "\"$case_dir/.thermal_convergence_streak\" "
                "\"$case_dir/.airflow_refresh_pending\"\n"
            "    echo \"Warm start invalidated cached airflow and thermal "
                "convergence references.\"\n"
            "    echo \"Warm start complete. The normal transient is configured "
                "to resume from latestTime.\"\n"
            "elif [[ \"$mode\" == \"--multirate\" ]]; then\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry startFrom -set startTime\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry startTime -set \"$reconstruct_time\"\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry endTime -set " << options.end_time << "\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry writeControl -set adjustableRunTime\n"
            "    \"$foam_launcher\" foamDictionary -precision 17 "
                "\"$case_dir/system/controlDict\" "
                "-entry writeInterval -set "
                << options.field_write_interval << "\n"
            "    echo \"Multirate run complete; production controls restored.\"\n"
            "    summary \"run_complete mode=$mode reconstructedTime=$reconstruct_time\"\n"
            "else\n"
            "    echo \"Parallel CHT run and latest-time reconstruction complete.\"\n"
            "fi\n";
    }
};

#endif

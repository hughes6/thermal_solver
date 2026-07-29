#ifndef OPENFOAM_EXPORTER_HPP
#define OPENFOAM_EXPORTER_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <filesystem>
#include <fstream>
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
    double fan_curve_extension_multiplier = 2.0;
    bool use_multirate_thermal = false;
    double airflow_warmup_time = 5.0;
    // Retain these names until the TOML schema is finalized. They control the
    // thermal-only stage, not OpenFOAM's native frozenFlow implementation.
    double frozen_flow_maximum_time_step = 1.0;
    double frozen_flow_maximum_courant_number = 1000.0;
    double airflow_refresh_interval = 300.0;
    double airflow_refresh_duration = 1.0;
    bool use_adaptive_airflow_refresh = true;
    double airflow_refresh_check_interval = 1.0;
    double maximum_airflow_refresh_duration = 20.0;
    double maximum_mass_imbalance_fraction = 0.01;
    double maximum_device_flow_change_fraction = 0.02;
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

        const std::filesystem::path poly_mesh =
            options.case_directory / "constant" / "polyMesh";
        if(std::filesystem::exists(poly_mesh) && !options.overwrite)
            throw std::runtime_error(
                "OpenFoamExporter: polyMesh already exists at '" +
                poly_mesh.string() + "'. Set overwrite=true to replace files.");

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
        if(options.parallel_processes < 1)
            throw std::invalid_argument(
                "OpenFoamExporter: parallel_processes must be positive.");
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
        }
        write_control_dict(
            mesh, options,
            options.case_directory / "system" / "controlDict");
        write_decompose_par_dict(
            mesh, options.parallel_processes,
            options.case_directory / "system" / "decomposeParDict");
        write_fv_schemes(options.case_directory / "system" / "fvSchemes");
        write_fv_solution(
            options, options.case_directory / "system" / "fvSolution");
        write_region_preparation_script(
            mesh, options,
            options.case_directory / "prepare_regions.sh");
        write_run_script(options.case_directory / "run_cht.sh");
        write_parallel_run_script(
            mesh, options,
            options.case_directory / "run_parallel.sh");
    }

private:
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
                    "' contains no solid cells.");
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
        const bool resisted_vent =
            options.use_vent_pressure_loss &&
            patch.kind == Mesh::OpenFoamBoundaryPatch::Kind::Vent;
        const bool curve_fan =
            options.use_fan_curves && patch.fan_has_curve;
        return resisted_vent || curve_fan;
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
            if(patch.fan_has_curve && options.use_fan_curves) {
                dict << "{ name " << name
                     << "_faces; type faceSet; action new;\n"
                     << "  source patchToFace; patch "
                     << foam_word(patch.name) << "; }\n"
                     << "{ name " << name
                     << "_faces; type faceZoneSet; action new;\n"
                     << "  source setToFaceZone; faceSet " << name
                     << "_faces; }\n";
            }
            dict << ");\n";
        }
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
                options.maximum_airflow_refresh_duration,
                "maximum_airflow_refresh_duration");
            validate_positive_finite(
                options.maximum_mass_imbalance_fraction,
                "maximum_mass_imbalance_fraction");
            validate_positive_finite(
                options.maximum_device_flow_change_fraction,
                "maximum_device_flow_change_fraction");
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
            "writeControl    adjustableRunTime;\n"
            "writeInterval   " << options.field_write_interval << ";\n\n"
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
        for(const auto& device :
            mesh.get_openfoam_internal_flow_devices()) {
            if(device.kind!=
               Mesh::OpenFoamInternalFlowDevice::Kind::Fan)
                continue;
            const std::string name=internal_device_name(device);
            output <<
                "    " << name << "_mass_flow\n"
                "    {\n"
                "        type surfaceFieldValue;\n"
                "        libs (fieldFunctionObjects);\n"
                "        region fluid;\n"
                "        writeControl adjustableRunTime;\n"
                "        writeInterval " << options.report_interval << ";\n"
                "        regionType faceZone;\n"
                "        name " << name << "_faces;\n"
                "        operation sum;\n"
                "        writeFields false;\n"
                "        fields (phi);\n"
                "    }\n";
        }
        output << "}\n";
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
                       << " useImplicit true;\n qrNbr none;\n qr none;\n"
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
                } else if(curve_fan || resisted_vent) {
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
               << " useImplicit true;\n qrNbr none;\n qr none;\n"
               << " value uniform " << temperature << ";\n"
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
        const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
        std::ofstream output(path);
        require_stream(output,path);
        write_header(output,"dictionary","fvSolution","system");
        output << "PIMPLE\n{\n    nOuterCorrectors "
               << (options.use_fan_curves ? 3 : 1) << ";\n}\n";
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
            << (options.use_fan_curves ? 3 : 2) << ";\n"
            " nNonOrthogonalCorrectors 0;\n"
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
            if(source.component_id != component.id) continue;
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
        const std::filesystem::path& path) {
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
                           << (a-b*q-c*q*q) << ")\n";
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
                           << (a-b*q-c*q*q) << ")\n";
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
            options,
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
            "\"$foam_launcher\" splitMeshRegions "
                "-case \"$case_dir\" -cellZones -overwrite\n"
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
            const std::string region =
                foam_word(components[
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
            "\"$foam_launcher\" checkMesh "
                "-case \"$case_dir\" -allRegions "
                "-allGeometry -allTopology\n\n"
            "echo \"Region meshes prepared successfully.\"\n";
    }

    static void write_run_script(const std::filesystem::path& path) {
        std::ofstream output(path,std::ios::binary);
        require_stream(output,path);
        output <<
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n\n"
            "case_dir=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
            "foam_launcher=\"${OPENFOAM_LAUNCHER:-openfoam2606}\"\n\n"
            "bash \"$case_dir/prepare_regions.sh\"\n"
            "\"$foam_launcher\" chtMultiRegionFoam -case \"$case_dir\" \"$@\"\n";
    }

    static void write_parallel_run_script(
        const Mesh& mesh,
        const OpenFoamExportOptions& options,
        const std::filesystem::path& path) {
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
            "if ! [[ \"$processes\" =~ ^[1-9][0-9]*$ ]]; then\n"
            "    echo \"Process count must be a positive integer.\" >&2\n"
            "    exit 2\n"
            "fi\n"
            "if [[ \"$mode\" != \"run\" && \"$mode\" != \"--warm-start\" "
                "&& \"$mode\" != \"--multirate\" ]]; then\n"
            "    echo \"Usage: $0 [processes] "
                "[--warm-start|--multirate [end-time]]\" >&2\n"
            "    exit 2\n"
            "fi\n"
            "if [[ \"$mode\" != \"run\" ]] && "
                "! [[ \"$requested_end\" =~ ^[0-9]+([.][0-9]+)?$ ]]; then\n"
            "    echo \"Requested end time must be a positive number.\" >&2\n"
            "    exit 2\n"
            "fi\n\n"
            "if [[ \"$mode\" == \"--warm-start\" ]]; then\n"
            "    latest_time=$(\"$foam_launcher\" foamListTimes "
                "-case \"$case_dir\" -latestTime 2>/dev/null || echo 0)\n"
            "    latest_time=\"${latest_time##*$'\\n'}\"\n"
            "    warm_interval=$(awk -v end=\"$requested_end\" "
                "-v start=\"${latest_time:-0}\" "
                "'BEGIN { d=end-start; if (d<=0) exit 1; "
                "printf \"%.17g\", d }') || {\n"
            "        echo \"Warm-start end time must be greater than latest "
                "time ${latest_time:-0}.\" >&2\n"
            "        exit 2\n"
            "    }\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry startFrom -set latestTime\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry endTime -set \"$requested_end\"\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry writeInterval -set \"$warm_interval\"\n"
            "    echo \"Running airflow/thermal warm start to "
                "t=$requested_end s.\"\n"
            "fi\n\n"
            "bash \"$case_dir/prepare_regions.sh\"\n"
            "\"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/decomposeParDict\" "
                "-entry numberOfSubdomains -set \"$processes\"\n"
            "\"$foam_launcher\" decomposePar -case \"$case_dir\" "
                "-allRegions -force\n\n";
        if(options.use_multirate_thermal) {
            output <<
                "if [[ \"$mode\" == \"--multirate\" ]]; then\n"
                "    current=$(\"$foam_launcher\" foamListTimes "
                    "-case \"$case_dir\" -processor -latestTime "
                    "2>/dev/null || echo 0)\n"
                "    current=\"${current##*$'\\n'}\"\n"
                "    current=\"${current:-0}\"\n";
            output << "    boundary_flow_names=(";
            for(const auto& patch : mesh.get_openfoam_boundary_patches())
                output << '"' << foam_word(patch.name) << "\" ";
            output << ")\n    tracked_flow_names=(";
            for(const auto& patch : mesh.get_openfoam_boundary_patches())
                output << '"' << foam_word(patch.name) << "\" ";
            for(const auto& device :
                mesh.get_openfoam_internal_flow_devices()) {
                if(device.kind ==
                   Mesh::OpenFoamInternalFlowDevice::Kind::Fan)
                    output << '"' << internal_device_name(device)
                           << "_faces\" ";
            }
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
                "    declare -A previous_flows=()\n"
                "    airflow_metrics_converged()\n"
                "    {\n"
                "        local report name value rule expected net=0 "
                    "sum_abs=0\n"
                "        local imbalance stable=1 directions_ok=1 "
                    "maximum_change=0 change\n"
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
                "            if [[ -n \"${previous_flows[$name]+set}\" ]]; "
                    "then\n"
                "                change=$(awk -v a=\"$value\" "
                    "-v b=\"${previous_flows[$name]}\" "
                    "'BEGIN { d=a-b; if(d<0)d=-d; s=b; if(s<0)s=-s; "
                    "if(s<1e-12)s=1e-12; print d/s }')\n"
                "                maximum_change=$(awk -v a=\"$maximum_change\" "
                    "-v b=\"$change\" 'BEGIN { print (a>b?a:b) }')\n"
                "            else\n"
                "                stable=0\n"
                "            fi\n"
                "        done\n"
                "        for name in \"${boundary_flow_names[@]}\"; do\n"
                "            value=\"${flows[$name]}\"\n"
                "            net=$(awk -v a=\"$net\" -v b=\"$value\" "
                    "'BEGIN { print a+b }')\n"
                "            sum_abs=$(awk -v a=\"$sum_abs\" -v b=\"$value\" "
                    "'BEGIN { if(b<0)b=-b; print a+b }')\n"
                "        done\n"
                "        imbalance=$(awk -v n=\"$net\" -v s=\"$sum_abs\" "
                    "'BEGIN { if(n<0)n=-n; d=0.5*s; "
                    "print (d>1e-12?n/d:1e30) }')\n"
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
                "        for name in \"${tracked_flow_names[@]}\"; do\n"
                "            previous_flows[\"$name\"]=\"${flows[$name]}\"\n"
                "        done\n"
                "        if ! awk -v v=\"$imbalance\" -v limit=\""
                << options.maximum_mass_imbalance_fraction
                << "\" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi\n"
                "        if ! awk -v v=\"$maximum_change\" -v limit=\""
                << options.maximum_device_flow_change_fraction
                << "\" 'BEGIN { exit !(v<=limit) }'; then stable=0; fi\n"
                "        echo \"Airflow refresh metrics: imbalance=$imbalance, "
                    "maxFlowChange=$maximum_change, directionsOK="
                    "$directions_ok\"\n"
                "        [[ \"$stable\" == 1 && \"$directions_ok\" == 1 ]]\n"
                "    }\n"
                "    stage()\n"
                "    {\n"
                "        local thermal_only=\"$1\" target=\"$2\" max_co=\"$3\" "
                    "max_dt=\"$4\" label=\"$5\"\n"
                "        local interval\n"
                "        interval=$(awk -v end=\"$target\" -v start=\"$current\" "
                    "'BEGIN { printf \"%.17g\", end-start }')\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/fluid/fvSolution\" "
                    "-entry PIMPLE/frozenFlow -set false\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/fluid/fvSolution\" "
                    "-entry PIMPLE/semiFrozenFlow -set false\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/fluid/fvSolution\" "
                    "-entry PIMPLE/thermalOnlyFlow -set \"$thermal_only\"\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/fluid/fvSolution\" "
                    "-entry PIMPLE/momentumPredictor -set true\n"
                "        if [[ \"$thermal_only\" == \"true\" ]]; then\n"
                "            adjust_time_step=false\n"
                "            stage_dt=\"$max_dt\"\n"
                "        else\n"
                "            adjust_time_step=true\n"
                "            stage_dt=\""
                    << options.initial_time_step << "\"\n"
                "        fi\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/controlDict\" "
                    "-entry adjustTimeStep -set \"$adjust_time_step\"\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/controlDict\" "
                    "-entry deltaT -set \"$stage_dt\"\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/controlDict\" "
                    "-entry startFrom -set latestTime\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/controlDict\" "
                    "-entry endTime -set \"$target\"\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/controlDict\" "
                    "-entry writeInterval -set \"$interval\"\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/controlDict\" "
                    "-entry maxCo -set \"$max_co\"\n"
                "        \"$foam_launcher\" foamDictionary "
                    "\"$case_dir/system/controlDict\" "
                    "-entry maxDeltaT -set \"$max_dt\"\n"
                "        echo \"$label: t=$current -> $target\"\n"
                "        \"$foam_launcher\" mpirun -np \"$processes\" "
                    "semiFrozenChtMultiRegionFoam "
                    "-case \"$case_dir\" -parallel\n"
                "        current=\"$target\"\n"
                "    }\n\n";
            if(options.use_adaptive_airflow_refresh) {
                output <<
                    "    adaptive_airflow_refresh()\n"
                    "    {\n"
                    "        local refresh_start=\"$current\" "
                        "refresh_elapsed=0 refresh_target\n"
                    "        previous_flows=()\n"
                    "        while true; do\n"
                    "            refresh_target=$(awk -v a=\"$current\" -v d=\""
                    << options.airflow_refresh_check_interval
                    << "\" -v start=\"$refresh_start\" -v maximum=\""
                    << options.maximum_airflow_refresh_duration
                    << "\" -v end=\"$requested_end\" "
                        "'BEGIN { x=a+d; limit=start+maximum; "
                        "if(x>limit)x=limit; if(x>end)x=end; print x }')\n"
                    "            stage false \"$refresh_target\" "
                    << options.maximum_courant_number << ' '
                    << options.maximum_time_step
                    << " \"Adaptive airflow refresh\"\n"
                    "            refresh_elapsed=$(awk -v a=\"$current\" "
                        "-v b=\"$refresh_start\" 'BEGIN { print a-b }')\n"
                    "            if awk -v a=\"$refresh_elapsed\" -v b=\""
                    << options.airflow_refresh_duration
                    << "\" 'BEGIN { exit !(a>=b) }'; then\n"
                    "                if airflow_metrics_converged; then\n"
                    "                    echo \"Airflow refresh converged "
                        "after $refresh_elapsed s.\"\n"
                    "                    return 0\n"
                    "                fi\n"
                    "            fi\n"
                    "            if ! awk -v a=\"$current\" "
                        "-v b=\"$requested_end\" "
                        "'BEGIN { exit !(a<b) }'; then return 0; fi\n"
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
            }
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
                << options.maximum_time_step << " \"Airflow warm-up\"\n"
                "    fi\n";
            if(options.use_adaptive_airflow_refresh) {
                output <<
                    "    if awk -v a=\"$current\" -v b=\"$requested_end\" "
                        "'BEGIN { exit !(a<b) }'; then\n"
                    "        echo \"Checking initial airflow operating point.\"\n"
                    "        adaptive_airflow_refresh\n"
                    "    fi\n";
            }
            output <<
                "    while awk -v a=\"$current\" -v b=\"$requested_end\" "
                    "'BEGIN { exit !(a<b) }'; do\n"
                "        frozen_target=$(awk -v a=\"$current\" -v d=\""
                << options.airflow_refresh_interval
                << "\" -v b=\"$requested_end\" "
                    "'BEGIN { x=a+d; print (x<b ? x : b) }')\n"
                "        stage true \"$frozen_target\" "
                << options.frozen_flow_maximum_courant_number << ' '
                << options.frozen_flow_maximum_time_step
                << " \"Implicit thermal-only stage (airflow held)\"\n"
                "        if awk -v a=\"$current\" -v b=\"$requested_end\" "
                    "'BEGIN { exit !(a<b) }'; then\n";
            if(options.use_adaptive_airflow_refresh) {
                output <<
                    "            adaptive_airflow_refresh\n";
            } else {
                output <<
                    "            refresh_target=$(awk -v a=\"$current\" -v d=\""
                    << options.airflow_refresh_duration
                    << "\" -v b=\"$requested_end\" "
                        "'BEGIN { x=a+d; print (x<b ? x : b) }')\n"
                    "            stage false \"$refresh_target\" "
                    << options.maximum_courant_number << ' '
                    << options.maximum_time_step
                    << " \"Airflow refresh\"\n";
            }
            output <<
                "        fi\n"
                "    done\n"
                "else\n"
                "    \"$foam_launcher\" mpirun -np \"$processes\" "
                    "chtMultiRegionFoam -case \"$case_dir\" -parallel\n"
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
            "\"$foam_launcher\" reconstructPar -case \"$case_dir\" "
                "-allRegions -latestTime -withZero\n\n"
            "\"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/fluid/fvSolution\" "
                "-entry PIMPLE/frozenFlow -set false\n"
            "\"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/fluid/fvSolution\" "
                "-entry PIMPLE/semiFrozenFlow -set false\n"
            "\"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/fluid/fvSolution\" "
                "-entry PIMPLE/thermalOnlyFlow -set false\n"
            "\"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/fluid/fvSolution\" "
                "-entry PIMPLE/momentumPredictor -set true\n"
            "\"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry maxCo -set " << options.maximum_courant_number << "\n"
            "\"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry maxDeltaT -set " << options.maximum_time_step << "\n"
            "\"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry adjustTimeStep -set true\n"
            "if [[ \"$mode\" == \"--warm-start\" ]]; then\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry startFrom -set latestTime\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry endTime -set " << options.end_time << "\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry writeInterval -set "
                << options.field_write_interval << "\n"
            "    echo \"Warm start complete. The normal transient is configured "
                "to resume from latestTime.\"\n"
            "elif [[ \"$mode\" == \"--multirate\" ]]; then\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry startFrom -set latestTime\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry endTime -set " << options.end_time << "\n"
            "    \"$foam_launcher\" foamDictionary "
                "\"$case_dir/system/controlDict\" "
                "-entry writeInterval -set "
                << options.field_write_interval << "\n"
            "    echo \"Multirate run complete; production controls restored.\"\n"
            "else\n"
            "    echo \"Parallel CHT run and latest-time reconstruction complete.\"\n"
            "fi\n";
    }
};

#endif

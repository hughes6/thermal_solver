#ifndef MESH_HPP
#define MESH_HPP

#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <limits>
#include <string>

#include "cell.hpp"
#include "component.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "rack.hpp"
#include "vent.hpp"
#include "workload.hpp"

class Mesh {
public:
    struct OpenFoamCellMetadata {
        enum class RegionType : unsigned char {
            Fluid,
            Solid
        };

        RegionType region_type = RegionType::Fluid;
        int component_id = -1;
        int material_id = -1;
        int heat_source_id = -1;
    };

    struct OpenFoamComponentRegion {
        int id = -1;
        std::string name;
        double conductivity = 0.0;
        double rho = 0.0;
        double cp = 0.0;
    };

    struct OpenFoamHeatSourceRegion {
        int id = -1;
        int component_id = -1;
        std::string name;
        double watts = 0.0;
        bool fluid = false;
    };

    struct OpenFoamBoundaryPatch {
        enum class Kind : unsigned char {
            Inlet,
            Outlet,
            Vent
        };

        int id = -1;
        std::string name;
        Kind kind = Kind::Vent;
        double vent_free_area_m2 = 0.0;
        double vent_discharge_coefficient = 0.0;
        bool fan_has_curve = false;
        double fan_curve_a = 0.0;
        double fan_curve_b = 0.0;
        double fan_curve_c = 0.0;
        double fan_rated_density = 1.2;
        double fan_reference_flow_m3s = 0.0;
        std::array<double,3> direction{0.0,0.0,0.0};
        std::vector<std::size_t> adjacent_cells;
        double source_zone_thickness = 0.0;
        std::array<double,3> requested_center{0.0,0.0,0.0};
        std::array<double,3> requested_size{0.0,0.0,0.0};
        bool circular = false;
        double requested_diameter = 0.0;
    };

    struct OpenFoamInternalFlowDevice {
        enum class Kind : unsigned char { Fan, Vent };
        int id = -1;
        int component_id = -1;
        std::string component_name;
        std::string name;
        Kind kind = Kind::Vent;
        std::array<double,3> direction{0.0,0.0,0.0};
        std::vector<std::size_t> cells;
        double thickness = 0.0;
        double free_area_m2 = 0.0;
        double discharge_coefficient = 0.0;
        double curve_a = 0.0;
        double curve_b = 0.0;
        double curve_c = 0.0;
        double rated_density = 1.2;
        double reference_flow_m3s = 0.0;
        std::array<double,3> requested_center{0.0,0.0,0.0};
        std::array<double,3> requested_size{0.0,0.0,0.0};
        bool circular = false;
        double requested_diameter = 0.0;
    };

    struct WallFace {
        int x = 0, y = 0, z = 0; // lower-coordinate cell
        int axis = 0;             // 0=x, 1=y, 2=z
        double thickness = 0.0;
        double conductivity = 0.0;
        double rho = 0.0;
        double cp = 0.0;
        double temperature = 20.0;
        bool active = true;
        int component_group = -1;
    };

    struct InternalFanInterface {
        std::array<int, 3> upstream;
        std::array<int, 3> downstream;
        double flow_m3s = 0.0;
        std::array<double, 3> direction{0.0, 0.0, 0.0};
        double curve_a = 0.0;
        double curve_b = 0.0;
        double curve_c = 0.0;
        double rho_rated = 1.2;
        double q_ref = 0.0;

        bool has_curve() const { return curve_a > 0.0; }
    };

    Mesh() = default;

    Mesh(int nx_, int ny_, int nz_,
         std::vector<double> dxs_, std::vector<double> dys_, std::vector<double> dzs_,
         Environment env_, Workload load_)
        : nx(nx_),
          ny(ny_),
          nz(nz_),
          dxs(std::move(dxs_)),
          dys(std::move(dys_)),
          dzs(std::move(dzs_)),
          env(env_),
          load(load_)
    {
        if (nx <= 0 || ny <= 0 || nz <= 0 ||
            dxs.size() != static_cast<size_t>(nx) ||
            dys.size() != static_cast<size_t>(ny) ||
            dzs.size() != static_cast<size_t>(nz)) {
            throw std::invalid_argument("Mesh dimensions and spacing vectors must be non-empty and consistent.");
        }
        auto validate_widths = [](const std::vector<double>& widths, const char* axis) {
            for (double width : widths) {
                if (!std::isfinite(width) || width <= 0.0) {
                    throw std::invalid_argument(
                        std::string("Mesh ") + axis + " cell widths must be finite and positive.");
                }
            }
        };
        validate_widths(dxs, "x");
        validate_widths(dys, "y");
        validate_widths(dzs, "z");
        cells.resize(
            static_cast<size_t>(nx) *
            static_cast<size_t>(ny) *
            static_cast<size_t>(nz)
        );
        build_bounds();
        validate_mesh_density();
    }

    void validate_mesh_density() {
        int CELL_COUNT_THRESHOLD = load.get_cell_count_threshold();
        int MEGABYTE_THRESHOLD = load.get_megabyte_threshold();// 4 Mb
        int cell_count = get_cell_count();
        int byte_count = get_memory_byte() * 2;// current and next mesh are both used
        std::cout << "Mesh cell count: " << cell_count << " cells." << std::endl;
        std::cout << "Memory from cell count: " << byte_count << " bytes." << std::endl;
        std::string cell_count_msg = "Mesh: cell count exceeds the max of " + std::to_string(CELL_COUNT_THRESHOLD);
        if(cell_count > CELL_COUNT_THRESHOLD) {
            throw std::invalid_argument(cell_count_msg);
        }
        std::string byte_count_msg = "Mesh: cell memory exceeds the max of " + std::to_string(MEGABYTE_THRESHOLD);
        if(byte_count > MEGABYTE_THRESHOLD) {
            throw std::invalid_argument(byte_count_msg);
        }
    }

    Workload get_load() const {
        return load;
    }

    double get_memory_byte() const {
        const std::size_t bytes = get_cell_count() * sizeof(Cell);
        return static_cast<double>(bytes);
    }

    std::size_t get_cell_count() const {
        return static_cast<std::size_t>(nx)
            * static_cast<std::size_t>(ny)
            * static_cast<std::size_t>(nz);
    }

    const Environment& get_env() const { return env; }
    const std::vector<Cell>& get_cells() const { return cells; }
    const std::vector<double>& get_x_bounds() const { return x_bounds; }
    const std::vector<double>& get_y_bounds() const { return y_bounds; }
    const std::vector<double>& get_z_bounds() const { return z_bounds; }
    bool has_openfoam_export_metadata() const {
        return openfoam_cell_metadata.size() == get_cell_count();
    }
    const std::vector<OpenFoamCellMetadata>&
    get_openfoam_cell_metadata() const {
        if(!has_openfoam_export_metadata()) {
            throw std::logic_error(
                "Mesh: OpenFOAM export metadata has not been enabled.");
        }
        return openfoam_cell_metadata;
    }
    const std::vector<OpenFoamComponentRegion>&
    get_openfoam_component_regions() const {
        return openfoam_component_regions;
    }
    const std::vector<OpenFoamHeatSourceRegion>&
    get_openfoam_heat_source_regions() const {
        return openfoam_heat_source_regions;
    }
    const std::vector<OpenFoamBoundaryPatch>&
    get_openfoam_boundary_patches() const {
        return openfoam_boundary_patches;
    }
    const std::vector<OpenFoamInternalFlowDevice>&
    get_openfoam_internal_flow_devices() const {
        return openfoam_internal_flow_devices;
    }
    int get_openfoam_boundary_patch_id(
        int x, int y, int z, int axis, int side) const {
        if(axis < 0 || axis > 2 || side < 0 || side > 1 ||
           !in_bounds(x,y,z) || openfoam_boundary_patch_ids.empty())
            return -1;
        return openfoam_boundary_patch_ids[
            idx(x,y,z)*6u + static_cast<std::size_t>(axis*2+side)];
    }
    const std::vector<InternalFanInterface>& get_internal_fans() const {
        return internal_fans;
    }
    std::vector<InternalFanInterface>& get_internal_fans() {
        return internal_fans;
    }
    const std::vector<WallFace>& get_wall_faces() const { return wall_faces; }
    std::vector<WallFace>& get_wall_faces() { return wall_faces; }
    bool has_face_walls() const { return !wall_face_refs.empty(); }
    std::size_t get_face_wall_memory_byte() const {
        return wall_face_refs.capacity() * sizeof(int32_t) +
               wall_faces.capacity() * sizeof(WallFace);
    }

    int wall_face_index(int x, int y, int z, int nx_, int ny_, int nz_) const {
        const int dx = nx_ - x, dy = ny_ - y, dz = nz_ - z;
        const int axis = dx != 0 ? 0 : (dy != 0 ? 1 : 2);
        int lx = std::min(x, nx_);
        int ly = std::min(y, ny_);
        int lz = std::min(z, nz_);
        if(!in_bounds(lx, ly, lz) || !in_bounds(
               lx + (axis == 0), ly + (axis == 1), lz + (axis == 2)) ||
           wall_face_refs.empty()) return -1;
        return wall_face_refs[idx(lx, ly, lz) * 3u + static_cast<size_t>(axis)];
    }

    const WallFace* wall_between(int x, int y, int z,
                                 int nx_, int ny_, int nz_) const {
        const int wi = wall_face_index(x, y, z, nx_, ny_, nz_);
        if(wi < 0 || static_cast<size_t>(wi) >= wall_faces.size() ||
           !wall_faces[wi].active) return nullptr;
        return &wall_faces[wi];
    }

    void add_wall_face(int x, int y, int z, int axis, double thickness,
                       double conductivity, double rho, double cp,
                       double temperature, int component_group = -1) {
        const int hx = x + (axis == 0);
        const int hy = y + (axis == 1);
        const int hz = z + (axis == 2);
        if(axis < 0 || axis > 2 || !in_bounds(x, y, z) ||
           !in_bounds(hx, hy, hz)) return;
        if(wall_face_refs.empty())
            wall_face_refs.assign(get_cell_count() * 3u, -1);
        const size_t slot = idx(x, y, z) * 3u + static_cast<size_t>(axis);
        if(wall_face_refs[slot] >= 0) return;
        wall_face_refs[slot] = static_cast<int32_t>(wall_faces.size());
        wall_faces.push_back(
            {x, y, z, axis, thickness, conductivity, rho, cp,
             temperature, true, component_group});
    }

    void open_wall_face(int x, int y, int z, int axis) {
        if(wall_face_refs.empty() || !in_bounds(x, y, z)) return;
        const size_t slot = idx(x, y, z) * 3u + static_cast<size_t>(axis);
        const int wi = wall_face_refs[slot];
        if(wi >= 0) wall_faces[wi].active = false;
        wall_face_refs[slot] = -1;
    }

    double wall_face_area(const WallFace& wall) const {
        if(wall.axis == 0) return get_dy(wall.y) * get_dz(wall.z);
        if(wall.axis == 1) return get_dx(wall.x) * get_dz(wall.z);
        return get_dx(wall.x) * get_dy(wall.y);
    }

    double wall_face_coordinate(const WallFace& wall) const {
        if(wall.axis == 0) return x_bounds[wall.x + 1];
        if(wall.axis == 1) return y_bounds[wall.y + 1];
        return z_bounds[wall.z + 1];
    }

    std::array<double,3> wall_face_center(const WallFace& wall) const {
        std::array<double,3> center{
            cell_center_x(wall.x),
            cell_center_y(wall.y),
            cell_center_z(wall.z)};
        center[wall.axis] = wall_face_coordinate(wall);
        return center;
    }

    size_t idx(int x, int y, int z) const {
        return
            static_cast<size_t>(x) * ny * nz +
            static_cast<size_t>(y) * nz +
            static_cast<size_t>(z);
    }

    bool in_bounds(int x, int y, int z) const {
        return
            x >= 0 && x < nx &&
            y >= 0 && y < ny &&
            z >= 0 && z < nz;
    }

    bool v_in_bounds(int x, int y, int z, std::array<bool,3> wall) const {

        return
            x >= 0.0 && (wall[0] ? x <= nx : x < nx) &&
            y >= 0.0 && (wall[1] ? y <= ny : y < ny) &&
            z >= 0.0 && (wall[2] ? z <= nz : z < nz );
    }

    Cell& at(int x, int y, int z) {
        return cells[idx(x,y,z)];
    }

    const Cell& at(int x, int y, int z) const {
        return cells[idx(x,y,z)];
    }

    void set_cell(int x, int y, int z, const Cell& cell) {
        cells[idx(x,y,z)] = cell;
    }

    int get_nx() const { return nx; }
    int get_ny() const { return ny; }
    int get_nz() const { return nz; }

    // Legacy scalar accessors. Valid as long as the mesh is uniform (true
    // of every caller today). These return exactly the same double that
    // used to live in the flat dx/dy/dz members - Solver/FlowSolver keep
    // working unmodified until Stage 2 migrates them to the per-index
    // overloads below.
    double get_dx() const { return dxs.empty() ? 0.0 : dxs[0]; }
    double get_dy() const { return dys.empty() ? 0.0 : dys[0]; }
    double get_dz() const { return dzs.empty() ? 0.0 : dzs[0]; }

    // Per-index accessors - ready for non-uniform spacing (Stage 3).
    double get_dx(int i) const { return dxs[i]; }
    double get_dy(int j) const { return dys[j]; }
    double get_dz(int k) const { return dzs[k]; }

    double cell_volume() const { return get_dx() * get_dy() * get_dz(); }
    double area_x() const { return get_dy() * get_dz(); }
    double area_y() const { return get_dx() * get_dz(); }
    double area_z() const { return get_dx() * get_dy(); }

    // True while every cell shares the same width on every axis (all of
    // Stage 1's build_mesh() calls, i.e. everything today). Solver and
    // FlowSolver use this to pick between their uniform-mesh kernels and
    // their per-cell/adaptive-mesh kernels, without either caller having
    // to be told explicitly which kind of mesh it was handed.
    bool is_uniform() const {
        if (dxs.empty() || dys.empty() || dzs.empty()) return false;
        for (double v : dxs) if (v != dxs[0]) return false;
        for (double v : dys) if (v != dys[0]) return false;
        for (double v : dzs) if (v != dzs[0]) return false;
        return true;
    }

    // ---------------------------------------------------------------
    // Coordinate -> cell-index lookups for genuinely non-uniform meshes.
    // Deliberately NOT used by the uniform stamp_component/fan/vent below
    // (those keep doing plain floor(x/dx) division, unchanged since Stage
    // 1) - a cumulative-sum boundary array is not bit-exact with direct
    // division at exact cell-boundary coordinates, so this machinery only
    // activates once real non-uniformity exists to justify that tradeoff.
    // See stamp_component_adaptive/stamp_fan_adaptive/stamp_vent_adaptive.
    // ---------------------------------------------------------------

    // Floor-equivalent: which cell contains this coordinate.
    int index_x(double coord) const { return locate_floor(x_bounds, coord); }
    int index_y(double coord) const { return locate_floor(y_bounds, coord); }
    int index_z(double coord) const { return locate_floor(z_bounds, coord); }

    // Ceil-equivalent: one-past the last cell covering up to this coordinate.
    int end_index_x(double coord) const { return locate_ceil(x_bounds, coord); }
    int end_index_y(double coord) const { return locate_ceil(y_bounds, coord); }
    int end_index_z(double coord) const { return locate_ceil(z_bounds, coord); }

    // Center coordinate of cell i/j/k along each axis - the non-uniform
    // generalization of the old "(i+0.5)*dx" formula.
    double cell_center_x(int i) const { return 0.5 * (x_bounds[i] + x_bounds[i+1]); }
    double cell_center_y(int j) const { return 0.5 * (y_bounds[j] + y_bounds[j+1]); }
    double cell_center_z(int k) const { return 0.5 * (z_bounds[k] + z_bounds[k+1]); }

    Mesh build_mesh(const Rack& rack, double dx, double dy, double dz, Environment env, Workload load){
        int nx = std::ceil(rack.get_width_m()  / dx);
        int ny = std::ceil(rack.get_depth_m()  / dy);
        int nz = std::ceil(rack.get_height_m() / dz);

        // Stage 1: still uniform everywhere - every entry is the same
        // scalar. This is the seam a future refinement planner (Stage 3)
        // will plug non-uniform widths into instead, without touching the
        // Mesh/Cell data model at all.
        std::vector<double> dxs(nx, dx), dys(ny, dy), dzs(nz, dz);

        Mesh mesh(nx, ny, nz, dxs, dys, dzs, env, load);
        for(int i=0;i<nx;i++)
        {
            for(int j=0;j<ny;j++)
            {
                for(int k=0;k<nz;k++)
                {
                    mesh.at(i,j,k) = Cell(
                        env.get_T_ambient(),
                        env.get_rho(),
                        env.get_cp(),
                        env.get_k(),
                        0.0,
                        0.0,
                        Cell::State::Air,
                        dxs[i],
                        dys[j],
                        dzs[k],
                        env.get_mu(),
                        env.get_pr()
                    );
                }
            }
        }

        return mesh;
    }

    // Sibling of build_mesh(): same Cell-construction loop, but takes
    // already-planned per-axis width vectors (e.g. from
    // MeshRefinementPlanner) instead of computing nx/ny/nz uniformly from
    // one scalar dx/dy/dz. build_mesh() itself is completely untouched -
    // this is purely additive, a second front door into the same Mesh.
    Mesh build_adaptive_mesh(const Rack& rack,
                              std::vector<double> dxs, std::vector<double> dys, std::vector<double> dzs,
                              Environment env, Workload load) {
        auto validate_extent = [](const std::vector<double>& widths,
                                  double expected,
                                  const char* axis) {
            double actual = 0.0;
            for (double width : widths) actual += width;
            const double tolerance = 1e-9 * std::max(1.0, std::abs(expected));
            if (std::abs(actual - expected) > tolerance) {
                throw std::invalid_argument(
                    std::string("Adaptive mesh ") + axis +
                    " widths must sum to the rack extent.");
            }
        };
        validate_extent(dxs, rack.get_width_m(), "x");
        validate_extent(dys, rack.get_depth_m(), "y");
        validate_extent(dzs, rack.get_height_m(), "z");

        int nx = static_cast<int>(dxs.size());
        int ny = static_cast<int>(dys.size());
        int nz = static_cast<int>(dzs.size());

        Mesh mesh(nx, ny, nz, dxs, dys, dzs, env, load);
        for(int i=0;i<nx;i++)
        {
            for(int j=0;j<ny;j++)
            {
                for(int k=0;k<nz;k++)
                {
                    mesh.at(i,j,k) = Cell(
                        env.get_T_ambient(),
                        env.get_rho(),
                        env.get_cp(),
                        env.get_k(),
                        0.0,
                        0.0,
                        Cell::State::Air,
                        dxs[i],
                        dys[j],
                        dzs[k],
                        env.get_mu(),
                        env.get_pr()
                    );
                }
            }
        }

        return mesh;
    }

    // Additive OpenFOAM stamping path. Existing stamp_component() and
    // stamp_component_adaptive() behavior remains unchanged. Call this
    // sibling instead when the finished mesh will be exported.
    void stamp_component_for_openfoam(const Component& component) {
        enable_openfoam_export_metadata();

        const int component_id =
            static_cast<int>(openfoam_component_regions.size());
        openfoam_component_regions.push_back(
            {component_id,
             component.get_name(),
             component.get_k(),
             component.get_rho(),
             component.get_cp()});

        // OpenFOAM metadata uses boundary-array lookups below. Stamp through
        // the same geometry-aligned path even on a uniform mesh so a decimal
        // coordinate such as 0.15 cannot be rounded below a cell boundary by
        // floor(x/dx) while metadata snaps it to that boundary. Mixing those
        // paths can silently omit an entire solid layer from the exported
        // region. The native-only stamper retains its established dispatch.
        stamp_component_adaptive(component);

        const auto origin = component.get_coords();
        const int i0 = std::max(0, index_x(origin[0]));
        const int j0 = std::max(0, index_y(origin[1]));
        const int k0 = std::max(0, index_z(origin[2]));
        const int i1 = std::min(
            nx, end_index_x(origin[0] + component.get_width_m()));
        const int j1 = std::min(
            ny, end_index_y(origin[1] + component.get_depth_m()));
        const int k1 = std::min(
            nz, end_index_z(origin[2] + component.get_height_m()));

        for(int i = i0; i < i1; ++i) {
            for(int j = j0; j < j1; ++j) {
                for(int k = k0; k < k1; ++k) {
                    if(!at(i,j,k).is_solid()) continue;
                    OpenFoamCellMetadata& metadata =
                        openfoam_cell_metadata[idx(i,j,k)];
                    metadata.region_type =
                        OpenFoamCellMetadata::RegionType::Solid;
                    metadata.component_id = component_id;
                    metadata.material_id = component_id;
                }
            }
        }

        // A homogeneous component may carry its heat load directly on the
        // component instead of defining an internal heat-source region. The
        // native stamper already applies this load; mirror it in OpenFOAM by
        // creating a source mask over the component's solid cells.
        if(std::abs(component.get_watts()) > 1e-12) {
            const int source_id =
                static_cast<int>(openfoam_heat_source_regions.size());
            openfoam_heat_source_regions.push_back(
                {source_id, component_id, component.get_name()+" load",
                 component.get_watts(), false});
            for(int i = i0; i < i1; ++i) {
                for(int j = j0; j < j1; ++j) {
                    for(int k = k0; k < k1; ++k) {
                        if(at(i,j,k).is_solid())
                            openfoam_cell_metadata[idx(i,j,k)]
                                .heat_source_id = source_id;
                    }
                }
            }
        }

        for(const InternalRegion& region : component.get_regions()) {
            if((region.get_region_type() != RegionType::HeatSource &&
                region.get_region_type() != RegionType::Air) ||
               std::abs(region.get_watts()) <= 1e-12)
                continue;
            const int source_id =
                static_cast<int>(openfoam_heat_source_regions.size());
            openfoam_heat_source_regions.push_back(
                {source_id, component_id, region.get_name(),
                 region.get_watts(),
                 region.get_region_type() == RegionType::Air});

            const auto position = region.get_global_position();
            const auto size = region.get_size_m();
            const int si0 = std::max(0, index_x(position[0]));
            const int sj0 = std::max(0, index_y(position[1]));
            const int sk0 = std::max(0, index_z(position[2]));
            const int si1 = std::min(nx, end_index_x(position[0] + size[0]));
            const int sj1 = std::min(ny, end_index_y(position[1] + size[1]));
            const int sk1 = std::min(nz, end_index_z(position[2] + size[2]));
            for(int i = si0; i < si1; ++i) {
                for(int j = sj0; j < sj1; ++j) {
                    for(int k = sk0; k < sk1; ++k) {
                        if(at(i,j,k).is_solid() !=
                           (region.get_region_type() == RegionType::Air))
                            openfoam_cell_metadata[idx(i,j,k)]
                                .heat_source_id = source_id;
                    }
                }
            }
        }

        for(const InternalRegion& region : component.get_regions()) {
            if(region.get_region_type() != RegionType::Fan &&
               region.get_region_type() != RegionType::Vent)
                continue;
            const auto center = region.get_global_position();
            const auto direction = region.get_direction();
            const auto size = region.get_size_m();
            const int normal_axis =
                std::abs(direction[0]) >= std::abs(direction[1]) &&
                std::abs(direction[0]) >= std::abs(direction[2]) ? 0 :
                (std::abs(direction[1]) >= std::abs(direction[2]) ? 1 : 2);
            const double domain_extent =
                normal_axis == 0 ? x_bounds.back() :
                (normal_axis == 1 ? y_bounds.back() : z_bounds.back());
            const double boundary_tolerance =
                1e-9*std::max(1.0,std::abs(domain_extent));
            const bool on_ambient_boundary =
                std::abs(center[normal_axis]) <= boundary_tolerance ||
                std::abs(center[normal_axis]-domain_extent) <=
                    boundary_tolerance;
            if(on_ambient_boundary) {
                const int patch_id =
                    static_cast<int>(openfoam_boundary_patches.size());
                const bool fan =
                    region.get_region_type() == RegionType::Fan;
                OpenFoamBoundaryPatch::Kind kind =
                    OpenFoamBoundaryPatch::Kind::Vent;
                if(fan)
                    kind = region.get_flow_type() == FlowType::Intake
                        ? OpenFoamBoundaryPatch::Kind::Inlet
                        : OpenFoamBoundaryPatch::Kind::Outlet;
                openfoam_boundary_patches.push_back(
                    {patch_id,
                     component.get_name()+"_"+region.get_name(),
                     kind,
                     fan ? 0.0 : region.free_area(),
                     fan ? 0.0 : region.get_cd(),
                     fan && region.has_curve(),
                     region.get_curve_a(),region.get_curve_b(),
                     region.get_curve_c(),region.get_fan_rho_rated(),
                     region.flow_m3s()});
                auto& patch=openfoam_boundary_patches.back();
                patch.requested_center=center;
                patch.requested_size=size;
                patch.circular=region.is_circular();
                patch.requested_diameter=region.get_diameter();
                const auto flow_direction =
                    fan ? region.get_velocity_direction() : direction;
                const std::size_t marked=mark_openfoam_boundary_opening(
                    center,flow_direction,size,region.get_diameter(),
                    region.is_circular(),
                    fan ? Cell::State::Fan : Cell::State::Vent,
                    patch_id);
                if(marked==0)
                    throw std::runtime_error(
                        "OpenFOAM ambient-connected component device '"+
                        component.get_name()+"/"+region.get_name()+
                        "' did not cover an exterior fluid face.");
                populate_openfoam_boundary_source_zone(
                    patch_id,flow_direction);
                continue;
            }
            const int plane_index =
                normal_axis == 0 ? index_x(center[0]) :
                (normal_axis == 1 ? index_y(center[1]) : index_z(center[2]));
            std::vector<std::size_t> selected;
            for(int i=0; i<nx; ++i) for(int j=0; j<ny; ++j)
                for(int k=0; k<nz; ++k) {
                    const int normal_index =
                        normal_axis == 0 ? i : (normal_axis == 1 ? j : k);
                    if(normal_index != plane_index) continue;
                    const std::array<double,3> point{
                        cell_center_x(i),cell_center_y(j),cell_center_z(k)};
                    bool inside = true;
                    for(int axis=0; axis<3; ++axis) {
                        if(axis == normal_axis) continue;
                        const double half =
                            region.is_circular()
                                ? region.get_diameter()/2.0
                                : size[axis]/2.0;
                        inside = inside &&
                            std::abs(point[axis]-center[axis]) <= half+1e-12;
                    }
                    if(region.is_circular()) {
                        double radius_squared = 0.0;
                        for(int axis=0; axis<3; ++axis)
                            if(axis != normal_axis)
                                radius_squared +=
                                    (point[axis]-center[axis])*
                                    (point[axis]-center[axis]);
                        inside = radius_squared <=
                            region.get_diameter()*region.get_diameter()/4.0;
                    }
                    if(inside && at(i,j,k).is_fluid())
                        selected.push_back(idx(i,j,k));
                }
            if(selected.empty())
                throw std::runtime_error(
                    "OpenFOAM internal flow device '" + region.get_name() +
                    "' selected no fluid cells.");
            const double thickness =
                normal_axis == 0
                    ? at(plane_index,0,0).get_dx()
                    : (normal_axis == 1
                        ? at(0,plane_index,0).get_dy()
                        : at(0,0,plane_index).get_dz());
            openfoam_internal_flow_devices.push_back(
                {static_cast<int>(openfoam_internal_flow_devices.size()),
                 component_id,component.get_name(),
                 region.get_name(),
                 region.get_region_type() == RegionType::Fan
                    ? OpenFoamInternalFlowDevice::Kind::Fan
                    : OpenFoamInternalFlowDevice::Kind::Vent,
                 direction,selected,thickness,
                 region.get_region_type() == RegionType::Vent
                    ? region.free_area() : 0.0,
                 region.get_region_type() == RegionType::Vent
                    ? region.get_cd() : 0.0,
                 region.get_curve_a(),region.get_curve_b(),
                 region.get_curve_c(),region.get_fan_rho_rated(),
                 region.flow_m3s(),center,size,region.is_circular(),
                 region.get_diameter()});
        }
    }

    void stamp_fan_for_openfoam(const Fan& fan) {
        enable_openfoam_export_metadata();
        const int patch_id =
            static_cast<int>(openfoam_boundary_patches.size());
        openfoam_boundary_patches.push_back(
            {patch_id, fan.get_name(),
             fan.get_type_t() == FlowType::Intake
                 ? OpenFoamBoundaryPatch::Kind::Inlet
                 : OpenFoamBoundaryPatch::Kind::Outlet,
             0.0, 0.0,
             fan.has_curve(), fan.get_curve_a(), fan.get_curve_b(),
             fan.get_curve_c(), fan.get_rho_rated(), fan.flow_m3s()});
        auto& patch=openfoam_boundary_patches.back();
        patch.requested_center=fan.get_center();
        patch.requested_size=fan.get_size_m();
        patch.circular=fan.get_shape_t()==ShapeType::Circular;
        patch.requested_diameter=fan.get_diameter();

        stamp_fan(fan);
        const std::size_t marked = mark_openfoam_boundary_opening(
            fan.get_center(), fan.direction, fan.get_size_m(),
            fan.get_diameter(),
            fan.get_shape_t() == ShapeType::Circular,
            fan.get_type_t() == FlowType::Intake
                ? Cell::State::Intake
                : Cell::State::Exhaust,
            patch_id);
        if(marked == 0)
            throw std::runtime_error(
                "OpenFOAM fan patch '" + fan.get_name() +
                "' did not cover an exterior mesh face.");
        populate_openfoam_boundary_source_zone(
            patch_id, fan.direction);
    }

    void stamp_vent_for_openfoam(const Vent& vent) {
        enable_openfoam_export_metadata();
        const int patch_id =
            static_cast<int>(openfoam_boundary_patches.size());
        openfoam_boundary_patches.push_back(
            {patch_id, vent.get_name(),
             OpenFoamBoundaryPatch::Kind::Vent,
             vent.free_area(), vent.get_cd()});
        auto& patch=openfoam_boundary_patches.back();
        patch.requested_center=vent.get_center();
        patch.requested_size=vent.get_size_m();
        patch.circular=vent.get_shape()==VentShapeType::Circular;
        patch.requested_diameter=vent.get_diameter();

        stamp_vent(vent);
        const std::size_t marked = mark_openfoam_boundary_opening(
            vent.get_center(), vent.get_direction(), vent.get_size_m(),
            vent.get_diameter(),
            vent.get_shape() == VentShapeType::Circular,
            Cell::State::Vent, patch_id);
        if(marked == 0)
            throw std::runtime_error(
                "OpenFOAM vent patch '" + vent.get_name() +
                "' did not cover an exterior mesh face.");
        populate_openfoam_boundary_source_zone(
            patch_id, vent.get_direction());
    }

    void stamp_component(const Component& c) {
        if (!is_uniform()) { stamp_component_adaptive(c); return; }

        // Stage 1: mesh is still uniform, so these are just the scalar
        // spacing values under a familiar name - every formula below is
        // untouched from before. Once Stage 3 introduces real per-axis
        // variation, this function gets its own index-lookup-based sibling
        // rather than being rewritten in place (see stamp_component_adaptive
        // note in the class docs once that lands).
        const double dx = get_dx();
        const double dy = get_dy();
        const double dz = get_dz();

        auto[x, y, z] = c.get_coords();
        // start coord converted to mesh units
        int mx = static_cast<int>(std::floor(x / dx));
        int my = static_cast<int>(std::floor(y / dy));
        int mz = static_cast<int>(std::floor(z / dz));
        // size of component converted to mesh units
        int cnx = std::ceil(c.get_width_m() / dx);
        int cny = std::ceil(c.get_depth_m() / dy);
        int cnz = std::ceil(c.get_height_m() / dz);

        // for all cells in component space, set to component properties
        for(int i = mx; i < mx + cnx; i++) {
            for(int j = my; j < my + cny; j++) {
                for(int k = mz; k < mz + cnz; k++) {
                    Cell& cell = at(i, j, k);
                    // Overlap is validated at the geometry level (CollisionChecker)
                    // before the mesh is built - trust cell_state here.
                    cell.set_qdot(c.watt_density());
                    cell.set_rho(c.get_rho());
                    cell.set_k(c.get_k());
                    cell.set_cp(c.get_cp());
                    cell.set_h(0.0);
                    cell.set_state(Cell::State::Component);
                    cell.set_T(c.get_t());
                }
            }
        }
        // stamp other regions in order of std::vector<RegionType>
        for(InternalRegion r : c.get_regions()) {
            if(r.get_region_type() == RegionType::Air) {
                auto[air_x, air_y, air_z] = r.get_global_position();
                auto[air_size_x, air_size_y, air_size_z] = r.get_size_m();
                // start coord converted to mesh units
                int air_mx = static_cast<int>(std::floor(air_x / dx));
                int air_my = static_cast<int>(std::floor(air_y / dy));
                int air_mz = static_cast<int>(std::floor(air_z / dz));
                // size of air converted to mesh units
                int air_sx = std::ceil(air_size_x / dx);
                int air_sy = std::ceil(air_size_y / dy);
                int air_sz = std::ceil(air_size_z / dz);
                // stamp all air cells in their size space
                for(int i = air_mx; i < air_mx + air_sx; i++) {
                    for(int j = air_my; j < air_my + air_sy; j++) {
                        for(int k = air_mz; k < air_mz + air_sz; k++) {
                            Cell& cell = at(i, j, k);
                            // collision detection done in component.hpp already
                            if(!in_bounds(i, j, k)) {
                                throw std::runtime_error("Internal air region extends outside mesh");
                            }
                            cell.set_T(env.get_T_ambient());
                            cell.set_rho(env.get_rho());
                            cell.set_cp(env.get_cp());
                            cell.set_k(env.get_k());
                            cell.set_mu(env.get_mu());
                            cell.set_qdot(0.0);
                            cell.set_state(Cell::State::Air);
                        }
                    }
                }

            }
            if(r.get_region_type() == RegionType::HeatSource) {
                auto[hs_x, hs_y, hs_z] = r.get_global_position();
                auto[hs_size_x, hs_size_y, hs_size_z] = r.get_size_m();
                // start coord converted to mesh units
                int hs_mx = static_cast<int>(std::floor(hs_x / dx));
                int hs_my = static_cast<int>(std::floor(hs_y / dy));
                int hs_mz = static_cast<int>(std::floor(hs_z / dz));
                // size of air converted to mesh units
                int hs_sx = std::ceil(hs_size_x / dx);
                int hs_sy = std::ceil(hs_size_y / dy);
                int hs_sz = std::ceil(hs_size_z / dz);

                const int hs_i0 = std::max(hs_mx, mx);
                const int hs_j0 = std::max(hs_my, my);
                const int hs_k0 = std::max(hs_mz, mz);
                const int hs_i1 = std::min({hs_mx + hs_sx, mx + cnx, nx});
                const int hs_j1 = std::min({hs_my + hs_sy, my + cny, ny});
                const int hs_k1 = std::min({hs_mz + hs_sz, mz + cnz, nz});
                const int stamped_nx = std::max(0, hs_i1 - hs_i0);
                const int stamped_ny = std::max(0, hs_j1 - hs_j0);
                const int stamped_nz = std::max(0, hs_k1 - hs_k0);
                const std::size_t stamped_cells =
                    static_cast<std::size_t>(stamped_nx) *
                    static_cast<std::size_t>(stamped_ny) *
                    static_cast<std::size_t>(stamped_nz);
                // Conserve the requested total power after discretization:
                // sum(qdot * cell_volume) over all stamped cells == region watts.
                const double discrete_qdot =
                    r.get_watts() /
                    (static_cast<double>(stamped_cells) * cell_volume());
                // stamp all air cells in their size space
                for(int i = hs_i0; i < hs_i1; ++i) {
                    for(int j = hs_j0; j < hs_j1; ++j) {
                        for(int k = hs_k0; k < hs_k1; ++k) {
                            Cell& cell = at(i, j, k);
                            if(!in_bounds(i, j, k)) {
                                throw std::runtime_error("Internal air region extends outside mesh");
                            }
                            cell.set_T(env.get_T_ambient());
                            cell.set_rho(r.get_rho());
                            cell.set_cp(r.get_cp());
                            cell.set_k(r.get_k());
                            cell.set_mu(0.0);
                            cell.set_h(0.0);
                            cell.set_pr(env.get_pr());
                            cell.set_vx(0.0);
                            cell.set_vy(0.0);
                            cell.set_vz(0.0);
                            cell.set_flow_source(0.0);
                            cell.set_qdot(discrete_qdot);
                            cell.set_state(Cell::State::Component);
                        }
                    }
                }
            }
            if(r.get_region_type() == RegionType::Vent) {
                auto [cx, cy, cz] = r.get_global_position();
                auto [nnx, nny, nnz] = r.get_direction();

                std::vector<std::array<int, 3>> covered;

                double ax = std::abs(nnx);
                double ay = std::abs(nny);
                double az = std::abs(nnz);

                auto stamp_cell = [&](int i, int j, int k, std::array<bool, 3> wall) {
                    if(!v_in_bounds(i, j, k, wall)) return;
                    if(wall[0] && i == nx) i -= 1;
                    if(wall[1] && j == ny) j -= 1;
                    if(wall[2] && k == nz) k -= 1;
                    covered.push_back({i, j, k});
                };

                // lies in the XY plane
                if(az >= ax && az >= ay) {
                    double w = r.get_size_m()[0] / 2.0;
                    double h = r.get_size_m()[1] / 2.0;
                    int k = static_cast<int>(std::floor(cz / dz));
                    int i0 = static_cast<int>(std::floor((cx-w)/dx));
                    int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
                    int j0 = static_cast<int>(std::floor((cy-h)/dy));
                    int j1 = static_cast<int>(std::ceil ((cy+h)/dy));
                    for(int i = i0; i < i1; ++i) {
                        for(int j = j0; j < j1; ++j) {
                            stamp_cell(i, j, k, {false, false, true});
                        }
                    }
                }
                // lies in the XZ plane
                if(ay >= ax && ay >= az) {
                    double w = r.get_size_m()[0] / 2.0;
                    double h = r.get_size_m()[2] / 2.0;
                    int k = static_cast<int>(std::floor(cy / dy));
                    int i0 = static_cast<int>(std::floor((cx-w)/dx));
                    int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
                    int j0 = static_cast<int>(std::floor((cz-h)/dz));
                    int j1 = static_cast<int>(std::ceil ((cz+h)/dz));
                    for(int i = i0; i < i1; ++i) {
                        for(int j = j0; j < j1; ++j) {
                            stamp_cell(i, k, j, {false, true, false});
                        }
                    }
                }
                // lies in the YZ plane
                if(ax >= ay && ax >= az) {
                    double w = r.get_size_m()[1] / 2.0;
                    double h = r.get_size_m()[2] / 2.0;
                    int k = static_cast<int>(std::floor(cx / dx));
                    int i0 = static_cast<int>(std::floor((cy-w)/dy));
                    int i1 = static_cast<int>(std::ceil ((cy+w)/dy));
                    int j0 = static_cast<int>(std::floor((cz-h)/dz));
                    int j1 = static_cast<int>(std::ceil ((cz+h)/dz));
                    for(int i = i0; i < i1; ++i) {
                        for(int j = j0; j < j1; ++j) {
                            stamp_cell(k, i, j, {true, false, false});
                        }
                    }
                }
                // apply vent conductance
                double C_total = r.get_cd() * r.free_area();
                double C_per_cell = covered.empty() ? 0.0 : C_total / covered.size();
                for(auto& [i, j, k] : covered) {
                    Cell& cell = at(i, j, k);
                    // Overlap already validated at the geometry level.
                    cell.set_T(env.get_T_ambient());
                    cell.set_rho(env.get_rho());
                    cell.set_cp(env.get_cp());
                    cell.set_k(env.get_k());
                    cell.set_mu(env.get_mu());
                    cell.set_pr(env.get_pr());
                    cell.set_qdot(0.0);
                    cell.set_h(0.0);
                    cell.set_flow_source(0.0);
                    cell.set_state(Cell::State::Vent);
                    cell.set_vent_conductance(C_per_cell);
                }

            }
            if(r.get_region_type() == RegionType::Fan) {
                auto [cx, cy, cz] = r.get_global_position();
                auto [nnx, nny, nnz] = r.get_velocity_direction();
                bool is_circular = r.is_circular();

                double rad = r.get_diameter() / 2.0;
                std::vector<std::array<int, 3>> covered;

                auto stamp_cell = [&](int i, int j, int k) {
                    if(!in_bounds(i, j, k)) return;
                    covered.push_back({i, j, k});
                };
                double ax = std::abs(nnx);
                double ay = std::abs(nny);
                double az = std::abs(nnz);

                // lies in XY plane
                if(az >= ax && az >= ay) {
                    double w = r.get_size_m()[0] / 2.0;
                    double h = r.get_size_m()[1] / 2.0;

                    int k = static_cast<int>(std::floor(cz / dz));
                    int i0 = static_cast<int>(std::floor((cx-w)/dx));
                    int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
                    int j0 = static_cast<int>(std::floor((cy-h)/dy));
                    int j1 = static_cast<int>(std::ceil ((cy+h)/dy));

                    if(is_circular) {
                        i0 = static_cast<int>(std::floor((cx-rad)/dx));
                        i1 = static_cast<int>(std::ceil ((cx+rad)/dx));
                        j0 = static_cast<int>(std::floor((cy-rad)/dy));
                        j1 = static_cast<int>(std::ceil ((cy+rad)/dy));
                    }

                    for(int i=i0;i<i1;i++) {
                        for(int j=j0;j<j1;j++) {
                            if(!is_circular) {
                                stamp_cell(i, j, k);
                            } else {
                                double xc = (i+0.5)*dx;
                                double yc = (j+0.5)*dy;

                                double dist2 =
                                    (xc-cx)*(xc-cx) +
                                    (yc-cy)*(yc-cy);

                                if(dist2 <= rad*rad)
                                    stamp_cell(i,j,k);
                            }
                        }
                    }
                }
                // lies in XZ plane
                if(ay >= ax && ay >= az) {
                    double w = r.get_size_m()[0] / 2.0;
                    double h = r.get_size_m()[2] / 2.0;

                    int k = static_cast<int>(std::floor(cy / dy));
                    int i0 = static_cast<int>(std::floor((cx-w)/dx));
                    int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
                    int j0 = static_cast<int>(std::floor((cz-h)/dz));
                    int j1 = static_cast<int>(std::ceil ((cz+h)/dz));

                    if(is_circular) {
                        i0 = static_cast<int>(std::floor((cx-rad)/dx));
                        i1 = static_cast<int>(std::ceil ((cx+rad)/dx));
                        j0 = static_cast<int>(std::floor((cz-rad)/dz));
                        j1 = static_cast<int>(std::ceil ((cz+rad)/dz));
                    }

                    for(int i=i0;i<i1;i++) {
                        for(int j=j0;j<j1;j++) {
                            if(!is_circular) {
                                stamp_cell(i, k, j);
                            } else {
                                double xc = (i+0.5)*dx;
                                double zc = (j+0.5)*dz;

                                double dist2 =
                                    (xc-cx)*(xc-cx) +
                                    (zc-cz)*(zc-cz);

                                if(dist2 <= rad*rad)
                                    stamp_cell(i,k,j);
                            }
                        }
                    }
                }
                // lies in YZ plane
                if(ax >= ay && ax >= az) {
                    double w = r.get_size_m()[1] / 2.0;
                    double h = r.get_size_m()[2] / 2.0;

                    int k = static_cast<int>(std::floor(cx / dx));
                    int i0 = static_cast<int>(std::floor((cy-w)/dy));
                    int i1 = static_cast<int>(std::ceil ((cy+w)/dy));
                    int j0 = static_cast<int>(std::floor((cz-h)/dz));
                    int j1 = static_cast<int>(std::ceil ((cz+h)/dz));

                    if(is_circular) {
                        i0 = static_cast<int>(std::floor((cy-rad)/dy));
                        i1 = static_cast<int>(std::ceil ((cy+rad)/dy));
                        j0 = static_cast<int>(std::floor((cz-rad)/dz));
                        j1 = static_cast<int>(std::ceil ((cz+rad)/dz));
                    }

                    for(int i=i0;i<i1;i++) {
                        for(int j=j0;j<j1;j++) {
                            if(!is_circular) {
                                stamp_cell(k, i, j);
                            } else {
                                double yc = (i+0.5)*dy;
                                double zc = (j+0.5)*dz;

                                double dist2 =
                                    (yc-cy)*(yc-cy) +
                                    (zc-cz)*(zc-cz);

                                if(dist2 <= rad*rad)
                                    stamp_cell(k,i,j);
                            }
                        }
                    }
                }
                // apply init velocities
                double Q_total = r.flow_m3s();
                double Q_per_cell = covered.empty() ? 0.0 : Q_total / covered.size();
                const int sx = nnx > 0.0 ? 1 : (nnx < 0.0 ? -1 : 0);
                const int sy = nny > 0.0 ? 1 : (nny < 0.0 ? -1 : 0);
                const int sz = nnz > 0.0 ? 1 : (nnz < 0.0 ? -1 : 0);
                for (auto& [i, j, k] : covered) {
                    Cell& cell = at(i, j, k);
                    cell.set_T(env.get_T_ambient());
                    cell.set_rho(env.get_rho());
                    cell.set_cp(env.get_cp());
                    cell.set_k(env.get_k());
                    cell.set_mu(env.get_mu());
                    cell.set_pr(env.get_pr());
                    cell.set_qdot(0.0);
                    cell.set_h(0.0);
                    cell.set_state(Cell::State::Fan);
                    cell.set_vx(r.velocity_x());
                    cell.set_vy(r.velocity_y());
                    cell.set_vz(r.velocity_z());
                    cell.set_flow_source(0.0);

                    const std::array<int, 3> upstream{i - sx, j - sy, k - sz};
                    const std::array<int, 3> downstream{i, j, k};
                    if(in_bounds(upstream[0], upstream[1], upstream[2])) {
                        if(!at(upstream[0], upstream[1], upstream[2]).is_fluid()) {
                            throw std::runtime_error(
                                "Internal fan '" + r.get_name() +
                                "' has no fluid cell immediately upstream. "
                                "Add an internal air region that reaches the fan face.");
                        }
                        internal_fans.push_back(
                            {upstream, downstream, Q_per_cell, {nnx, nny, nnz},
                             r.get_curve_a(),
                             r.get_curve_b() * covered.size(),
                             r.get_curve_c() * covered.size() * covered.size(),
                             r.get_fan_rho_rated(),
                             Q_per_cell});
                    }
                }
            }
        }
        conserve_internal_heat_source_power(c);
    }

    // Same structure as stamp_component(), field for field, region for
    // region - only the coordinate math changed: floor(v/dx)-style lookups
    // became index_x/y/z(), ceil(v/dx) became end_index_x/y/z(), and the
    // "(i+0.5)*dx" circular-fan/vent cell-center formula became
    // cell_center_x/y/z(). The one genuine behavior difference is the
    // heat-source power conservation: the original divided by
    // (stamped_cells * cell_volume()), which assumed every stamped cell has
    // the same volume. That assumption doesn't hold on a non-uniform mesh,
    // so this sums the actual per-cell volumes instead - watts still
    // conserve exactly, just cell-by-cell rather than via a single
    // mesh-wide volume constant.
    void stamp_component_adaptive(const Component& c) {
        auto[x, y, z] = c.get_coords();
        int mx = index_x(x);
        int my = index_y(y);
        int mz = index_z(z);
        int mx1 = end_index_x(x + c.get_width_m());
        int my1 = end_index_y(y + c.get_depth_m());
        int mz1 = end_index_z(z + c.get_height_m());

        for(int i = mx; i < mx1; i++) {
            for(int j = my; j < my1; j++) {
                for(int k = mz; k < mz1; k++) {
                    Cell& cell = at(i, j, k);
                    // Overlap is validated at the geometry level (CollisionChecker)
                    // before the mesh is built - trust cell_state here.
                    cell.set_qdot(c.watt_density());
                    cell.set_rho(c.get_rho());
                    cell.set_k(c.get_k());
                    cell.set_cp(c.get_cp());
                    cell.set_h(0.0);
                    cell.set_state(Cell::State::Component);
                    cell.set_T(c.get_t());
                }
            }
        }
        // stamp other regions in order of std::vector<RegionType>
        for(InternalRegion r : c.get_regions()) {
            if(r.get_region_type() == RegionType::Air) {
                auto[air_x, air_y, air_z] = r.get_global_position();
                auto[air_size_x, air_size_y, air_size_z] = r.get_size_m();
                int air_mx = index_x(air_x);
                int air_my = index_y(air_y);
                int air_mz = index_z(air_z);
                int air_mx1 = end_index_x(air_x + air_size_x);
                int air_my1 = end_index_y(air_y + air_size_y);
                int air_mz1 = end_index_z(air_z + air_size_z);
                for(int i = air_mx; i < air_mx1; i++) {
                    for(int j = air_my; j < air_my1; j++) {
                        for(int k = air_mz; k < air_mz1; k++) {
                            Cell& cell = at(i, j, k);
                            // collision detection done in component.hpp already
                            if(!in_bounds(i, j, k)) {
                                throw std::runtime_error("Internal air region extends outside mesh");
                            }
                            cell.set_T(env.get_T_ambient());
                            cell.set_rho(env.get_rho());
                            cell.set_cp(env.get_cp());
                            cell.set_k(env.get_k());
                            cell.set_mu(env.get_mu());
                            cell.set_qdot(0.0);
                            cell.set_state(Cell::State::Air);
                        }
                    }
                }

            }
            if(r.get_region_type() == RegionType::HeatSource) {
                auto[hs_x, hs_y, hs_z] = r.get_global_position();
                auto[hs_size_x, hs_size_y, hs_size_z] = r.get_size_m();
                int hs_mx = index_x(hs_x);
                int hs_my = index_y(hs_y);
                int hs_mz = index_z(hs_z);
                int hs_mx1 = end_index_x(hs_x + hs_size_x);
                int hs_my1 = end_index_y(hs_y + hs_size_y);
                int hs_mz1 = end_index_z(hs_z + hs_size_z);

                const int hs_i0 = std::max(hs_mx, mx);
                const int hs_j0 = std::max(hs_my, my);
                const int hs_k0 = std::max(hs_mz, mz);
                const int hs_i1 = std::min({hs_mx1, mx1, nx});
                const int hs_j1 = std::min({hs_my1, my1, ny});
                const int hs_k1 = std::min({hs_mz1, mz1, nz});

                // Conserve the requested total power after discretization:
                // sum(qdot * cell.volume()) over all stamped cells == region
                // watts. Sum actual per-cell volumes (they can differ on a
                // non-uniform mesh) rather than assuming stamped_cells *
                // one shared volume.
                double total_volume = 0.0;
                for(int i = hs_i0; i < hs_i1; ++i)
                    for(int j = hs_j0; j < hs_j1; ++j)
                        for(int k = hs_k0; k < hs_k1; ++k)
                            total_volume += at(i, j, k).volume();

                const double discrete_qdot =
                    total_volume > 0.0 ? r.get_watts() / total_volume : 0.0;

                for(int i = hs_i0; i < hs_i1; ++i) {
                    for(int j = hs_j0; j < hs_j1; ++j) {
                        for(int k = hs_k0; k < hs_k1; ++k) {
                            Cell& cell = at(i, j, k);
                            if(!in_bounds(i, j, k)) {
                                throw std::runtime_error("Internal air region extends outside mesh");
                            }
                            cell.set_T(env.get_T_ambient());
                            cell.set_rho(r.get_rho());
                            cell.set_cp(r.get_cp());
                            cell.set_k(r.get_k());
                            cell.set_mu(0.0);
                            cell.set_h(0.0);
                            cell.set_pr(env.get_pr());
                            cell.set_vx(0.0);
                            cell.set_vy(0.0);
                            cell.set_vz(0.0);
                            cell.set_flow_source(0.0);
                            cell.set_qdot(discrete_qdot);
                            cell.set_state(Cell::State::Component);
                        }
                    }
                }
            }
            if(r.get_region_type() == RegionType::Vent) {
                auto [cx, cy, cz] = r.get_global_position();
                auto [nnx, nny, nnz] = r.get_direction();

                std::vector<std::array<int, 3>> covered;

                double ax = std::abs(nnx);
                double ay = std::abs(nny);
                double az = std::abs(nnz);

                auto stamp_cell = [&](int i, int j, int k, std::array<bool, 3> wall) {
                    if(!v_in_bounds(i, j, k, wall)) return;
                    if(wall[0] && i == nx) i -= 1;
                    if(wall[1] && j == ny) j -= 1;
                    if(wall[2] && k == nz) k -= 1;
                    covered.push_back({i, j, k});
                };

                // lies in the XY plane
                if(az >= ax && az >= ay) {
                    double w = r.get_size_m()[0] / 2.0;
                    double h = r.get_size_m()[1] / 2.0;
                    int k = index_z(cz);
                    int i0 = index_x(cx-w);
                    int i1 = end_index_x(cx+w);
                    int j0 = index_y(cy-h);
                    int j1 = end_index_y(cy+h);
                    for(int i = i0; i < i1; ++i) {
                        for(int j = j0; j < j1; ++j) {
                            stamp_cell(i, j, k, {false, false, true});
                        }
                    }
                }
                // lies in the XZ plane
                if(ay >= ax && ay >= az) {
                    double w = r.get_size_m()[0] / 2.0;
                    double h = r.get_size_m()[2] / 2.0;
                    int k = index_y(cy);
                    int i0 = index_x(cx-w);
                    int i1 = end_index_x(cx+w);
                    int j0 = index_z(cz-h);
                    int j1 = end_index_z(cz+h);
                    for(int i = i0; i < i1; ++i) {
                        for(int j = j0; j < j1; ++j) {
                            stamp_cell(i, k, j, {false, true, false});
                        }
                    }
                }
                // lies in the YZ plane
                if(ax >= ay && ax >= az) {
                    double w = r.get_size_m()[1] / 2.0;
                    double h = r.get_size_m()[2] / 2.0;
                    int k = index_x(cx);
                    int i0 = index_y(cy-w);
                    int i1 = end_index_y(cy+w);
                    int j0 = index_z(cz-h);
                    int j1 = end_index_z(cz+h);
                    for(int i = i0; i < i1; ++i) {
                        for(int j = j0; j < j1; ++j) {
                            stamp_cell(k, i, j, {true, false, false});
                        }
                    }
                }
                // apply vent conductance
                double C_total = r.get_cd() * r.free_area();
                double C_per_cell = covered.empty() ? 0.0 : C_total / covered.size();
                for(auto& [i, j, k] : covered) {
                    Cell& cell = at(i, j, k);
                    // Overlap already validated at the geometry level.
                    cell.set_T(env.get_T_ambient());
                    cell.set_rho(env.get_rho());
                    cell.set_cp(env.get_cp());
                    cell.set_k(env.get_k());
                    cell.set_mu(env.get_mu());
                    cell.set_pr(env.get_pr());
                    cell.set_qdot(0.0);
                    cell.set_h(0.0);
                    cell.set_flow_source(0.0);
                    cell.set_state(Cell::State::Vent);
                    cell.set_vent_conductance(C_per_cell);

                    // A component vent represents an opening through the
                    // chassis, not a one-cell surface coating. Carve the
                    // remaining wall cells along the inward normal until the
                    // already-stamped internal air cavity is reached. Leaving
                    // those cells solid both blocks the flow physically and
                    // creates one-cell solid skins that fail OpenFOAM's cell
                    // determinant/connectivity check.
                    const double component_center[3]{
                        x+0.5*c.get_width_m(),
                        y+0.5*c.get_depth_m(),
                        z+0.5*c.get_height_m()};
                    const double vent_center[3]{cx,cy,cz};
                    const int normal_axis =
                        ay >= ax && ay >= az ? 1 : (az >= ax ? 2 : 0);
                    const int inward_step =
                        component_center[normal_axis] >=
                                vent_center[normal_axis]
                            ? 1 : -1;
                    int cursor[3]{i,j,k};
                    while(true) {
                        cursor[normal_axis] += inward_step;
                        if(cursor[0] < mx || cursor[0] >= mx1 ||
                           cursor[1] < my || cursor[1] >= my1 ||
                           cursor[2] < mz || cursor[2] >= mz1)
                            break;
                        Cell& wall_cell=at(cursor[0],cursor[1],cursor[2]);
                        if(wall_cell.is_fluid()) break;
                        wall_cell.set_T(env.get_T_ambient());
                        wall_cell.set_rho(env.get_rho());
                        wall_cell.set_cp(env.get_cp());
                        wall_cell.set_k(env.get_k());
                        wall_cell.set_mu(env.get_mu());
                        wall_cell.set_pr(env.get_pr());
                        wall_cell.set_qdot(0.0);
                        wall_cell.set_h(0.0);
                        wall_cell.set_flow_source(0.0);
                        wall_cell.set_state(Cell::State::Air);
                    }
                }

            }
            if(r.get_region_type() == RegionType::Fan) {
                auto [cx, cy, cz] = r.get_global_position();
                auto [nnx, nny, nnz] = r.get_velocity_direction();
                bool is_circular = r.is_circular();

                double rad = r.get_diameter() / 2.0;
                std::vector<std::array<int, 3>> covered;

                auto stamp_cell = [&](int i, int j, int k) {
                    if(!in_bounds(i, j, k)) return;
                    covered.push_back({i, j, k});
                };
                double ax = std::abs(nnx);
                double ay = std::abs(nny);
                double az = std::abs(nnz);

                // lies in XY plane
                if(az >= ax && az >= ay) {
                    double w = r.get_size_m()[0] / 2.0;
                    double h = r.get_size_m()[1] / 2.0;

                    int k = index_z(cz);
                    int i0 = index_x(cx-w);
                    int i1 = end_index_x(cx+w);
                    int j0 = index_y(cy-h);
                    int j1 = end_index_y(cy+h);

                    if(is_circular) {
                        i0 = index_x(cx-rad);
                        i1 = end_index_x(cx+rad);
                        j0 = index_y(cy-rad);
                        j1 = end_index_y(cy+rad);
                    }

                    for(int i=i0;i<i1;i++) {
                        for(int j=j0;j<j1;j++) {
                            if(!is_circular) {
                                stamp_cell(i, j, k);
                            } else {
                                double xc = cell_center_x(i);
                                double yc = cell_center_y(j);

                                double dist2 =
                                    (xc-cx)*(xc-cx) +
                                    (yc-cy)*(yc-cy);

                                if(dist2 <= rad*rad)
                                    stamp_cell(i,j,k);
                            }
                        }
                    }
                }
                // lies in XZ plane
                if(ay >= ax && ay >= az) {
                    double w = r.get_size_m()[0] / 2.0;
                    double h = r.get_size_m()[2] / 2.0;

                    int k = index_y(cy);
                    int i0 = index_x(cx-w);
                    int i1 = end_index_x(cx+w);
                    int j0 = index_z(cz-h);
                    int j1 = end_index_z(cz+h);

                    if(is_circular) {
                        i0 = index_x(cx-rad);
                        i1 = end_index_x(cx+rad);
                        j0 = index_z(cz-rad);
                        j1 = end_index_z(cz+rad);
                    }

                    for(int i=i0;i<i1;i++) {
                        for(int j=j0;j<j1;j++) {
                            if(!is_circular) {
                                stamp_cell(i, k, j);
                            } else {
                                double xc = cell_center_x(i);
                                double zc = cell_center_z(j);

                                double dist2 =
                                    (xc-cx)*(xc-cx) +
                                    (zc-cz)*(zc-cz);

                                if(dist2 <= rad*rad)
                                    stamp_cell(i,k,j);
                            }
                        }
                    }
                }
                // lies in YZ plane
                if(ax >= ay && ax >= az) {
                    double w = r.get_size_m()[1] / 2.0;
                    double h = r.get_size_m()[2] / 2.0;

                    int k = index_x(cx);
                    int i0 = index_y(cy-w);
                    int i1 = end_index_y(cy+w);
                    int j0 = index_z(cz-h);
                    int j1 = end_index_z(cz+h);

                    if(is_circular) {
                        i0 = index_y(cy-rad);
                        i1 = end_index_y(cy+rad);
                        j0 = index_z(cz-rad);
                        j1 = end_index_z(cz+rad);
                    }

                    for(int i=i0;i<i1;i++) {
                        for(int j=j0;j<j1;j++) {
                            if(!is_circular) {
                                stamp_cell(k, i, j);
                            } else {
                                double yc = cell_center_y(i);
                                double zc = cell_center_z(j);

                                double dist2 =
                                    (yc-cy)*(yc-cy) +
                                    (zc-cz)*(zc-cz);

                                if(dist2 <= rad*rad)
                                    stamp_cell(k,i,j);
                            }
                        }
                    }
                }
                // apply init velocities
                double Q_total = r.flow_m3s();
                double Q_per_cell = covered.empty() ? 0.0 : Q_total / covered.size();
                const int sx = nnx > 0.0 ? 1 : (nnx < 0.0 ? -1 : 0);
                const int sy = nny > 0.0 ? 1 : (nny < 0.0 ? -1 : 0);
                const int sz = nnz > 0.0 ? 1 : (nnz < 0.0 ? -1 : 0);
                for (auto& [i, j, k] : covered) {
                    Cell& cell = at(i, j, k);
                    // A component-owned fan is an internal transfer device,
                    // not an exchange with ambient. The fan cell represents
                    // the downstream side of the interface; the immediately
                    // upstream cell receives an equal and opposite source.
                    cell.set_T(env.get_T_ambient());
                    cell.set_rho(env.get_rho());
                    cell.set_cp(env.get_cp());
                    cell.set_k(env.get_k());
                    cell.set_mu(env.get_mu());
                    cell.set_pr(env.get_pr());
                    cell.set_qdot(0.0);
                    cell.set_h(0.0);
                    cell.set_state(Cell::State::Fan);
                    cell.set_vx(r.velocity_x());
                    cell.set_vy(r.velocity_y());
                    cell.set_vz(r.velocity_z());
                    cell.set_flow_source(0.0);

                    const std::array<int, 3> upstream{i - sx, j - sy, k - sz};
                    const std::array<int, 3> downstream{i, j, k};
                    if(in_bounds(upstream[0], upstream[1], upstream[2])) {
                        if(!at(upstream[0], upstream[1], upstream[2]).is_fluid()) {
                            const Cell& upstream_cell =
                                at(upstream[0], upstream[1], upstream[2]);
                            throw std::runtime_error(
                                "Component '" + c.get_name() +
                                "' internal fan '" + r.get_name() +
                                "' has no fluid cell immediately upstream at index (" +
                                std::to_string(upstream[0]) + ", " +
                                std::to_string(upstream[1]) + ", " +
                                std::to_string(upstream[2]) + "), center (" +
                                std::to_string(cell_center_x(upstream[0])) + ", " +
                                std::to_string(cell_center_y(upstream[1])) + ", " +
                                std::to_string(cell_center_z(upstream[2])) +
                                ") m, state " +
                                std::to_string(static_cast<int>(upstream_cell.get_state())) +
                                ". Add an internal air region that reaches the fan face."
                            );
                        }
                        internal_fans.push_back(
                            {upstream, downstream, Q_per_cell, {nnx, nny, nnz},
                             r.get_curve_a(),
                             r.get_curve_b() * covered.size(),
                             r.get_curve_c() * covered.size() * covered.size(),
                             r.get_fan_rho_rated(),
                             Q_per_cell});
                    }
                }
            }
        }
        conserve_internal_heat_source_power(c);
    }

    void conserve_internal_heat_source_power(const Component& component) {
        for(const InternalRegion& region : component.get_regions()) {
            if(region.get_region_type() != RegionType::HeatSource &&
               region.get_region_type() != RegionType::Air)
                continue;
            const auto position=region.get_global_position();
            const auto size=region.get_size_m();
            const int i0=index_x(position[0]);
            const int j0=index_y(position[1]);
            const int k0=index_z(position[2]);
            const int i1=end_index_x(position[0]+size[0]);
            const int j1=end_index_y(position[1]+size[1]);
            const int k1=end_index_z(position[2]+size[2]);
            double selected_volume=0.0;
            for(int i=i0;i<i1;++i) for(int j=j0;j<j1;++j)
                for(int k=k0;k<k1;++k)
                    if(at(i,j,k).is_solid() ==
                       (region.get_region_type()==RegionType::HeatSource))
                        selected_volume += at(i,j,k).volume();
            if(region.get_watts()>0.0 && selected_volume<=0.0) {
                throw std::runtime_error(
                    "Internal heat source '" + region.get_name() +
                    "' has no remaining " +
                    (region.get_region_type()==RegionType::HeatSource
                         ? std::string("solid") : std::string("fluid")) +
                    " cells after fan/vent stamping.");
            }
            const double qdot=selected_volume>0.0
                ? region.get_watts()/selected_volume : 0.0;
            for(int i=i0;i<i1;++i) for(int j=j0;j<j1;++j)
                for(int k=k0;k<k1;++k)
                    if(at(i,j,k).is_solid() ==
                       (region.get_region_type()==RegionType::HeatSource))
                        at(i,j,k).set_qdot(qdot);
        }
    }

    // Coarse-grid enclosure model: retain explicit internal heat-source,
    // fan, and vent stamps, but replace non-generating enclosure volume
    // with thermally massive, airflow-blocking faces.
    void stamp_component_face_walls_adaptive(const Component& c) {
        const int component_group = next_wall_component_group++;
        if(std::abs(c.get_watts()) > 1e-12) {
            throw std::invalid_argument(
                "Coarse face-wall mode requires component '" + c.get_name() +
                "' watts = 0. Move its power into explicit heat_source regions.");
        }

        const auto regions = c.get_regions();
        const InternalRegion* air = nullptr;
        double largest_air_volume = -1.0;
        for(const InternalRegion& r : regions) {
            if(r.get_region_type() != RegionType::Air) continue;
            const auto s = r.get_size_m();
            const double volume = s[0] * s[1] * s[2];
            if(volume > largest_air_volume) {
                largest_air_volume = volume;
                air = &r;
            }
        }
        if(air == nullptr) {
            throw std::invalid_argument(
                "Coarse face-wall component '" + c.get_name() +
                "' requires an internal air region.");
        }

        stamp_component_adaptive(c);

        const auto origin = c.get_coords();
        const std::array<double,3> outer{
            c.get_width_m(), c.get_depth_m(), c.get_height_m()};
        const auto inner_origin = air->get_global_position();
        const auto inner_size = air->get_size_m();
        std::array<double,6> thickness{
            inner_origin[0] - origin[0],
            origin[0] + outer[0] - (inner_origin[0] + inner_size[0]),
            inner_origin[1] - origin[1],
            origin[1] + outer[1] - (inner_origin[1] + inner_size[1]),
            inner_origin[2] - origin[2],
            origin[2] + outer[2] - (inner_origin[2] + inner_size[2])};
        for(double& value : thickness) {
            if(value <= 0.0) value = 1e-6;
        }

        auto nearest_boundary = [](const std::vector<double>& bounds,
                                   double coordinate) {
            auto upper = std::lower_bound(
                bounds.begin(),bounds.end(),coordinate);
            if(upper == bounds.begin()) return 0;
            if(upper == bounds.end())
                return static_cast<int>(bounds.size())-1;
            const int hi = static_cast<int>(upper-bounds.begin());
            const int lo = hi-1;
            return std::abs(bounds[hi]-coordinate) <
                   std::abs(bounds[lo]-coordinate) ? hi : lo;
        };
        const int i0 = nearest_boundary(x_bounds,origin[0]);
        const int j0 = nearest_boundary(y_bounds,origin[1]);
        const int k0 = nearest_boundary(z_bounds,origin[2]);
        const int i1 = std::max(i0+1,nearest_boundary(
            x_bounds,origin[0]+outer[0]));
        const int j1 = std::max(j0+1,nearest_boundary(
            y_bounds,origin[1]+outer[1]));
        const int k1 = std::max(k0+1,nearest_boundary(
            z_bounds,origin[2]+outer[2]));

        // Remove only the zero-power enclosure volume. Explicit heat-source
        // regions have nonzero qdot and remain solid.
        const int stamped_i0=index_x(origin[0]);
        const int stamped_j0=index_y(origin[1]);
        const int stamped_k0=index_z(origin[2]);
        const int stamped_i1=end_index_x(origin[0]+outer[0]);
        const int stamped_j1=end_index_y(origin[1]+outer[1]);
        const int stamped_k1=end_index_z(origin[2]+outer[2]);
        for(int i=stamped_i0; i<stamped_i1; ++i)
            for(int j=stamped_j0; j<stamped_j1; ++j)
            for(int k=stamped_k0; k<stamped_k1; ++k) {
                Cell& cell = at(i,j,k);
                if(cell.get_state() == Cell::State::Component &&
                   std::abs(cell.get_qdot()) <= 1e-15) {
                    cell = Cell(env.get_T_ambient(), env.get_rho(), env.get_cp(),
                                env.get_k(), 0.0, 0.0, Cell::State::Air,
                                get_dx(i), get_dy(j), get_dz(k),
                                env.get_mu(), env.get_pr());
                }
            }

        const size_t component_wall_begin = wall_faces.size();
        double max_snap_distance = 0.0;
        auto add_plane = [&](int axis, int low, double t, double intended) {
            if(low >= 0) {
                const double snapped =
                    axis == 0 ? x_bounds[low+1] :
                    axis == 1 ? y_bounds[low+1] : z_bounds[low+1];
                max_snap_distance =
                    std::max(max_snap_distance,std::abs(snapped-intended));
            }
            if(axis == 0)
                for(int j=j0; j<j1; ++j) for(int k=k0; k<k1; ++k)
                    add_wall_face(low,j,k,axis,t,c.get_k(),c.get_rho(),c.get_cp(),c.get_t(),component_group);
            else if(axis == 1)
                for(int i=i0; i<i1; ++i) for(int k=k0; k<k1; ++k)
                    add_wall_face(i,low,k,axis,t,c.get_k(),c.get_rho(),c.get_cp(),c.get_t(),component_group);
            else
                for(int i=i0; i<i1; ++i) for(int j=j0; j<j1; ++j)
                    add_wall_face(i,j,low,axis,t,c.get_k(),c.get_rho(),c.get_cp(),c.get_t(),component_group);
        };
        add_plane(0,i0-1,thickness[0],origin[0]);
        add_plane(0,i1-1,thickness[1],origin[0]+outer[0]);
        add_plane(1,j0-1,thickness[2],origin[1]);
        add_plane(1,j1-1,thickness[3],origin[1]+outer[1]);
        add_plane(2,k0-1,thickness[4],origin[2]);
        add_plane(2,k1-1,thickness[5],origin[2]+outer[2]);
        std::cout << "Coarse face-wall snap '" << c.get_name()
                  << "': max displacement = "
                  << max_snap_distance << " m\n";

        // Fans and vents are openings through an otherwise blocked face.
        for(const InternalRegion& r : regions) {
            if(r.get_region_type() != RegionType::Fan &&
               r.get_region_type() != RegionType::Vent) continue;
            const auto p = r.get_global_position();
            const auto d = r.get_direction();
            const auto s = r.get_size_m();
            const double ax=std::abs(d[0]), ay=std::abs(d[1]), az=std::abs(d[2]);
            const int axis = ax>=ay && ax>=az ? 0 : (ay>=az ? 1 : 2);
            const double radius = r.get_diameter()/2.0;
            double nearest_distance = std::numeric_limits<double>::infinity();
            double nearest_plane = 0.0;
            for(size_t wi=component_wall_begin; wi<wall_faces.size(); ++wi) {
                const WallFace& wall = wall_faces[wi];
                if(!wall.active || wall.axis != axis) continue;
                const double plane = wall_face_coordinate(wall);
                const double distance = std::abs(plane-p[axis]);
                if(distance < nearest_distance) {
                    nearest_distance = distance;
                    nearest_plane = plane;
                }
            }
            for(size_t wi=component_wall_begin; wi<wall_faces.size(); ++wi) {
                WallFace& wall = wall_faces[wi];
                if(!wall.active || wall.axis != axis ||
                   std::abs(wall_face_coordinate(wall)-nearest_plane) > 1e-8) continue;
                const double center[3]{
                    cell_center_x(wall.x), cell_center_y(wall.y), cell_center_z(wall.z)};
                const int a0 = axis==0 ? 1 : 0;
                const int a1 = axis==2 ? 1 : 2;
                bool inside = false;
                if(r.is_circular()) {
                    const double du=center[a0]-p[a0], dv=center[a1]-p[a1];
                    inside = du*du+dv*dv <= radius*radius;
                } else {
                    inside = std::abs(center[a0]-p[a0]) <= s[a0]/2.0 &&
                             std::abs(center[a1]-p[a1]) <= s[a1]/2.0;
                }
                if(inside) open_wall_face(wall.x,wall.y,wall.z,wall.axis);
            }
        }
    }

    void stamp_fan(const Fan& f) {
        if (!is_uniform()) { stamp_fan_adaptive(f); return; }

        const double dx = get_dx();
        const double dy = get_dy();
        const double dz = get_dz();

        auto [cx, cy, cz] = f.get_center();
        auto [nnx, nny, nnz] = f.get_velocity_dir();
        bool is_circular = f.is_circular();

        double r = f.get_diameter() / 2.0;
        std::vector<std::array<int, 3>> covered;

        auto stamp_cell = [&](int i, int j, int k) {
            // A device centered on a positive domain face maps to the
            // one-past-end grid line. Stamp its adjacent interior cell,
            // matching the existing exterior-vent behavior.
            if(i == nx) --i;
            if(j == ny) --j;
            if(k == nz) --k;
            if(!in_bounds(i, j, k)) return;
            covered.push_back({i, j, k});
        };

        double ax = std::abs(nnx);
        double ay = std::abs(nny);
        double az = std::abs(nnz);

        //====================================================
        // Fan normal in +Z/-Z (disk lies in XY plane)
        //====================================================
        if(az >= ax && az >= ay) {
            double w = f.get_size_m()[0] / 2.0;
            double h = f.get_size_m()[1] / 2.0;

            int k = static_cast<int>(std::floor(cz / dz));
            int i0 = static_cast<int>(std::floor((cx-w)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
            int j0 = static_cast<int>(std::floor((cy-h)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+h)/dy));

            if(is_circular) {
                i0 = static_cast<int>(std::floor((cx-r)/dx));
                i1 = static_cast<int>(std::ceil ((cx+r)/dx));
                j0 = static_cast<int>(std::floor((cy-r)/dy));
                j1 = static_cast<int>(std::ceil ((cy+r)/dy));
            }

            for(int i=i0;i<i1;i++) {
                for(int j=j0;j<j1;j++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double xc = (i+0.5)*dx;
                        double yc = (j+0.5)*dy;

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (yc-cy)*(yc-cy);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
        }

        //====================================================
        // Fan normal in +Y/-Y (disk lies in XZ plane)
        //====================================================
        else if(ay >= ax && ay >= az) {
            double w = f.get_size_m()[0] / 2.0;
            double h = f.get_size_m()[2] / 2.0;

            int j = static_cast<int>(std::floor(cy / dy));
            int i0 = static_cast<int>(std::floor((cx-w)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
            int k0 = static_cast<int>(std::floor((cz-h)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+h)/dz));

            if(is_circular) {
                i0 = static_cast<int>(std::floor((cx-r)/dx));
                i1 = static_cast<int>(std::ceil ((cx+r)/dx));
                k0 = static_cast<int>(std::floor((cz-r)/dz));
                k1 = static_cast<int>(std::ceil ((cz+r)/dz));
            }

            for(int i=i0;i<i1;i++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double xc = (i+0.5)*dx;
                        double zc = (k+0.5)*dz;

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
        }

        //====================================================
        // Fan normal in +X/-X (disk lies in YZ plane)
        //====================================================
        else {
            double w = f.get_size_m()[1] / 2.0;
            double h = f.get_size_m()[2] / 2.0;

            int i = static_cast<int>(std::floor(cx / dx));
            int j0 = static_cast<int>(std::floor((cy-w)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+w)/dy));
            int k0 = static_cast<int>(std::floor((cz-h)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+h)/dz));

            if(is_circular) {
                j0 = static_cast<int>(std::floor((cy-r)/dy));
                j1 = static_cast<int>(std::ceil ((cy+r)/dy));
                k0 = static_cast<int>(std::floor((cz-r)/dz));
                k1 = static_cast<int>(std::ceil ((cz+r)/dz));
            }

            for(int j=j0;j<j1;j++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double yc = (j+0.5)*dy;
                        double zc = (k+0.5)*dz;

                        double dist2 =
                            (yc-cy)*(yc-cy) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
        }

        // apply init velocites
        double Q_total = f.flow_m3s();
        double Q_per_cell = covered.empty() ? 0.0 : Q_total / covered.size();
        double sign = (f.get_type_t() == FlowType::Intake) ? +1.0 : -1.0;
        double area_per_cell = covered.empty() ? 0.0 : f.area() / covered.size();
        for (auto& [i, j,k ] : covered) {
            Cell& cell = at(i, j, k);
            // Overlap already validated at the geometry level.
            cell.set_state(
                f.get_type_t() == FlowType::Intake ?
                Cell::State::Intake :
                Cell::State::Exhaust
            );
            cell.set_vx(f.velocity_x());
            cell.set_vy(f.velocity_y());
            cell.set_vz(f.velocity_z());
            if (f.has_curve()) {
                // Curve-driven fan: FlowSolver derives flow from the network's
                // backpressure each outer iteration. Do NOT set flow_source --
                // that path is mutually exclusive with the curve network element.
                cell.set_fan_curve(f.curve_a, f.curve_b, f.curve_c, f.rho_rated);
                cell.set_fan_Q_ref(Q_per_cell);   // free-air CFM as initial guess
                cell.set_fan_dir(f.get_velocity_dir()[0], f.get_velocity_dir()[1], f.get_velocity_dir()[2]);
                cell.set_fan_area(area_per_cell);
                cell.set_flow_source(0.0);
            } else {
                // Old behavior, unchanged.
                cell.set_flow_source(sign * Q_per_cell);
            }
        }
    }

    // Same structure as stamp_fan(): floor(v/dx) -> index_x/y/z(),
    // ceil(v/dx) -> end_index_x/y/z(), "(i+0.5)*dx" -> cell_center_x/y/z().
    void stamp_fan_adaptive(const Fan& f) {
        auto [cx, cy, cz] = f.get_center();
        auto [nnx, nny, nnz] = f.get_velocity_dir();
        bool is_circular = f.is_circular();

        double r = f.get_diameter() / 2.0;
        std::vector<std::array<int, 3>> covered;

        auto stamp_cell = [&](int i, int j, int k) {
            if(i == nx) --i;
            if(j == ny) --j;
            if(k == nz) --k;
            if(!in_bounds(i, j, k)) return;
            covered.push_back({i, j, k});
        };

        double ax = std::abs(nnx);
        double ay = std::abs(nny);
        double az = std::abs(nnz);

        //====================================================
        // Fan normal in +Z/-Z (disk lies in XY plane)
        //====================================================
        if(az >= ax && az >= ay) {
            double w = f.get_size_m()[0] / 2.0;
            double h = f.get_size_m()[1] / 2.0;

            int k = index_z(cz);
            int i0 = index_x(cx-w);
            int i1 = end_index_x(cx+w);
            int j0 = index_y(cy-h);
            int j1 = end_index_y(cy+h);

            if(is_circular) {
                i0 = index_x(cx-r);
                i1 = end_index_x(cx+r);
                j0 = index_y(cy-r);
                j1 = end_index_y(cy+r);
            }

            for(int i=i0;i<i1;i++) {
                for(int j=j0;j<j1;j++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double xc = cell_center_x(i);
                        double yc = cell_center_y(j);

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (yc-cy)*(yc-cy);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
        }

        //====================================================
        // Fan normal in +Y/-Y (disk lies in XZ plane)
        //====================================================
        else if(ay >= ax && ay >= az) {
            double w = f.get_size_m()[0] / 2.0;
            double h = f.get_size_m()[2] / 2.0;

            int j = index_y(cy);
            int i0 = index_x(cx-w);
            int i1 = end_index_x(cx+w);
            int k0 = index_z(cz-h);
            int k1 = end_index_z(cz+h);

            if(is_circular) {
                i0 = index_x(cx-r);
                i1 = end_index_x(cx+r);
                k0 = index_z(cz-r);
                k1 = end_index_z(cz+r);
            }

            for(int i=i0;i<i1;i++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double xc = cell_center_x(i);
                        double zc = cell_center_z(k);

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
        }

        //====================================================
        // Fan normal in +X/-X (disk lies in YZ plane)
        //====================================================
        else {
            double w = f.get_size_m()[1] / 2.0;
            double h = f.get_size_m()[2] / 2.0;

            int i = index_x(cx);
            int j0 = index_y(cy-w);
            int j1 = end_index_y(cy+w);
            int k0 = index_z(cz-h);
            int k1 = end_index_z(cz+h);

            if(is_circular) {
                j0 = index_y(cy-r);
                j1 = end_index_y(cy+r);
                k0 = index_z(cz-r);
                k1 = end_index_z(cz+r);
            }

            for(int j=j0;j<j1;j++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i, j, k);
                    } else {
                        double yc = cell_center_y(j);
                        double zc = cell_center_z(k);

                        double dist2 =
                            (yc-cy)*(yc-cy) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k);
                    }
                }
            }
        }

        // apply init velocites
        double Q_total = f.flow_m3s();
        double Q_per_cell = covered.empty() ? 0.0 : Q_total / covered.size();
        double sign = (f.get_type_t() == FlowType::Intake) ? +1.0 : -1.0;
        double area_per_cell = covered.empty() ? 0.0 : f.area() / covered.size();
        for (auto& [i, j,k ] : covered) {
            Cell& cell = at(i, j, k);
            // Overlap already validated at the geometry level.
            cell.set_state(
                f.get_type_t() == FlowType::Intake ?
                Cell::State::Intake :
                Cell::State::Exhaust
            );
            cell.set_vx(f.velocity_x());
            cell.set_vy(f.velocity_y());
            cell.set_vz(f.velocity_z());
            if (f.has_curve()) {
                cell.set_fan_curve(f.curve_a, f.curve_b, f.curve_c, f.rho_rated);
                cell.set_fan_Q_ref(Q_per_cell);
                cell.set_fan_dir(f.get_velocity_dir()[0], f.get_velocity_dir()[1], f.get_velocity_dir()[2]);
                cell.set_fan_area(area_per_cell);
                cell.set_flow_source(0.0);
            } else {
                cell.set_flow_source(sign * Q_per_cell);
            }
        }
    }

    void stamp_vent(const Vent& v) {
        if (!is_uniform()) { stamp_vent_adaptive(v); return; }

        const double dx = get_dx();
        const double dy = get_dy();
        const double dz = get_dz();

        auto [cx, cy, cz] = v.get_center();
        auto [nnx, nny, nnz] = v.get_direction();
        bool is_circular = v.is_circular();

        double r = v.get_diameter() / 2.0;
        std::vector<std::array<int, 3>> covered;

        auto stamp_cell = [&](int i, int j, int k, std::array<bool,3> wall) {
            if(!v_in_bounds(i, j, k, wall)) return;
            if(wall[0] && i == nx) i -= 1;
            if(wall[1] && j == ny) j -= 1;
            if(wall[2] && k == nz) k -= 1;
            covered.push_back({i, j, k});
        };

        double ax = std::abs(nnx);
        double ay = std::abs(nny);
        double az = std::abs(nnz);

        //====================================================
        // Vent normal in +Z/-Z (rect lies in XY plane)
        //====================================================
        if(az >= ax && az >= ay) {
            double w = v.get_size_m()[0] / 2.0; // get x component of size
            double h = v.get_size_m()[1] / 2.0; // get y component of size

            int k = static_cast<int>(std::floor(cz / dz));
            int i0 = static_cast<int>(std::floor((cx-w)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
            int j0 = static_cast<int>(std::floor((cy-h)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+h)/dy));

            if(is_circular) {
                i0 = static_cast<int>(std::floor((cx-r)/dx));
                i1 = static_cast<int>(std::ceil ((cx+r)/dx));
                j0 = static_cast<int>(std::floor((cy-r)/dy));
                j1 = static_cast<int>(std::ceil ((cy+r)/dy));
            }

            for(int i=i0;i<i1;i++) {
                for(int j=j0;j<j1;j++) {
                    if(!is_circular) {
                        stamp_cell(i,j,k,{false,false,true});
                    } else {
                        double xc = (i+0.5)*dx;
                        double yc = (j+0.5)*dy;

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (yc-cy)*(yc-cy);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k,{false,false,true});
                    }
                }
            }
        }

        //====================================================
        // Vent normal in +Y/-Y (rect lies in XZ plane)
        //====================================================
        else if(ay >= ax && ay >= az) {
            double w = v.get_size_m()[0] / 2.0; //get x component of size
            double h = v.get_size_m()[2] / 2.0; // get z component of size

            int j = static_cast<int>(std::floor(cy / dy));
            int i0 = static_cast<int>(std::floor((cx-w)/dx));
            int i1 = static_cast<int>(std::ceil ((cx+w)/dx));
            int k0 = static_cast<int>(std::floor((cz-h)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+h)/dz));

            if(is_circular) {
                i0 = static_cast<int>(std::floor((cx-r)/dx));
                i1 = static_cast<int>(std::ceil ((cx+r)/dx));
                k0 = static_cast<int>(std::floor((cz-r)/dz));
                k1 = static_cast<int>(std::ceil ((cz+r)/dz));
            }

            for(int i=i0;i<i1;i++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i,j,k,{false,true,false});
                    } else {
                        double xc = (i+0.5)*dx;
                        double zc = (k+0.5)*dz;

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k,{false,true,false});
                    }
                }
            }
        }

        //====================================================
        // Vent normal in +X/-X (rect lies in YZ plane)
        //====================================================
        else {
            double w = v.get_size_m()[1] / 2.0; // get y component of size
            double h = v.get_size_m()[2] / 2.0; // get z component of size

            int i = static_cast<int>(std::floor(cx / dx));
            int j0 = static_cast<int>(std::floor((cy-w)/dy));
            int j1 = static_cast<int>(std::ceil ((cy+w)/dy));
            int k0 = static_cast<int>(std::floor((cz-h)/dz));
            int k1 = static_cast<int>(std::ceil ((cz+h)/dz));

            if(is_circular) {
                j0 = static_cast<int>(std::floor((cy-r)/dy));
                j1 = static_cast<int>(std::ceil ((cy+r)/dy));
                k0 = static_cast<int>(std::floor((cz-r)/dz));
                k1 = static_cast<int>(std::ceil ((cz+r)/dz));
            }

            for(int j=j0;j<j1;j++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i,j,k,{true,false,false});
                    } else {
                        double yc = (j+0.5)*dy;
                        double zc = (k+0.5)*dz;

                        double dist2 =
                            (yc-cy)*(yc-cy) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k, {true, false, false});
                    }
                }
            }
        }

        // apply vent conductance
        double C_total = v.get_cd() * v.free_area();
        double C_per_cell = covered.empty() ? 0.0 : C_total / covered.size();
        for(auto& [i, j, k] : covered) {
            Cell& cell = at(i, j, k);
            // Overlap already validated at the geometry level.
            cell.set_state(Cell::State::Vent);
            cell.set_vent_conductance(C_per_cell);
        }

    }

    // Same structure as stamp_vent(): floor(v/dx) -> index_x/y/z(),
    // ceil(v/dx) -> end_index_x/y/z(), "(i+0.5)*dx" -> cell_center_x/y/z().
    void stamp_vent_adaptive(const Vent& v) {
        auto [cx, cy, cz] = v.get_center();
        auto [nnx, nny, nnz] = v.get_direction();
        bool is_circular = v.is_circular();

        double r = v.get_diameter() / 2.0;
        std::vector<std::array<int, 3>> covered;

        auto stamp_cell = [&](int i, int j, int k, std::array<bool,3> wall) {
            if(!v_in_bounds(i, j, k, wall)) return;
            if(wall[0] && i == nx) i -= 1;
            if(wall[1] && j == ny) j -= 1;
            if(wall[2] && k == nz) k -= 1;
            covered.push_back({i, j, k});
        };

        double ax = std::abs(nnx);
        double ay = std::abs(nny);
        double az = std::abs(nnz);

        //====================================================
        // Vent normal in +Z/-Z (rect lies in XY plane)
        //====================================================
        if(az >= ax && az >= ay) {
            double w = v.get_size_m()[0] / 2.0; // get x component of size
            double h = v.get_size_m()[1] / 2.0; // get y component of size

            int k = index_z(cz);
            int i0 = index_x(cx-w);
            int i1 = end_index_x(cx+w);
            int j0 = index_y(cy-h);
            int j1 = end_index_y(cy+h);

            if(is_circular) {
                i0 = index_x(cx-r);
                i1 = end_index_x(cx+r);
                j0 = index_y(cy-r);
                j1 = end_index_y(cy+r);
            }

            for(int i=i0;i<i1;i++) {
                for(int j=j0;j<j1;j++) {
                    if(!is_circular) {
                        stamp_cell(i,j,k,{false,false,true});
                    } else {
                        double xc = cell_center_x(i);
                        double yc = cell_center_y(j);

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (yc-cy)*(yc-cy);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k,{false,false,true});
                    }
                }
            }
        }

        //====================================================
        // Vent normal in +Y/-Y (rect lies in XZ plane)
        //====================================================
        else if(ay >= ax && ay >= az) {
            double w = v.get_size_m()[0] / 2.0; //get x component of size
            double h = v.get_size_m()[2] / 2.0; // get z component of size

            int j = index_y(cy);
            int i0 = index_x(cx-w);
            int i1 = end_index_x(cx+w);
            int k0 = index_z(cz-h);
            int k1 = end_index_z(cz+h);

            if(is_circular) {
                i0 = index_x(cx-r);
                i1 = end_index_x(cx+r);
                k0 = index_z(cz-r);
                k1 = end_index_z(cz+r);
            }

            for(int i=i0;i<i1;i++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i,j,k,{false,true,false});
                    } else {
                        double xc = cell_center_x(i);
                        double zc = cell_center_z(k);

                        double dist2 =
                            (xc-cx)*(xc-cx) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k,{false,true,false});
                    }
                }
            }
        }

        //====================================================
        // Vent normal in +X/-X (rect lies in YZ plane)
        //====================================================
        else {
            double w = v.get_size_m()[1] / 2.0; // get y component of size
            double h = v.get_size_m()[2] / 2.0; // get z component of size

            int i = index_x(cx);
            int j0 = index_y(cy-w);
            int j1 = end_index_y(cy+w);
            int k0 = index_z(cz-h);
            int k1 = end_index_z(cz+h);

            if(is_circular) {
                j0 = index_y(cy-r);
                j1 = end_index_y(cy+r);
                k0 = index_z(cz-r);
                k1 = end_index_z(cz+r);
            }

            for(int j=j0;j<j1;j++) {
                for(int k=k0;k<k1;k++) {
                    if(!is_circular) {
                        stamp_cell(i,j,k,{true,false,false});
                    } else {
                        double yc = cell_center_y(j);
                        double zc = cell_center_z(k);

                        double dist2 =
                            (yc-cy)*(yc-cy) +
                            (zc-cz)*(zc-cz);

                        if(dist2 <= r*r)
                            stamp_cell(i,j,k, {true, false, false});
                    }
                }
            }
        }

        // apply vent conductance
        double C_total = v.get_cd() * v.free_area();
        double C_per_cell = covered.empty() ? 0.0 : C_total / covered.size();
        for(auto& [i, j, k] : covered) {
            Cell& cell = at(i, j, k);
            // Overlap already validated at the geometry level.
            cell.set_state(Cell::State::Vent);
            cell.set_vent_conductance(C_per_cell);
        }

    }

    void check_stamps() const {
        double air = 0.0;
        double solid = 0.0;
        double vent = 0.0;
        double fan = 0.0;
        for(int i = 0; i < nx; i++) {
            for(int j = 0; j < ny; j++) {
                for(int k = 0; k < nz; k++) {
                    const Cell& c = at(i, j, k);
                    if(c.is_solid()) solid++;
                    if(c.is_fan()) fan++;
                    if(c.is_air()) air++;
                    if(c.is_vent()) vent++;
                }
            }
        }
        std::cout << "air cells: " << air << "\n";
        std::cout << "solid cells: " << solid << "\n";
        std::cout << "fan cells: " << fan << "\n";
        std::cout << "vent cells: " << vent << "\n";
    }

    void print_mesh() const {
        for (int z = 0; z < nz; ++z) {
            std::cout << "\n========== Layer z = " << z << " ==========\n";
            // x headers
            std::cout << "    ";
            for (int x = 0; x < nx; ++x) {
                std::cout << std::setw(2) << x << ' ';
            }
            std::cout << '\n';
            // rows are y
            for (int y = 0; y < ny; ++y) {
                std::cout << std::setw(2) << y << "  ";
                for (int x = 0; x < nx; ++x) {
                    const auto& cell = cells[idx(x,y,z)];
                    std::string print = " . ";
                    if(cell.get_state() == Cell::State::Component) {
                        print = " # ";
                    } else if (cell.get_state() == Cell::State::Exhaust) {
                        print = " E ";
                    } else if (cell.get_state() == Cell::State::Intake) {
                        print = " I ";
                    } else if (cell.get_state() == Cell::State::Vent) {
                        print = "V";
                    }
                    std::cout
                        << print;
                }
                std::cout << '\n';
            }
        }
        std::cout << std::endl;
    }

    void print_mesh_layer_temp(int layer) {
        std::cout << "\n========== Layer z = " << layer << " ==========\n";
        // x headers
        std::cout << "    ";
        for (int x = 0; x < nx; ++x) {
            std::cout << std::setw(2) << x << ' ';
        }
        std::cout << '\n';
        // rows are y
        for (int y = 0; y < ny; ++y) {
            std::cout << std::setw(2) << y << "  ";
            for (int x = 0; x < nx; ++x) {
                const auto& cell = cells[idx(x,y,layer)];
                double print = cell.get_T();
                std::cout << print << " ";
            }
            std::cout << '\n';
        }

        std::cout << std::endl;
    }

private:
    int nx, ny, nz;
    // Per-axis cell widths. Right now build_mesh() always fills these with
    // nx/ny/nz identical copies of a single scalar, so behavior is byte-for-
    // byte the same as the old flat dx/dy/dz members - this is groundwork
    // for adaptive meshing (Stage 3), not a behavior change yet.
    std::vector<double> dxs, dys, dzs;
    // Cumulative cell-boundary positions per axis (size n+1), rebuilt any
    // time dxs/dys/dzs are set. Only consumed by index_x/y/z, end_index_x/
    // y/z, and cell_center_x/y/z - the uniform stamp_component/fan/vent
    // never touch these, so they stay bit-exact with pre-Stage-3 behavior.
    std::vector<double> x_bounds, y_bounds, z_bounds;
    Environment env;
    Workload load;

    std::vector<Cell> cells;
    std::vector<OpenFoamCellMetadata> openfoam_cell_metadata;
    std::vector<OpenFoamComponentRegion> openfoam_component_regions;
    std::vector<OpenFoamHeatSourceRegion> openfoam_heat_source_regions;
    std::vector<OpenFoamBoundaryPatch> openfoam_boundary_patches;
    std::vector<OpenFoamInternalFlowDevice>
        openfoam_internal_flow_devices;
    std::vector<int> openfoam_boundary_patch_ids;
    std::vector<InternalFanInterface> internal_fans;
    std::vector<int32_t> wall_face_refs;
    std::vector<WallFace> wall_faces;
    int next_wall_component_group = 0;

    void enable_openfoam_export_metadata() {
        if(openfoam_cell_metadata.empty()) {
            openfoam_cell_metadata.assign(
                get_cell_count(), OpenFoamCellMetadata{});
            openfoam_boundary_patch_ids.assign(
                get_cell_count()*6u, -1);
        }
    }

    std::size_t mark_openfoam_boundary_opening(
        const std::array<double,3>& center,
        const std::array<double,3>& direction,
        const std::array<double,3>& size,
        double diameter,
        bool circular,
        Cell::State expected_state,
        int patch_id) {
        const double ax = std::abs(direction[0]);
        const double ay = std::abs(direction[1]);
        const double az = std::abs(direction[2]);
        const int axis =
            ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
        const std::array<const std::vector<double>*,3> bounds{
            &x_bounds,&y_bounds,&z_bounds};
        const double extent = bounds[axis]->back();
        const double tolerance =
            1e-9 * std::max(1.0,std::abs(extent));
        int side = -1;
        if(std::abs(center[axis]) <= tolerance) side = 0;
        else if(std::abs(center[axis]-extent) <= tolerance) side = 1;
        if(side < 0)
            throw std::invalid_argument(
                "OpenFOAM boundary opening center is not on the rack exterior.");

        const int first_tangent = axis == 0 ? 1 : 0;
        const int second_tangent = axis == 2 ? 1 : 2;
        const double radius = 0.5*diameter;
        std::size_t marked = 0;

        for(int a=0; a<(first_tangent==0 ? nx :
                        first_tangent==1 ? ny : nz); ++a) {
            for(int b=0; b<(second_tangent==0 ? nx :
                            second_tangent==1 ? ny : nz); ++b) {
                std::array<int,3> cell_index{0,0,0};
                cell_index[axis] =
                    side == 0 ? 0 :
                    (axis == 0 ? nx-1 : axis == 1 ? ny-1 : nz-1);
                cell_index[first_tangent] = a;
                cell_index[second_tangent] = b;

                const std::array<double,3> cell_center{
                    cell_center_x(cell_index[0]),
                    cell_center_y(cell_index[1]),
                    cell_center_z(cell_index[2])};
                bool covered = false;
                if(circular) {
                    const double da =
                        cell_center[first_tangent]-center[first_tangent];
                    const double db =
                        cell_center[second_tangent]-center[second_tangent];
                    covered = da*da+db*db <= radius*radius+tolerance;
                } else {
                    covered =
                        std::abs(cell_center[first_tangent]-
                                 center[first_tangent])
                            <= 0.5*size[first_tangent]+tolerance &&
                        std::abs(cell_center[second_tangent]-
                                 center[second_tangent])
                            <= 0.5*size[second_tangent]+tolerance;
                }
                if(!covered ||
                   at(cell_index[0],cell_index[1],cell_index[2])
                       .get_state() != expected_state)
                    continue;

                openfoam_boundary_patch_ids[
                    idx(cell_index[0],cell_index[1],cell_index[2])*6u +
                    static_cast<std::size_t>(axis*2+side)] = patch_id;
                ++marked;
            }
        }
        return marked;
    }

    void populate_openfoam_boundary_source_zone(
        int patch_id, const std::array<double,3>& direction) {
        auto& patch =
            openfoam_boundary_patches.at(static_cast<std::size_t>(patch_id));
        patch.direction = direction;
        std::vector<unsigned char> selected(get_cell_count(),0);
        const bool fan =
            patch.kind==OpenFoamBoundaryPatch::Kind::Inlet ||
            patch.kind==OpenFoamBoundaryPatch::Kind::Outlet;
        for(int i=0;i<nx;++i) for(int j=0;j<ny;++j)
            for(int k=0;k<nz;++k)
                for(int face=0;face<6;++face) {
                    const std::size_t boundary_cell=idx(i,j,k);
                    if(openfoam_boundary_patch_ids[
                           boundary_cell*6u+
                           static_cast<std::size_t>(face)]!=patch_id)
                        continue;
                    std::array<int,3> source{i,j,k};
                    if(fan) {
                        const int axis=face/2;
                        const int side=face%2;
                        source[axis]+=side==0 ? 1 : -1;
                        if(!in_bounds(source[0],source[1],source[2]) ||
                           !at(source[0],source[1],source[2]).is_fluid())
                            throw std::runtime_error(
                                "OpenFOAM ambient fan '"+patch.name+
                                "' has no inboard fluid layer for its "
                                "momentum-source zone.");
                    }
                    selected[idx(source[0],source[1],source[2])]=1;
                }
        for(std::size_t cell=0; cell<selected.size(); ++cell)
            if(selected[cell]) patch.adjacent_cells.push_back(cell);

        const double ax=std::abs(direction[0]);
        const double ay=std::abs(direction[1]);
        const double az=std::abs(direction[2]);
        const int axis=ax>=ay && ax>=az ? 0 : (ay>=az ? 1 : 2);
        if(!patch.adjacent_cells.empty()) {
            const Cell& cell=cells.at(patch.adjacent_cells.front());
            patch.source_zone_thickness =
                axis==0 ? cell.get_dx() :
                (axis==1 ? cell.get_dy() : cell.get_dz());
        }
    }

    void build_bounds() {
        x_bounds.assign(nx + 1, 0.0);
        for (int i = 0; i < nx; ++i) x_bounds[i+1] = x_bounds[i] + dxs[i];

        y_bounds.assign(ny + 1, 0.0);
        for (int j = 0; j < ny; ++j) y_bounds[j+1] = y_bounds[j] + dys[j];

        z_bounds.assign(nz + 1, 0.0);
        for (int k = 0; k < nz; ++k) z_bounds[k+1] = z_bounds[k] + dzs[k];
    }

    // static int locate_floor(const std::vector<double>& bounds, double coord) {
    //     auto it = std::upper_bound(bounds.begin(), bounds.end(), coord);
    //     return static_cast<int>(it - bounds.begin()) - 1;
    // }

    static int locate_floor(const std::vector<double>& bounds, double coord) {
        const int snapped=locate_near_boundary(bounds,coord);
        if(snapped>=0) {
            const int n=static_cast<int>(bounds.size())-1;
            return std::min(snapped,n-1);
        }
        auto it = std::upper_bound(bounds.begin(), bounds.end(), coord);
        int idx = static_cast<int>(it - bounds.begin()) - 1;
        // A coordinate exactly on (or fractionally past) the outer boundary
        // belongs to the last cell, not one-past it.
        const int n = static_cast<int>(bounds.size()) - 1;
        if (idx >= n) idx = n - 1;
        return idx;
    }

    static int locate_ceil(const std::vector<double>& bounds, double coord) {
        const int snapped=locate_near_boundary(bounds,coord);
        if(snapped>=0) return snapped;
        auto it = std::lower_bound(bounds.begin(), bounds.end(), coord);
        return static_cast<int>(it - bounds.begin());
    }

    static int locate_near_boundary(const std::vector<double>& bounds,
                                    double coord) {
        if(bounds.empty() || !std::isfinite(coord)) return -1;
        const double scale=std::max(
            {1.0,std::abs(coord),std::abs(bounds.back())});
        const double numeric_tolerance=
            64.0*std::numeric_limits<double>::epsilon()*scale;
        auto it=std::lower_bound(bounds.begin(),bounds.end(),coord);
        if(it==bounds.begin())
            return std::abs(*it-coord)<=numeric_tolerance ? 0 : -1;
        if(it==bounds.end()) {
            const int last=static_cast<int>(bounds.size())-1;
            return std::abs(bounds.back()-coord)<=numeric_tolerance ? last : -1;
        }

        // Geometry cuts closer than the planner's anti-sliver threshold are
        // deliberately represented by one retained face. Round a coordinate
        // inside a nonuniform cell to its nearest face, rather than always
        // expanding a stamped region to both enclosing faces. Exact/planned
        // cuts still land on themselves; merged cuts now remain independent
        // of refinement-band spacing.
        const auto previous=std::prev(it);
        const double lower_distance=coord-*previous;
        const double upper_distance=*it-coord;
        if(lower_distance<=upper_distance+numeric_tolerance)
            return static_cast<int>(previous-bounds.begin());
        return static_cast<int>(it-bounds.begin());
    }
};

#endif

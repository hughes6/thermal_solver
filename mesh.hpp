#ifndef MESH_HPP
#define MESH_HPP

#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <iomanip>

#include "cell.hpp"
#include "component.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "rack.hpp"
#include "vent.hpp"
#include "workload.hpp"

class Mesh {
public:
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
    const std::vector<InternalFanInterface>& get_internal_fans() const {
        return internal_fans;
    }
    std::vector<InternalFanInterface>& get_internal_fans() {
        return internal_fans;
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
                            stamp_cell(i, j, k, {false, true, false});
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
                            stamp_cell(i, j, k, {true, false, false});
                        }
                    }
                }
                // apply vent conductance
                double C_total = r.get_cd() * r.free_area();
                double C_per_cell = covered.empty() ? 0.0 : C_total / covered.size();
                for(auto& [i, j, k] : covered) {
                    Cell& cell = at(i, j, k);
                    // Overlap already validated at the geometry level.
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
                // apply init velocities
                double Q_total = r.flow_m3s();
                double Q_per_cell = covered.empty() ? 0.0 : Q_total / covered.size();
                const int sx = nnx > 0.0 ? 1 : (nnx < 0.0 ? -1 : 0);
                const int sy = nny > 0.0 ? 1 : (nny < 0.0 ? -1 : 0);
                const int sz = nnz > 0.0 ? 1 : (nnz < 0.0 ? -1 : 0);
                for (auto& [i, j, k] : covered) {
                    Cell& cell = at(i, j, k);
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
                            stamp_cell(i, j, k, {false, true, false});
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
                            stamp_cell(i, j, k, {true, false, false});
                        }
                    }
                }
                // apply vent conductance
                double C_total = r.get_cd() * r.free_area();
                double C_per_cell = covered.empty() ? 0.0 : C_total / covered.size();
                for(auto& [i, j, k] : covered) {
                    Cell& cell = at(i, j, k);
                    // Overlap already validated at the geometry level.
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
                                stamp_cell(i, j, k);
                            } else {
                                double xc = cell_center_x(i);
                                double yc = cell_center_z(j);

                                double dist2 =
                                    (xc-cx)*(xc-cx) +
                                    (yc-cy)*(yc-cy);

                                if(dist2 <= rad*rad)
                                    stamp_cell(i,j,k);
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
                                stamp_cell(i, j, k);
                            } else {
                                double xc = cell_center_y(i);
                                double yc = cell_center_z(j);

                                double dist2 =
                                    (xc-cx)*(xc-cx) +
                                    (yc-cy)*(yc-cy);

                                if(dist2 <= rad*rad)
                                    stamp_cell(i,j,k);
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
    std::vector<InternalFanInterface> internal_fans;

    void build_bounds() {
        x_bounds.assign(nx + 1, 0.0);
        for (int i = 0; i < nx; ++i) x_bounds[i+1] = x_bounds[i] + dxs[i];

        y_bounds.assign(ny + 1, 0.0);
        for (int j = 0; j < ny; ++j) y_bounds[j+1] = y_bounds[j] + dys[j];

        z_bounds.assign(nz + 1, 0.0);
        for (int k = 0; k < nz; ++k) z_bounds[k+1] = z_bounds[k] + dzs[k];
    }

    static int locate_floor(const std::vector<double>& bounds, double coord) {
        auto it = std::upper_bound(bounds.begin(), bounds.end(), coord);
        return static_cast<int>(it - bounds.begin()) - 1;
    }

    static int locate_ceil(const std::vector<double>& bounds, double coord) {
        auto it = std::lower_bound(bounds.begin(), bounds.end(), coord);
        return static_cast<int>(it - bounds.begin());
    }
};

#endif

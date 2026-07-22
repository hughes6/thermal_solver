#ifndef MODEL_LOADER_HPP
#define MODEL_LOADER_HPP

#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <string>

#include "input_types.hpp"
#include "../component_grapher.hpp"
#include "../collision.hpp"
#include "../toml.hpp"


// -----------------------------
// ANON NAMESPACE FOR HELPERS
// -----------------------------
namespace {

    const toml::table& require_table (const toml::node_view<const toml::node>& node, const std::string& context) {
        const toml::table* table = node.as_table();
        if(table == nullptr) {
            throw std::runtime_error(context + " must be a TOML table");
        }

        return *table;
    }


    template<typename T>
    T require_value(const toml::node_view<const toml::node>& node, const std::string& context) {
        const auto value = node.value<T>();

        if(!value) {
            throw std::runtime_error("Missing or invalid value: " + context);
        }

        return *value;
    }

    PositionInput parse_position(const toml::table& table, const std::string& context) {
        PositionInput position;
        position.x = require_value<double>(table["x"], context + ".x");
        position.y = require_value<double>(table["y"], context + ".y");
        position.z = require_value<double>(table["z"], context + ".z");
        position.units = table["units"].value<std::string>();
        return position;
    }

    DirectionInput parse_direction(const toml::table& table, const std::string& context) {
        return {
            require_value<double>(table["x"], context + ".x"),
            require_value<double>(table["y"], context + ".y"),
            require_value<double>(table["z"], context + ".z")
        };
    }

    SizeInput parse_size(const toml::table& table, const std::string& context) {
        SizeInput size;
        size.width = require_value<double>(table["width"], context + ".width");
        size.depth = require_value<double>(table["depth"], context + ".depth");
        size.height = require_value<double>(table["height"], context + ".height");
        size.units = table["units"].value<std::string>();
        return size;
    }

    MaterialInput parse_material(const toml::table& table, const std::string& context) {
        return {
            require_value<double>(table["rho"], context + ".rho"),
            require_value<double>(table["cp"],  context + ".cp"),
            require_value<double>(table["k"],   context + ".k")
        };
    }

    FanShape parse_fan_shape(const std::string& value) {
        if(value == "circular") {
            return FanShape::Circular;
        }
        if(value == "rectangular") {
            return FanShape::Rectangular;
        }
        throw std::runtime_error("Invalid Fan Shape" + value);
    }

    FanFlowType parse_fan_flow_type(const std::string& value) {
        if(value == "intake") {
            return FanFlowType::Intake;
        }
        if(value == "exhaust") {
            return FanFlowType::Exhaust;
        }
        throw std::runtime_error("Invalid fan flow type: " + value);
    }

    VentShape parse_vent_shape(const std::string& value) {
        if(value == "circular") {
            return VentShape::Circular;
        }
        if(value == "rectangular") {
            return VentShape::Rectangular;
        }
        throw std::runtime_error("Invalid vent flow type: " + value);
    }

    FanInput parse_fan(const toml::table& table, const std::string& context);
    VentInput parse_vent(const toml::table& table, const std::string& context);

    RegionState parse_region_state(const std::string& value) {
        if(value == "solid") {
            return RegionState::Solid;
        }
        if(value == "air") {
            return RegionState::Air;
        }
        if(value == "vent") {
            return RegionState::Vent;
        }
        if(value == "fan") {
            return RegionState::Fan;
        }
        throw std::runtime_error("Invalid region state: " + value);
    }

    InternalRegionInput parse_internal_region(const toml::table& table, const std::string& context) {
        InternalRegionInput internal_region;
        internal_region.state = parse_region_state(require_value<std::string>(table["state"], context + ".state"));

        if(internal_region.state == RegionState::Fan) {
            internal_region.fan = parse_fan(table, context);
            internal_region.name = internal_region.fan->name;
            internal_region.local_position = internal_region.fan->position;
            if(internal_region.fan->size.has_value()) internal_region.size = *internal_region.fan->size;
            return internal_region;
        }

        if(internal_region.state == RegionState::Vent) {
            internal_region.vent = parse_vent(table, context);
            internal_region.name = internal_region.vent->name;
            internal_region.local_position = internal_region.vent->position;
            if(internal_region.vent->size.has_value()) internal_region.size = *internal_region.vent->size;
            return internal_region;
        }

        internal_region.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        internal_region.local_position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        internal_region.name = require_value<std::string>(table["name"], context + ".name");

        if(internal_region.state == RegionState::Air) return internal_region;

        if(internal_region.state == RegionState::Solid) {
            internal_region.material = parse_material(require_table(table["material"], context + ".material"), context + ".material");
            internal_region.watts = require_value<double>(table["watts"], context + ".watts");
            return internal_region;
        }

        throw std::runtime_error(context + ": unsupported internal region state");
    }

    ComponentInput parse_component(const toml::table& table, const std::string& context, bool is_loading_from_template = false, PositionInput pos = PositionInput()) {
        ComponentInput component;

        component.template_path = table["template"].value<std::string>();
        if(!is_loading_from_template) {
        component.position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        } else {
            component.position = pos;
        }
        if (component.template_path.has_value()) return component;

        component.name = require_value<std::string>(table["name"], context + ".name");
        component.watts = require_value<double>(table["watts"], context + ".watts");
        component.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        component.material = parse_material(require_table(table["material"], context + ".material"), context + ".material");

        const toml::array* internal_regions = table["internal_regions"].as_array();

        if(internal_regions == nullptr) {
            throw std::runtime_error("internal_regions[...] must be a table");
        }

        std::size_t index = 0;

        for(const toml::node& node : *internal_regions) {

            const toml::table* internal_region_table = node.as_table();
            if(internal_region_table == nullptr) {
                throw std::runtime_error("internal_regions[" + std::to_string(index) + "] must be a table");
            }
            component.internal_regions.push_back(parse_internal_region(*internal_region_table, "internal_regions[" + std::to_string(index) + "]"));
            index++;
        }
        return component;
    }
        
    ComponentInput load_component_template(const std::filesystem::path& path, PositionInput pos) {
        try {
            const toml::table root = toml::parse_file(path.string());
            return parse_component(root, path.stem().string(), true, pos);
        } catch (const toml::parse_error& error) {
            throw std::runtime_error("Failed to parse component template '" + path.string() + "': "
                                    + std::string(error.description()));
        }
    }

    FanInput parse_fan(const toml::table& table, const std::string& context) {
        FanInput fan;

        fan.name = require_value<std::string>(table["name"], context + ".name");
        fan.shape = parse_fan_shape(require_value<std::string>(table["shape"], context + ".shape"));
        fan.flow_type = parse_fan_flow_type(require_value<std::string>(table["flow_type"], context + ".flow_type"));
        fan.cfm = require_value<double>(table["cfm"], context + ".cfm");
        fan.position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        fan.direction = parse_direction(require_table(table["direction"], context + ".direction"), context + ".direction");
       
        fan.curve_name = table["curve"].value<std::string>();

        if(fan.shape == FanShape::Rectangular) {
            fan.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        } else {
            fan.diameter = table["diameter"].value<double>().value_or(0.0);       
            fan.diameter_units = table["diameter_units"].value<std::string>().value_or("u");
        }

        return fan;
    }

    VentInput parse_vent(const toml::table& table, const std::string& context) {
        VentInput vent;

        vent.name = require_value<std::string>(table["name"], context + ".name");
        vent.shape = parse_vent_shape(require_value<std::string>(table["shape"], context + ".shape"));
        vent.position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        vent.direction = parse_direction(require_table(table["normal"], context + ".direction"), context + ".direction");
        vent.free_area_ratio = require_value<double>(table["free_area_ratio"], context + ".free_area_ratio");
        vent.cd = require_value<double>(table["vent_discharge_coeff"],context + ".vent_discharge_coeff");

        if(vent.shape == VentShape::Rectangular) {
            vent.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        } else {
            vent.diameter = require_value<double>(table["diameter"], context + ".diameter");
            vent.diameter_units = table["diameter_units"].value<std::string>().value_or("u");
        }
        
        return vent;
    }

    void parse_global_components(const toml::table& root, ModelInput& model) {
        const toml::array* components = root["components"].as_array();
        if(components == nullptr) return;

        std::size_t index = 0;

        for(const toml::node& node : *components) {
            const toml::table* component_table = node.as_table();
            if(component_table == nullptr) {
                throw std::runtime_error("components[" + std::to_string(index) + "] must be a table");
            }

            model.components.push_back(parse_component(*component_table, "compnents[" + std::to_string(index) + "]"));
            index++;
        }
    }

    void parse_global_fans(const toml::table& root, ModelInput& model) {
        const toml::array* fans = root["fans"].as_array();
        if(fans == nullptr) return;

        std::size_t index = 0;

        for(const toml::node& node : *fans) {
            const toml::table* fan_table = node.as_table();
            if(fan_table == nullptr) {
                throw std::runtime_error("fans[" + std::to_string(index) + "] must be a table");
            }
            model.fans.push_back(parse_fan(*fan_table, "fans[" + std::to_string(index) + "]"));
            index++;
        }
    }

    void parse_global_vents(const toml::table& root, ModelInput& model) {
        const toml::array* vents = root["vents"].as_array();
        if(vents == nullptr) return;

        std::size_t index = 0;

        for(const toml::node& node : *vents) {
            const toml::table* vent_table = node.as_table();
            if(vent_table == nullptr) {
                throw std::runtime_error("vents[" + std::to_string(index) + "] must be a table");
            }
            model.vents.push_back(parse_vent(*vent_table, "vents[" + std::to_string(index) + "]"));
            index++;
        }
    }

    FanCurveInput parse_fan_curve(const toml::table& table, const std::string& context) {
        FanCurveInput curve;
        curve.name = require_value<std::string>(table["name"], context + ".name");
        curve.a = require_value<double>(table["a"], context + ".a");
        curve.b = require_value<double>(table["b"], context + ".b");
        curve.c = table["c"].value<double>().value_or(0.0);
        curve.rho_rated = table["rho_rated"].value<double>().value_or(1.2);

        if (curve.a <= 0.0) {
            throw std::runtime_error(context + ": shutoff pressure 'a' must be > 0.0");
        }
        return curve;
    }

    std::unordered_map<std::string, FanCurveInput>
        load_fan_curve_library(const std::filesystem::path& path) {
            std::unordered_map<std::string, FanCurveInput> library;
            try {
                const toml::table root = toml::parse_file(path.string());
                const toml::array* curves = root["fan_curve"].as_array();
                if (curves == nullptr) return library;

                std::size_t index = 0;
                for (const toml::node& node : *curves) {
                    const toml::table* curve_table = node.as_table();
                    if (curve_table == nullptr) {
                        throw std::runtime_error("fan_curve[" + std::to_string(index) + "] must be a table");
                    }
                    FanCurveInput curve = parse_fan_curve(*curve_table, "fan_curve[" + std::to_string(index) + "]");
                    if (library.count(curve.name)) {
                        throw std::runtime_error("Duplicate fan curve name: " + curve.name);
                    }
                    library[curve.name] = curve;
                    index++;
                }
            } catch (const toml::parse_error& error) {
                throw std::runtime_error("Failed to parse fan curve library '" + path.string() + "': "
                                        + std::string(error.description()));
            }
            return library;
        }

    double to_meters(double value, const std::string& units, const std::string& context) {
        if(units == "m") return value;
        if(units == "u") return value * Fan::U_TO_M;
        if(units == "in") return value * Fan::IN_TO_M;
        if(units == "mm") return value * Fan::MM_TO_M;
        throw std::runtime_error("Invalid " + context + " units: '" + units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
    }

    std::array<double, 3> position_to_meters(const PositionInput& position, const std::string& context) {
        const std::string units = position.units.value_or("u");
        return {
            to_meters(position.x, units, context),
            to_meters(position.y, units, context),
            to_meters(position.z, units, context)
        };
    }

    std::array<double, 3> size_to_meters(const SizeInput& size, const std::string& context) {
        const std::string units = size.units.value_or("u");
        return {
            to_meters(size.width, units, context),
            to_meters(size.depth, units, context),
            to_meters(size.height, units, context)
        };
    }

    InternalRegion build_internal_region(const InternalRegionInput& input) {
        if(input.state == RegionState::Fan) {
            if(!input.fan.has_value()) throw std::runtime_error("Internal fan region is missing parsed fan data.");
            const FanInput& f = *input.fan;
            const std::array<double, 3> size = f.shape == FanShape::Rectangular
                ? size_to_meters(*f.size, "internal fan.size")
                : std::array<double, 3>{0.0, 0.0, 0.0};
            const double diameter = f.shape == FanShape::Circular
                ? to_meters(*f.diameter, f.diameter_units.value_or("u"), "internal fan.diameter")
                : 0.0;
            Fan fan(f.name, f.cfm, diameter, size,
                position_to_meters(f.position, "internal fan.position"),
                {f.direction.x, f.direction.y, f.direction.z},
                f.flow_type == FanFlowType::Intake ? FlowType::Intake : FlowType::Exhaust,
                f.shape == FanShape::Circular ? ShapeType::Circular : ShapeType::Rectangular
            );
            return InternalRegion(fan);
        }

        if(input.state == RegionState::Vent) {
            if(!input.vent.has_value()) throw std::runtime_error("Internal vent region is missing parsed vent data.");
            const VentInput& v = *input.vent;
            const std::array<double, 3> size = v.shape == VentShape::Rectangular
                ? size_to_meters(*v.size, "internal vent.size")
                : std::array<double, 3>{0.0, 0.0, 0.0};
            const double diameter = v.shape == VentShape::Circular
                ? to_meters(*v.diameter, v.diameter_units.value_or("u"), "internal vent.diameter")
                : 0.0;
            Vent vent(v.name, size, v.free_area_ratio, diameter, v.cd,
                position_to_meters(v.position, "internal vent.position"),
                {v.direction.x, v.direction.y, v.direction.z},
                v.shape == VentShape::Circular ? VentShapeType::Circular : VentShapeType::Rectangular
            );
            return InternalRegion(vent);
        }

        InternalRegion region;
        const auto position = position_to_meters(input.local_position, "internal_region.position");
        const auto size = size_to_meters(input.size, "internal_region.size");
        region.set_local_position(position);
        region.set_size(size);
        region.set_name(input.name);

        if(input.state == RegionState::Solid) {
            region.set_region_type(RegionType::HeatSource);
            region.set_cp(input.material.cp);
            region.set_rho(input.material.density);
            region.set_k(input.material.k);
            region.set_watts(input.watts);
        } else if(input.state == RegionState::Air) {
            region.set_region_type(RegionType::Air);
        } else {
            throw std::runtime_error("Unsupported internal region state.");
        }
        return region;
    }
}

struct ModelLoader {
    ModelInput model;
    std::unordered_map<std::string, FanCurveInput> fan_curve_library;
    std::unordered_map<std::string, ComponentInput> component_template_cache; 

    ModelLoader() = default;

    void load_fan_curves(const std::filesystem::path& library_path) {
        fan_curve_library = load_fan_curve_library(library_path);
    }

    void load_model(const std::filesystem::path& model_path) 
    {
        try {
            const toml::table root = toml::parse_file(model_path.string());

            model.name = root["name"].value<std::string>().value_or(model_path.stem().string());

            // ------------------------------------------Simulation------------------------------------------
            const toml::table& simulation = require_table(root["simulation"], "simulation");
            model.simulation.dt = require_value<double>(simulation["dt"], "simulation.dt");
            model.simulation.duration = require_value<double>(simulation["duration"], "simulation.duration");
            model.simulation.output_interval = simulation["output_interval"].value<int>().value_or(1);
            model.simulation.max_timesteps = require_value<double>(simulation["max_timesteps"], "simulation.max_timesteps");
            model.simulation.max_updates = require_value<double>(simulation["max_updates"], "simulation.max_updates");
            model.simulation.max_cell_count = require_value<double>(simulation["max_cell_count"], "simulation.max_cell_count");
            model.simulation.max_megabyte_usage = require_value<double>(simulation["max_megabyte_usage"], "simulation.max_megabyte_usage");
            model.simulation.update_flow_interval = simulation["update_flow_interval"].value<int>().value_or(1);

            // ----------------------------------------Flow Solver-------------------------------------------
            const toml::table& flow_solver = require_table(root["flow_solver"], "simulation");
            model.flow_solver.enable_flow_solver = flow_solver["enable_flow_solver"].value<bool>().value_or(false);
            model.flow_solver.resistivity = flow_solver["resistivity"].value<double>().value_or(4.5);
            model.flow_solver.tolerance = flow_solver["tolerance"].value<double>().value_or(1e-4);
            model.flow_solver.max_iterations = flow_solver["max_iterations"].value<int>().value_or(100);
            model.flow_solver.sor_omega = flow_solver["sor_omega"].value<double>().value_or(1.2);
            model.flow_solver.max_outer_iters = flow_solver["max_outer_iters"].value<int>().value_or(5);
            model.flow_solver.flow_tolerance = flow_solver["flow_tolerance"].value<double>().value_or(1e-2);

            // ----------------------------------------Environtment------------------------------------------
            const toml::table& environment = require_table(root["environment"], "environment");
            model.environment.humidity = require_value<double>(environment["humidity"], "environtment.humidity");
            model.environment.elevation = require_value<double>(environment["elevation"], "environtment.elevation");
            model.environment.T_ambient = require_value<double>(environment["T_ambient"], "environtment.T_ambient");
            model.environment.cp = require_value<double>(environment["cp"], "environtment.cp");
            model.environment.k = require_value<double>(environment["k"], "environtment.k");
            model.environment.mu = require_value<double>(environment["mu"], "environtment.mu");
            model.environment.pr = require_value<double>(environment["pr"], "environment.pr");
            model.environment.rho = require_value<double>(environment["rho"], "environtment.rho");

            // ------------------------------------------Mesh------------------------------------------------
            const toml::table& mesh = require_table(root["mesh"],"mesh");
            model.mesh.dx = require_value<double>(mesh["dx"], "mesh.dx");
            model.mesh.dy = require_value<double>(mesh["dy"], "mesh.dy");
            model.mesh.dz = require_value<double>(mesh["dz"], "mesh.dz");

            // ------------------------------------------Rack------------------------------------------------
            const toml::table& rack = require_table(root["rack"], "rack");
            model.rack.name = rack["name"].value<std::string>().value_or("Unnamed rack");
            model.rack.size = parse_size(require_table(rack["size"], "rack.size"), "rack.size");
            const toml::table& ambient = require_table(rack["ambient"], "rack.ambient");
            model.rack.ambient.pressure = ambient["pressure"].value<double>().value_or(101325.0);
            model.rack.ambient.h = ambient["h"].value<double>().value_or(0.0);
            model.rack.ambient.temperature = require_value<double>(ambient["temperature"], "rack.ambient.temperature");
            model.rack.ambient.k = require_value<double>(ambient["k"], "rack.ambient.k");
            model.rack.ambient.rho = require_value<double>(ambient["rho"], "rack.ambient.rho");
            model.rack.ambient.cp = require_value<double>(ambient["cp"], "rack.ambient.cp");

            // ------------------------------------------Global Objects---------------------------------------
            parse_global_components(root, model);
            parse_global_fans(root, model);
            parse_global_vents(root, model);

        } catch(const toml::parse_error& error) {
            throw std::runtime_error("Failed to parse model file '" + model_path.string() + "': " + std::string(error.description()));
        }
    }

const ComponentInput& get_component_template(const std::string& path, PositionInput pos) {
    auto it = component_template_cache.find(path);
    if (it != component_template_cache.end()) return it->second;
    auto [inserted, ok] = component_template_cache.emplace(path, load_component_template(path, pos));
    return inserted->second;
}

void run() 
    {
    Workload load = Workload(model.simulation.max_timesteps, model.simulation.max_updates, model.simulation.max_cell_count, model.simulation.max_megabyte_usage);
    Environment env(model.environment.humidity, model.environment.elevation, model.environment.T_ambient, 
                    model.environment.cp, model.environment.k, model.environment.mu, model.environment.pr, model.environment.rho);
    Rack rack;
    const std::string rack_units = model.rack.size.units.value_or("u");

    if(rack_units == "u") {
        rack = Rack::from_rack_units(model.rack.size.width, model.rack.size.depth, model.rack.size.height, model.rack.name);
    } else if(rack_units == "m") {
        rack = Rack::from_meters(model.rack.size.width, model.rack.size.depth, model.rack.size.height, model.rack.name);
    } else if(rack_units == "in") {
        rack = Rack::from_inches(model.rack.size.width, model.rack.size.depth, model.rack.size.height, model.rack.name);
    } else if(rack_units == "mm") {
        rack = Rack::from_mm(model.rack.size.width, model.rack.size.depth, model.rack.size.height, model.rack.name);
    } else {
        throw std::runtime_error("Invalid rack.size units: '" + rack_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
    }
    rack.set_cp(model.rack.ambient.cp);
    rack.set_k(model.rack.ambient.k);
    rack.set_h(model.rack.ambient.h);
    rack.set_rho(model.rack.ambient.rho);
    rack.set_t(model.rack.ambient.temperature);

    Mesh mesh = Mesh().build_mesh(rack, model.mesh.dx, model.mesh.dy, model.mesh.dz, env, load);
    Grapher grapher = Grapher(rack, model.mesh.dx, model.mesh.dy, model.mesh.dz);

    // Built up-front, geometry-checked as a whole, then stamped. Keeping the
    // "build" and "stamp" phases separate is what lets CollisionChecker see
    // every component/fan/vent before the mesh has any say in the matter.
    std::vector<Component> components;
    std::vector<Fan> fans;
    std::vector<Vent> vents;

    for(const ComponentInput& c_in : model.components) {
        Component component;
        ComponentInput c = c_in;
        if (c.template_path.has_value()) {
            const ComponentInput& tmpl = get_component_template(*c.template_path, c.position);
            c.name = tmpl.name;
            c.size = tmpl.size;
            c.material = tmpl.material;
            c.watts = tmpl.watts;
            c.internal_regions = tmpl.internal_regions;
        }
        const std::string comp_units = c.size.units.value_or("u");
        if(comp_units == "u") {
            component = Component::from_rack_units(c.size.width, c.size.depth, c.size.height, c.name);
        } else if(comp_units == "m") {
            component = Component::from_meters(c.size.width, c.size.depth, c.size.height, c.name);
        } else if(comp_units == "in") {
            component = Component::from_inches(c.size.width, c.size.depth, c.size.height, c.name);
        } else if(comp_units == "mm") {
            component = Component::from_mm(c.size.width, c.size.depth, c.size.height, c.name);
        } else {
            throw std::runtime_error("Invalid component.size units: '" + comp_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        const std::string comp_pos_units = c.position.units.value_or("u");
        if(comp_pos_units == "u") {
            component.set_coords_rack_units(c.position.x, c.position.y, c.position.z);
        } else if(comp_pos_units == "m") {
            component.set_coords_m(c.position.x, c.position.y, c.position.z);
        } else if(comp_pos_units == "in") {
            component.set_coords_in(c.position.x, c.position.y, c.position.z);
        } else if(comp_pos_units == "mm") {
            component.set_coords_mm(c.position.x, c.position.y, c.position.z);
        } else {
            throw std::runtime_error("Invalid component.position units: '" + comp_pos_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        component.set_name(c.name);
        component.set_cp(c.material.cp);
        component.set_rho_solid(c.material.density);
        component.set_k_solid(c.material.k);
        component.set_watts(c.watts);
        for(const InternalRegionInput& i : c.internal_regions) {
            component.add_region(build_internal_region(i));
        }
        component.order_internal_regions();
        components.push_back(component);
    }

    for(const FanInput& f : model.fans) {
        Fan fan = Fan();
        const std::string fan_units = f.position.units.value_or("u");
        if(fan_units == "u") {
            fan.set_center_rack_units(f.position.x, f.position.y, f.position.z);
        } else if(fan_units == "m") {
            fan.set_center_meters(f.position.x, f.position.y, f.position.z);
        } else if(fan_units == "in") {
            fan.set_center_inches(f.position.x, f.position.y, f.position.z);
        } else if(fan_units == "mm") {
            fan.set_center_mm(f.position.x, f.position.y, f.position.z);
        } else {
            throw std::runtime_error("Invalid fan.position units: '" + fan_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        if(f.shape == FanShape::Circular) {
            FlowType flow = FlowType::Exhaust;
            if(f.flow_type == FanFlowType::Intake) flow = FlowType::Intake;
            const std::string fan_d_units = f.diameter_units.value_or("u");
            if(fan_d_units == "u") {
                fan.set_diameter_rack_units(*f.diameter);
            } else if(fan_d_units == "m") {
                fan.set_diameter_meters(*f.diameter);
            } else if(fan_d_units == "in") {
                fan.set_diameter_inches(*f.diameter);
            } else if(fan_d_units == "mm") {
                fan.set_diameter_mm(*f.diameter);
            } else {
                throw std::runtime_error("Invalid fan diameter_units: '" + fan_d_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            fan.set_name(f.name);
            fan.set_cfm(f.cfm);
            fan.set_velocity_dir({f.direction.x, f.direction.y, f.direction.z});
            fan.set_type(flow);
            fan.set_shape(ShapeType::Circular);
        }
        if(f.shape == FanShape::Rectangular) {
        FlowType flow = FlowType::Exhaust;
        if(f.flow_type == FanFlowType::Intake) flow = FlowType::Intake;
            fan.set_name(f.name);
            const std::string fan_pos_units = f.size->units.value_or("u");
            if(fan_pos_units == "u") {
                fan.set_size_rack_units(f.size->width, f.size->depth, f.size->height);
            } else if(fan_pos_units == "m") {
                fan.set_size_meters(f.size->width, f.size->depth, f.size->height);
            } else if(fan_pos_units == "in") {
                fan.set_size_inches(f.size->width, f.size->depth, f.size->height);
            } else if(fan_pos_units == "mm") {
                fan.set_size_mm(f.size->width, f.size->depth, f.size->height);
            } else {
                throw std::runtime_error("Invalid fan.size units: '" + fan_pos_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            fan.set_cfm(f.cfm);
            fan.set_velocity_dir({f.direction.x, f.direction.y, f.direction.z});
            fan.set_type(flow);
            fan.set_shape(ShapeType::Rectangular);
        }
        if (f.curve_name.has_value()) {
            auto it = fan_curve_library.find(*f.curve_name);
            if (it == fan_curve_library.end()) {
                throw std::runtime_error(
                    "Fan '" + f.name + "' references unknown curve '" + *f.curve_name + "'");
            }
            const FanCurveInput& curve = it->second;
            fan.set_curve(curve.a, curve.b, curve.c, curve.rho_rated);
        }
        fans.push_back(fan);
    }

    for(const VentInput& v : model.vents) {
        Vent vent = Vent();
        const std::string vent_units = v.position.units.value_or("u");
        if(vent_units == "u") {
            vent.set_center_rack_units(v.position.x, v.position.y, v.position.z);
        } else if(vent_units == "m") {
            vent.set_center_meters(v.position.x, v.position.y, v.position.z);
        } else if(vent_units == "in") {
            vent.set_center_inches(v.position.x, v.position.y, v.position.z);
        } else if(vent_units == "mm") {
            vent.set_center_mm(v.position.x, v.position.y, v.position.z);
        } else {
            throw std::runtime_error("Invalid vent.position units: '" + vent_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        if(v.shape == VentShape::Circular) {
            const std::string vent_d_units = v.diameter_units.value_or("u");
            if(vent_d_units == "u") {
                vent.set_diameter_rack_units(*v.diameter);
            } else if(vent_d_units == "m") {
                vent.set_diameter_meters(*v.diameter);
            } else if(vent_d_units == "in") {
                vent.set_diameter_inches(*v.diameter);
            } else if(vent_d_units == "mm") {
                vent.set_diameter_mm(*v.diameter);
            } else {
                throw std::runtime_error("Invalid vent diameter_units: '" + vent_d_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            vent.set_cd(v.cd);
            vent.set_direction(v.direction.x, v.direction.y, v.direction.z);
            vent.set_name(v.name);
            vent.set_free_area_ratio(v.free_area_ratio);
            vent.set_shape(VentShapeType::Circular);
        }
        if(v.shape == VentShape::Rectangular) {
            const std::string vent_s_units = v.size->units.value_or("u");
            if(vent_s_units == "u") {
                vent.set_size_rack_units(v.size->width, v.size->depth, v.size->height);
            } else if(vent_s_units == "m") {
                vent.set_size_meters(v.size->width, v.size->depth, v.size->height);
            } else if(vent_s_units == "in") {
                vent.set_size_inches(v.size->width, v.size->depth, v.size->height);
            } else if(vent_s_units == "mm") {
                vent.set_size_mm(v.size->width, v.size->depth, v.size->height);
            } else {
                throw std::runtime_error("Invalid vent.size units: '" + vent_s_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            vent.set_cd(v.cd);
            vent.set_name(v.name);
            vent.set_free_area_ratio(v.free_area_ratio);
            vent.set_direction(v.direction.x, v.direction.y, v.direction.z);
            // vent.set_shape(ShapeType::Rectangular);
        }
        vents.push_back(vent);
    }

    // Single geometry-level validation gate. Throws with a descriptive
    // message if any two components/fans/vents overlap in real-world space,
    // independent of mesh resolution. Mesh stamping below trusts this.
    CollisionChecker::check_all(components, fans, vents);

    for(const Component& component : components) {
        mesh.stamp_component(component);
        grapher.add_component(component);
    }
    for(const Fan& fan : fans) {
        mesh.stamp_fan(fan);
        grapher.add_fan(fan);
    }
    for(const Vent& vent : vents) {
        mesh.stamp_vent(vent);
        grapher.add_vent(vent);
    }

    grapher.stamp_components();
    grapher.stamp_fans();
    grapher.stamp_vents();
    grapher.export_to_file("output.txt");
    if(model.flow_solver.enable_flow_solver) {
        FlowSolver flow_solver(mesh, *model.flow_solver.resistivity, *model.flow_solver.tolerance, *model.flow_solver.max_iterations, 
                            *model.flow_solver.sor_omega, *model.flow_solver.max_outer_iters, *model.flow_solver.flow_tolerance);
        flow_solver.solve(); // pre populate all velocity cells
    }
    int update_flow_interval = model.flow_solver.enable_flow_solver
        ? model.simulation.update_flow_interval.value_or(1)
        : -1;

    Solver solver(mesh, model.simulation.dt, model.simulation.duration, false,
                model.simulation.output_interval,
                update_flow_interval,
                *model.flow_solver.resistivity, *model.flow_solver.tolerance,
                *model.flow_solver.max_iterations, *model.flow_solver.sor_omega,
                *model.flow_solver.max_outer_iters, *model.flow_solver.flow_tolerance);
    solver.solve();
    }
};



struct ComponentLoader {
    ComponentInput model;
    std::unordered_map<std::string, ComponentInput> component_template_cache; 

    ComponentLoader() = default;


    void load_component(const std::filesystem::path& component_path) 
    {
        try {
            const toml::table root = toml::parse_file(component_path.string());

            model.name = root["name"].value<std::string>().value_or(component_path.stem().string());
            PositionInput pos;
            model = parse_component(root, "component laoder", true, pos);
    
        } catch(const toml::parse_error& error) {
            throw std::runtime_error("Failed to parse model file '" + component_path.string() + "': " + std::string(error.description()));
        }
    }

    const ComponentInput& get_component_template(const std::string& path, PositionInput pos) {
        auto it = component_template_cache.find(path);
        if (it != component_template_cache.end()) return it->second;
        auto [inserted, ok] = component_template_cache.emplace(path, load_component_template(path, pos));
        return inserted->second;
    }

    void run() 
    {
        
        ComponentGrapher grapher = ComponentGrapher();

        Component component;
        if (model.template_path.has_value()) {
            const ComponentInput& tmpl = get_component_template(*model.template_path, model.position);
            model.name = tmpl.name;
            model.size = tmpl.size;
            model.material = tmpl.material;
            model.watts = tmpl.watts;
            model.internal_regions = tmpl.internal_regions;
        }
        const std::string comp_units = model.size.units.value_or("u");
        if(comp_units == "u") {
            component = Component::from_rack_units(model.size.width, model.size.depth, model.size.height, model.name);
        } else if(comp_units == "m") {
            component = Component::from_meters(model.size.width, model.size.depth, model.size.height, model.name);
        } else if(comp_units == "in") {
            component = Component::from_inches(model.size.width, model.size.depth, model.size.height, model.name);
        } else if(comp_units == "mm") {
            component = Component::from_mm(model.size.width, model.size.depth, model.size.height, model.name);
        } else {
            throw std::runtime_error("Invalid component.size units: '" + comp_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        const std::string comp_pos_units = model.position.units.value_or("u");
        if(comp_pos_units == "u") {
            component.set_coords_rack_units(model.position.x, model.position.y, model.position.z);
        } else if(comp_pos_units == "m") {
            component.set_coords_m(model.position.x, model.position.y, model.position.z);
        } else if(comp_pos_units == "in") {
            component.set_coords_in(model.position.x, model.position.y, model.position.z);
        } else if(comp_pos_units == "mm") {
            component.set_coords_mm(model.position.x, model.position.y, model.position.z);
        } else {
            throw std::runtime_error("Invalid component.position units: '" + comp_pos_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
        }
        component.set_name(model.name);
        component.set_cp(model.material.cp);
        component.set_rho_solid(model.material.density);
        component.set_k_solid(model.material.k);
        component.set_watts(model.watts);
        for(const InternalRegionInput& i : model.internal_regions) {
            component.add_region(build_internal_region(i));
        }
        component.order_internal_regions();
        grapher.add_component(component);
        grapher.export_to_file("output.txt");
    }
};

#endif
#ifndef MODEL_LOADER_HPP
#define MODEL_LOADER_HPP

#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <string>

#include "input_types.hpp"
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

        internal_region.name = require_value<std::string>(table["name"], context + ".name");
        internal_region.local_position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        internal_region.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        internal_region.material = parse_material(require_table(table["material"], context + ".material"), context + ".material");
        internal_region.watts = require_value<double>(table["watts"], context + ".watts");
        internal_region.state = parse_region_state(require_value<std::string>(table["state"], context + ".state"));

        return internal_region;
    }

    ComponentInput parse_component(const toml::table& table, const std::string& context) {
        ComponentInput component;

        component.name = require_value<std::string>(table["name"], context + ".name");
        component.position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        component.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        component.material = parse_material(require_table(table["material"], context + ".material"), context + ".material");
        component.watts = require_value<double>(table["watts"], context + ".watts");

        const toml::array* internal_regions = table["internal_regions"].as_array();

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
}

struct ModelLoader {
    ModelInput model;
    std::unordered_map<std::string, FanCurveInput> fan_curve_library;

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

    for(const ComponentInput& c : model.components) {
        const std::string comp_units = c.size.units.value_or("u");
        Component component;
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
        component.set_cp(c.material.cp);
        component.set_rho_solid(c.material.density);
        component.set_k_solid(c.material.k);
        component.set_watts(c.watts);
        for(const InternalRegionInput& i : c.internal_regions) {
            InternalRegion internal_region = InternalRegion();
            const std::string int_units = i.local_position.units.value_or("u");
            if(int_units == "u") {
                internal_region.set_local_position_rack_units(i.local_position.x, i.local_position.y, i.local_position.z);
            } else if(int_units == "m") {
                internal_region.set_local_position_meters(i.local_position.x, i.local_position.y, i.local_position.z);
            } else if(int_units == "in") {
                internal_region.set_local_position_inches(i.local_position.x, i.local_position.y, i.local_position.z);
            } else if(int_units == "mm") {
                internal_region.set_local_position_mm(i.local_position.x, i.local_position.y, i.local_position.z);
            } else {
                throw std::runtime_error("Invalid internal_region.position units: '" + int_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
            }
            if(i.state == RegionState::Solid) {
                const std::string int_s_units = i.size.units.value_or("u");
                if(int_s_units == "u") {
                    internal_region.set_size_rack_units(i.size.width, i.size.depth, i.size.height);
                } else if(int_s_units == "m") {
                    internal_region.set_size_meters(i.size.width, i.size.depth, i.size.height);
                } else if(int_s_units == "in") {
                    internal_region.set_size_inches(i.size.width, i.size.depth, i.size.height);
                } else if(int_s_units == "mm") {
                    internal_region.set_size_mm(i.size.width, i.size.depth, i.size.height);
                } else {
                    throw std::runtime_error("Invalid internal_region.size units: '" + int_s_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
                }
                internal_region.set_region_type(RegionType::HeatSource);
                internal_region.set_name(i.name);
                internal_region.set_cp(i.material.cp);
                internal_region.set_rho(i.material.density);
                internal_region.set_k(i.material.k);
                internal_region.set_watts(i.watts);
            }
            if(i.state == RegionState::Air) {
                const std::string int_s_units = i.local_position.units.value_or("u");
                if(int_s_units == "u") {
                    internal_region.set_size_rack_units(i.size.width, i.size.depth, i.size.height);
                } else if(int_s_units == "m") {
                    internal_region.set_size_meters(i.size.width, i.size.depth, i.size.height);
                } else if(int_s_units == "in") {
                    internal_region.set_size_inches(i.size.width, i.size.depth, i.size.height);
                } else if(int_s_units == "mm") {
                    internal_region.set_size_mm(i.size.width, i.size.depth, i.size.height);
                } else {
                    throw std::runtime_error("Invalid internal_region.size units: '" + int_s_units + "'. Supported values are 'u', 'm', 'in', and 'mm'.");
                }
                internal_region.set_region_type(RegionType::Air);
                internal_region.set_name(i.name);
            }
            // if(i.state == RegionState::Fan) {
            //   internal_region.set_region_type(RegionType::Fan);
            //   internal_region.set_name(i.name);
            //   internal_region.set_size({i.size.width, i.size.depth, i.size.height});
            // }
            // if(i.state == RegionState::Vent) {
            //   internal_region.set_region_type(RegionType::Vent);
            //   internal_region.set_name(i.name);
            //   internal_region.set_size({i.size.width, i.size.depth, i.size.height});
            // }
            component.add_region(internal_region);
        }
        component.order_internal_regions();
        mesh.stamp_component(component);
        grapher.add_component(component);
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
        mesh.stamp_fan(fan);
        grapher.add_fan(fan);
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

#endif
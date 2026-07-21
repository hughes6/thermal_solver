#ifndef MODEL_LOADER_HPP
#define MODEL_LOADER_HPP

#include <filesystem>
#include <iostream>
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
        return {
            require_value<double>(table["x"], context + ".x"),
            require_value<double>(table["y"], context + ".y"),
            require_value<double>(table["z"], context + ".z")
        };
    }

    DirectionInput parse_direction(const toml::table& table, const std::string& context) {
        return {
            require_value<double>(table["x"], context + ".x"),
            require_value<double>(table["y"], context + ".y"),
            require_value<double>(table["z"], context + ".z")
        };
    }

    SizeInput parse_size(const toml::table& table, const std::string& context) {
        return {
            require_value<double>(table["width"],  context + ".width"),
            require_value<double>(table["depth"],  context + ".depth"),
            require_value<double>(table["height"], context + ".height")
        };
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

        if(fan.shape == FanShape::Rectangular) {
            fan.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        } else {
            fan.diameter = require_value<double>(table["diameter"], context + ".diameter");
        }

        return fan;
    }

    VentInput parse_vent(const toml::table& table, const std::string& context) {
        VentInput vent;

        vent.name = require_value<std::string>(table["name"], context + ".name");
        vent.shape = parse_vent_shape(require_value<std::string>(table["shape"], context + ".shape"));
        vent.position = parse_position(require_table(table["position"], context + ".position"), context + ".position");
        vent.direction = parse_direction(require_table(table["normal"], context + ".direction"), context + ".direction");

        if(vent.shape == VentShape::Rectangular) {
            vent.size = parse_size(require_table(table["size"], context + ".size"), context + ".size");
        } else {
            vent.diameter = require_value<double>(table["diameter"], context + ".diameter");
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

}


// -----------------
// ACTUAL API HERE
// -----------------
ModelInput load_model(const std::filesystem::path& model_path) 
{
    try {
        const toml::table root = toml::parse_file(model_path.string());

        ModelInput model;


        model.name = root["name"].value<std::string>().value_or(model_path.stem().string());

        // ------------------------------------------Simulation------------------------------------------
        const toml::table& simulation = require_table(root["simulation"], "simulation");
        model.simulation.dt = require_value<double>(simulation["dt"], "simulation.dt");
        model.simulation.output_interval = simulation["output_interval"].value<int>().value_or(1);
        model.simulation.enable_flow_solver = simulation["enable_flow_solver"].value<bool>().value_or(false);
        model.simulation.max_timesteps = require_value<double>(simulation["max_timesteps"], "simulation.max_timesteps");
        model.simulation.max_updates = require_value<double>(simulation["max_updates"], "simulation.max_updates");
        model.simulation.max_cell_count = require_value<double>(simulation["max_cell_count"], "simulation.max_cell_count");
        model.simulation.max_megabyte_usage= require_value<double>(simulation["max_megabyte_usage"], "simulation.max_megabyte_usage");

        // ----------------------------------------Environtment------------------------------------------
        const toml::table& environment = require_table(root["environtment"], "environment");
        model.environment.humidity = require_value<double>(environment["dt"], "environtment.humidity");
        model.environment.elevation = require_value<double>(environment["elevation"], "environtment.elevation");
        model.environment.T_ambient = require_value<double>(environment["T_ambient"], "environtment.T_ambient");
        model.environment.cp = require_value<double>(environment["cp"], "environtment.cp");
        model.environment.k = require_value<double>(environment["k"], "environtment.k");
        model.environment.mu = require_value<double>(environment["mu"], "environtment.mu");
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


        return model;

    } catch(const toml::parse_error& error) {
        throw std::runtime_error("Failed to parse model file '" + model_path.string() + "': " + std::string(error.description()));
    }
}

void run() 
{
    
}

#endif

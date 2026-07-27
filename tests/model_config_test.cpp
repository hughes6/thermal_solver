#include <cassert>
#include <cmath>
#include <iostream>

#include "../grapher.hpp"
#include "../input/model_loader.hpp"

int main() {
    ModelLoader loader;
    loader.load_fan_curves("library/fan_curves/fan_curves.toml");
    loader.load_model("library/models/model.toml");

    const ModelInput& model = loader.model;
    assert(model.flow_solver.enable_flow_solver);
    assert(model.flow_solver.pressure_method == "pcg");
    assert(model.mesh.adaptive);
    assert(model.multistage.enabled);
    assert(model.simulation.advection_subcycling);
    assert(model.simulation.advection_cfl_target > 0.0);
    assert(model.simulation.advection_cfl_target <= 1.0);
    assert(model.simulation.max_advection_substeps >= 1);
    assert(model.multistage.coarse_mesh.fine_dx > 0.0);
    assert(model.multistage.coarse_mesh.coarse_dx >=
           model.multistage.coarse_mesh.fine_dx);
    assert(!model.components.empty());
    assert(!model.fans.empty());
    assert(!model.vents.empty());

    std::cout << "model_config_test PASSED\n";
}

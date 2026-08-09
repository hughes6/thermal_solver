#include <cassert>
#include <iostream>

#include "grapher.hpp"
#include "input/model_loader.hpp"

int main() {
    ComponentLoader loader;
    loader.load_component("library/tests/air_heat_source_component.toml");
    assert(loader.model.internal_regions.size() == 1);
    assert(loader.model.internal_regions[0].state == RegionState::Air);
    assert(loader.model.internal_regions[0].watts == 5.0);
    std::cout << "air_heat_source_parser_test PASSED\n";
}

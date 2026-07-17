#ifndef UNIT_TEST_HPP
#define UNIT_TEST_HPP

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "cell.hpp"
#include "component.hpp"
#include "environment.hpp"
#include "fan.hpp"
#include "flow_solver.hpp"
#include "grapher.hpp"
#include "mesh.hpp"
#include "rack.hpp"
#include "solver.hpp"
#include "unit_test.hpp"
#include "vent.hpp"


class UnitTest {
private:
    double dx = 0.01;
    double dy = 0.01;
    double dz = 0.01;
    double sim_length = 20; 
    double dt = 0.1;
    double mu = 1.8e-5; 
    double pr = 0.71; 
    double T = 20.0;

public:
    UnitTest() {}

    void run() { 
        test1();
        test2();
        // test3();
    }

    void test1() {
        Rack rack = Rack::from_meters(0.1, 0.1, 0.1); 
        rack.set_t(T);    // 68 F
        rack.set_cp(1005); // cp of air
        rack.set_k(0.02587);
        rack.set_rho(1.225);

        Mesh mesh = Mesh().build_mesh(rack, dx, dy, dz, mu, pr);

        FlowSolver flow_solver(mesh);
        flow_solver.solve();        
        Solver solver(mesh, dt, sim_length);
        solver.solve();
        std::array<bool, 3> result = test_all_mesh_values(mesh, T, 0.0, 0.0);
        
        if(!result[0]) {
            std::string s = "Test 1 - All Temp = " + std::to_string(T) + " failed.";
            throw std::runtime_error(s);
        } 
        if(!result[1]) {
            throw std::runtime_error("Test 1 - All velocity = 0.0 failed.");
        } 
        if(!result[2]) {
            throw std::runtime_error("Test 1 - All Qdot = 0.0 failed.");
        }  
    }

    void test2() {
        Rack rack = Rack::from_meters(0.1, 0.1, 0.1); 
        rack.set_t(40.0);    // 68 F
        rack.set_cp(1005); // cp of air
        rack.set_k(0.02587);
        rack.set_rho(1.225);

        Mesh mesh = Mesh().build_mesh(rack, 0.05, 0.05, 0.05, mu, pr);

        FlowSolver flow_solver(mesh);
        flow_solver.solve();        
        Solver solver(mesh, dt, sim_length);
        solver.solve();
        std::array<bool, 3> result = test_all_mesh_values(mesh, 40.0, 0.0, 0.0);
        if(!result[0]) {
            std::string s = "Test 2 - All Temp = 40.0 failed.";
            throw std::runtime_error(s);
        } 
    }

    std::array<bool, 3> test_all_mesh_values(Mesh& m, double T, double v, double qdot) {
        bool T_passed = true;
        bool qdot_passed = true;
        bool v_passed = true;

        for(int x = 0; x < m.get_nx(); x++) {
            for(int y = 0; y < m.get_ny(); y++) {
                for(int z = 0; z < m.get_nz(); z++) {
                    if(!m.in_bounds(x, y, z)) continue;
                    if(m.at(x, y, z).get_T() != T) T_passed = false;
                    if(m.at(x, y, z).get_vx() != v) v_passed = false;
                    if(m.at(x, y, z).get_vy() != v) v_passed = false;
                    if(m.at(x, y, z).get_vz() != v) v_passed = false;
                    if(m.at(x, y, z).get_qdot() != qdot) qdot_passed = false;
                }
            }
        }
        return {T_passed, v_passed, qdot_passed};
    }

    // void test3() {
    //     Rack rack = Rack::from_meters(0.1, 0.1, 0.1); 
    //     rack.set_t(20.0);    // 68 F
    //     rack.set_cp(1005); // cp of air
    //     rack.set_k(0.02587);
    //     rack.set_rho(1.225);

    //     Mesh mesh = Mesh().build_mesh(rack, 0.1, 0.05, 0.1, mu, pr);

    //     FlowSolver flow_solver(mesh);
    //     flow_solver.solve();  
        
        
    //     Solver solver(mesh, dt, sim_length);
    //     solver.solve();

    // }

};

#endif

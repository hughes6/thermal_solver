#include "fvCFD.H"
#include "turbulentFluidThermoModel.H"
#include "rhoReactionThermo.H"
#include "CombustionModel.H"
#include "fixedGradientFvPatchFields.H"
#include "regionProperties.H"
#include "compressibleCourantNo.H"
#include "solidRegionDiffNo.H"
#include "solidThermo.H"
#include "radiationModel.H"
#include "fvOptions.H"
#include "loopControl.H"
#include "pressureControl.H"
#include "generated/solverBuildAttestation.H"

#include <cstring>
#include <iostream>

// This policy string is part of both the ordinary solver log and the
// no-case runtime attestation response.  The build wrapper binds that
// response to the exact repository-local solver inputs compiled into the
// executable. OpenFOAM/toolchain dependencies are identified separately and
// are not represented by this project-source digest.
static const char modePolicyMarker[] =
    "THERMAL_SIM_SEMIFROZEN_MODE_POLICY_V1";
static const char runtimeAttestationSchema[] =
    "THERMAL_SIM_SOLVER_ATTESTATION_V1";

int main(int argc, char *argv[])
{
    bool runtimeAttestationRequested = false;
    for (int argi = 1; argi < argc; ++argi)
    {
        if
        (
            std::strcmp(argv[argi], "--thermal-sim-attest") == 0
        )
        {
            runtimeAttestationRequested = true;
        }
    }

    if (runtimeAttestationRequested)
    {
        // Attestation is deliberately exclusive and precedes every OpenFOAM
        // argument, case, mesh, field, and time access.  Mixing it with a
        // solver invocation is an error, not a best-effort diagnostic.
        if
        (
            argc != 2
         || std::strcmp(argv[1], "--thermal-sim-attest") != 0
        )
        {
            std::cerr
                << "--thermal-sim-attest must be the only argument"
                << std::endl;
            return 64;
        }

        std::cout
            << runtimeAttestationSchema
            << " solver=semiFrozenChtMultiRegionFoam"
            << " project_source_sha256="
            << THERMAL_SIM_SOLVER_PROJECT_SOURCE_SHA256
            << " policy=" << modePolicyMarker
            << " foam_api=" << THERMAL_SIM_SOLVER_FOAM_API
            << " wm_project_version="
            << THERMAL_SIM_SOLVER_WM_PROJECT_VERSION
            << " wm_options=" << THERMAL_SIM_SOLVER_WM_OPTIONS
            << std::endl;
        return 0;
    }

    argList::addNote
    (
        "Pressure-corrected semi-frozen transient conjugate heat-transfer "
        "solver"
    );

    #define NO_CONTROL
    #define CREATE_MESH createMeshesPostProcess.H
    #include "postProcess.H"

    #include "addCheckCaseOptions.H"
    #include "setRootCaseLists.H"

    // The generated case runner requires this marker in the deployed
    // executable's exclusive runtime attestation before it acquires the case
    // lock or changes any case state. Printing it also records the accepted
    // solver policy in every ordinary solver log.
    Info<< "semiFrozenChtMultiRegionFoam mode policy: "
        << modePolicyMarker << nl << endl;

    #include "createTime.H"
    #include "createMeshes.H"
    #include "createFields.H"
    #include "initContinuityErrs.H"
    #include "createTimeControls.H"
    #include "readSolidTimeControls.H"
    #include "compressibleMultiRegionCourantNo.H"
    #include "solidRegionDiffusionNo.H"
    #include "setInitialMultiRegionDeltaT.H"
    #include "createCoupledRegions.H"

    auto resolveThermalOnlyFlowMode = [&]()
    {
        bool foundFluidRegion = false;
        bool requestedThermalOnlyFlow = false;
        forAll(fluidRegions, i)
        {
            fvMesh& mesh = fluidRegions[i];
            #include "readFluidMultiRegionPIMPLEControls.H"
            const bool regionThermalOnlyFlow =
                pimple.getOrDefault("thermalOnlyFlow", false);
            const bool regionIsothermalAirflow =
                pimple.getOrDefault("isothermalAirflow", false);

            if (regionThermalOnlyFlow && regionIsothermalAirflow)
            {
                FatalErrorInFunction
                    << "Fluid region " << fluidRegions[i].name()
                    << " enables both thermalOnlyFlow and "
                       "isothermalAirflow; these modes are mutually "
                       "exclusive. Physical time was not advanced."
                    << exit(FatalError);
            }
            if (regionIsothermalAirflow)
            {
                FatalErrorInFunction
                    << "isothermalAirflow is disabled because its previous "
                       "implementation advanced physical time while freezing "
                       "solid and coupled energy fields. Run airflow "
                       "initialization in a separate scratch case with a "
                       "validated time-zero field handoff. Physical time was "
                       "not advanced."
                    << exit(FatalError);
            }
            if (!foundFluidRegion)
            {
                requestedThermalOnlyFlow = regionThermalOnlyFlow;
                foundFluidRegion = true;
            }
            else if (regionThermalOnlyFlow != requestedThermalOnlyFlow)
            {
                FatalErrorInFunction
                    << "Mixed live and thermal-only fluid-region modes are "
                       "unsupported. Physical time was not advanced."
                    << exit(FatalError);
            }
        }

        // Preserve the solver's historical solid-only behavior: an empty
        // fluid list resolves to the ordinary (non-thermal-only) path and can
        // never be mistaken for an all-isothermal initialization.
        return foundFluidRegion && requestedThermalOnlyFlow;
    };

    // Resolve once even for a zero-step/post-check invocation so an invalid
    // isothermal request cannot produce a misleading successful exit.
    const bool invocationThermalOnlyFlow = resolveThermalOnlyFlowMode();

    while (runTime.run())
    {
        #include "readTimeControls.H"
        #include "readSolidTimeControls.H"
        #include "readPIMPLEControls.H"
        #include "compressibleMultiRegionCourantNo.H"
        #include "solidRegionDiffusionNo.H"
        #include "setMultiRegionDeltaT.H"

        // Resolve one uniform fluid mode before advancing physical time and
        // pin it for this solver invocation.  The historical isothermal
        // branch advanced runTime while suppressing all solid/coupled energy
        // work; mixed regional flags could also leave assembled matrices
        // unsolved.  A trustworthy isothermal initializer therefore requires
        // a separate scratch-case/handoff workflow and is deliberately
        // unavailable here until that workflow is implemented and validated.
        const bool requestedThermalOnlyFlow =
            resolveThermalOnlyFlowMode();
        if (requestedThermalOnlyFlow != invocationThermalOnlyFlow)
        {
            FatalErrorInFunction
                << "thermalOnlyFlow changed during one solver invocation. "
                   "Use a separate solver launch for each mode so state and "
                   "provenance remain unambiguous. Physical time was not "
                   "advanced."
                << exit(FatalError);
        }
        const bool thermalOnlyFlowMode = invocationThermalOnlyFlow;

        ++runTime;
        Info<< "Time = " << runTime.timeName() << nl << endl;

        if (nOuterCorr != 1)
        {
            forAll(fluidRegions, i)
            {
                #include "storeOldFluidFields.H"
            }
        }

        for (int oCorr=0; oCorr<nOuterCorr; ++oCorr)
        {
            const bool finalIter = (oCorr == nOuterCorr-1);

            forAll(fluidRegions, i)
            {
                fvMesh& mesh = fluidRegions[i];
                #include "readFluidMultiRegionPIMPLEControls.H"
                #include "setRegionFluidFields.H"
                if (thermalOnlyFlowMode)
                {
                    Info<< "\nSolving thermal-only fluid region "
                        << fluidRegions[i].name() << endl;
                    if (finalIter)
                    {
                        mesh.data().setFinalIteration(true);
                    }
                    #include "EEqn.H"
                    if (finalIter)
                    {
                        mesh.data().setFinalIteration(false);
                    }
                }
                else
                {
                    #include "solveFluid.H"
                }
            }

            forAll(solidRegions, i)
            {
                fvMesh& mesh = solidRegions[i];
                #include "readSolidMultiRegionPIMPLEControls.H"
                #include "setRegionSolidFields.H"
                #include "solveSolid.H"
            }

            if (coupled)
            {
                Info<< "\nSolving energy coupled regions " << endl;
                fvMatrixAssemblyPtr->solve();
                #include "correctThermos.H"

                forAll(fluidRegions, i)
                {
                    fvMesh& mesh = fluidRegions[i];
                    #include "readFluidMultiRegionPIMPLEControls.H"
                    const bool semiFrozenFlow =
                        pimple.getOrDefault("semiFrozenFlow", false);
                    #include "setRegionFluidFields.H"

                    if (thermalOnlyFlowMode)
                    {
                        // Keep the converged velocity and mass-flux operating
                        // point, but update thermodynamic density and the
                        // hydrostatic pressure split as temperature changes.
                        // Absolute pressure remains the last pressure-corrected
                        // airflow solution until the next refresh stage.
                        rho = thermo.rho();
                        p_rgh = p - rho*gh;
                    }
                    else if (!frozenFlow)
                    {
                        Info<< "\nSolving for fluid region "
                            << fluidRegions[i].name() << endl;
                        for (int corr=0; corr<nCorr; corr++)
                        {
                            #include "pEqn.H"
                        }
                        if (!semiFrozenFlow)
                        {
                            turbulence.correct();
                        }
                    }

                    rho = thermo.rho();
                    Info<< "Min/max T:" << min(thermo.T()).value() << ' '
                        << max(thermo.T()).value() << endl;
                }

                fvMatrixAssemblyPtr->clear();
            }

            if (!oCorr && nOuterCorr > 1)
            {
                loopControl looping(runTime, pimple, "energyCoupling");
                while (looping.loop())
                {
                    Info<< nl << looping << nl;
                    forAll(fluidRegions, i)
                    {
                        fvMesh& mesh = fluidRegions[i];
                        #include "readFluidMultiRegionPIMPLEControls.H"
                        #include "setRegionFluidFields.H"
                        frozenFlow = true;
                        #include "solveFluid.H"
                    }
                    forAll(solidRegions, i)
                    {
                        fvMesh& mesh = solidRegions[i];
                        Info<< "\nSolving for solid region "
                            << solidRegions[i].name() << endl;
                        #include "readSolidMultiRegionPIMPLEControls.H"
                        #include "setRegionSolidFields.H"
                        #include "solveSolid.H"
                    }
                    if (coupled)
                    {
                        Info<< "\nSolving energy coupled regions " << endl;
                        fvMatrixAssemblyPtr->solve();
                        #include "correctThermos.H"
                        forAll(fluidRegions, i)
                        {
                            #include "setRegionFluidFields.H"
                            rho = thermo.rho();
                        }
                        fvMatrixAssemblyPtr->clear();
                    }
                }
            }
        }

        runTime.write();
        runTime.printExecutionTime(Info);
    }

    Info<< "End\n" << endl;
    return 0;
}

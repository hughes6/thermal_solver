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

int main(int argc, char *argv[])
{
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

    while (runTime.run())
    {
        #include "readTimeControls.H"
        #include "readSolidTimeControls.H"
        #include "readPIMPLEControls.H"
        #include "compressibleMultiRegionCourantNo.H"
        #include "solidRegionDiffusionNo.H"
        #include "setMultiRegionDeltaT.H"

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
            bool isothermalAirflow = false;

            forAll(fluidRegions, i)
            {
                fvMesh& mesh = fluidRegions[i];
                #include "readFluidMultiRegionPIMPLEControls.H"
                const bool thermalOnlyFlow =
                    pimple.getOrDefault("thermalOnlyFlow", false);
                const bool regionIsothermalAirflow =
                    pimple.getOrDefault("isothermalAirflow", false);
                isothermalAirflow =
                    isothermalAirflow || regionIsothermalAirflow;
                #include "setRegionFluidFields.H"
                if (thermalOnlyFlow)
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
                else if (regionIsothermalAirflow)
                {
                    Info<< "\nSolving isothermal airflow region "
                        << fluidRegions[i].name() << endl;
                    if (finalIter)
                    {
                        mesh.data().setFinalIteration(true);
                    }
                    if (oCorr == 0)
                    {
                        #include "rhoEqn.H"
                    }
                    #include "UEqn.H"
                    #include "YEqn.H"
                    if (!coupled)
                    {
                        for (int corr=0; corr<nCorr; corr++)
                        {
                            #include "pEqn.H"
                        }
                        turbulence.correct();
                        rho = thermo.rho();
                    }
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

            if (!isothermalAirflow)
            {
                forAll(solidRegions, i)
                {
                    fvMesh& mesh = solidRegions[i];
                    #include "readSolidMultiRegionPIMPLEControls.H"
                    #include "setRegionSolidFields.H"
                    #include "solveSolid.H"
                }
            }

            if (coupled)
            {
                if (!isothermalAirflow)
                {
                    Info<< "\nSolving energy coupled regions " << endl;
                    fvMatrixAssemblyPtr->solve();
                    #include "correctThermos.H"
                }

                forAll(fluidRegions, i)
                {
                    fvMesh& mesh = fluidRegions[i];
                    #include "readFluidMultiRegionPIMPLEControls.H"
                    const bool semiFrozenFlow =
                        pimple.getOrDefault("semiFrozenFlow", false);
                    const bool thermalOnlyFlow =
                        pimple.getOrDefault("thermalOnlyFlow", false);
                    const bool regionIsothermalAirflow =
                        pimple.getOrDefault("isothermalAirflow", false);
                    #include "setRegionFluidFields.H"

                    if (thermalOnlyFlow)
                    {
                        // Keep the converged velocity and mass-flux operating
                        // point, but update thermodynamic density and the
                        // hydrostatic pressure split as temperature changes.
                        // Absolute pressure remains the last pressure-corrected
                        // airflow solution until the next refresh stage.
                        rho = thermo.rho();
                        p_rgh = p - rho*gh;
                    }
                    else if (regionIsothermalAirflow)
                    {
                        Info<< "\nPressure-correcting isothermal airflow region "
                            << fluidRegions[i].name() << endl;
                        for (int corr=0; corr<nCorr; corr++)
                        {
                            #include "pEqn.H"
                        }
                        turbulence.correct();
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

                if (!isothermalAirflow)
                {
                    fvMatrixAssemblyPtr->clear();
                }
            }

            if (!isothermalAirflow && !oCorr && nOuterCorr > 1)
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

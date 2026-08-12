#include "fvCFD.H"
#include "zeroGradientFvPatchFields.H"
#include "inletOutletFvPatchFields.H"
#include "timeSelector.H"

using namespace Foam;

int main(int argc, char *argv[])
{
    argList::addNote("Solve a bounded steady passive exhaust tracer on fixed flow fields");
    argList::addOption("source-zone", "name", "Cell zone held at tracer concentration one");
    argList::addOption("source-zones", "names", "OpenFOAM wordList of cell zones held at one");
    argList::addOption("field", "name", "Tracer field name (default exhaustTracer)");
    argList::addOption("schmidt", "number", "Turbulent Schmidt number (default 0.7)");
    argList::addOption("iterations", "count", "Maximum iterations (default 500)");
    argList::addOption("tolerance", "number", "Maximum field-change tolerance (default 1e-9)");
    argList::addOption("region", "name", "Mesh region (default fluid)");
    timeSelector::addOptions();

    #include "setRootCase.H"
    #include "createTime.H"
    instantList selectedTimes = timeSelector::select0(runTime, args);
    if (selectedTimes.empty())
    {
        FatalErrorInFunction << "No matching OpenFOAM time selected" << exit(FatalError);
    }
    runTime.setTime(selectedTimes.last(), selectedTimes.size() - 1);

    const word regionName(args.getOrDefault<word>("region", "fluid"));
    wordList sourceNames;
    if (args.found("source-zones"))
    {
        sourceNames = args.get<wordList>("source-zones");
    }
    else
    {
        sourceNames = wordList(1, args.get<word>("source-zone"));
    }
    const word fieldName(args.getOrDefault<word>("field", "exhaustTracer"));
    const scalar schmidt(args.getOrDefault<scalar>("schmidt", 0.7));
    const label maxIterations(args.getOrDefault<label>("iterations", 500));
    const scalar tolerance(args.getOrDefault<scalar>("tolerance", 1e-9));

    if (schmidt <= 0 || maxIterations < 1 || tolerance <= 0)
    {
        FatalErrorInFunction << "schmidt, iterations, and tolerance must be positive"
            << exit(FatalError);
    }

    fvMesh mesh
    (
        IOobject(regionName, runTime.timeName(), runTime, IOobject::MUST_READ)
    );
    Info<< "MESH_SIZE," << mesh.nCells() << ',' << mesh.nFaces() << nl;
    volScalarField rho
    (
        IOobject("rho", runTime.timeName(), mesh, IOobject::MUST_READ, IOobject::NO_WRITE),
        mesh
    );
    surfaceScalarField phi
    (
        IOobject("phi", runTime.timeName(), mesh, IOobject::MUST_READ, IOobject::NO_WRITE),
        mesh
    );
    volScalarField nut
    (
        IOobject("nut", runTime.timeName(), mesh, IOobject::MUST_READ, IOobject::NO_WRITE),
        mesh
    );
    volScalarField tracer
    (
        IOobject(fieldName, runTime.timeName(), mesh, IOobject::READ_IF_PRESENT, IOobject::AUTO_WRITE),
        mesh,
        dimensionedScalar(fieldName, dimless, 0),
        zeroGradientFvPatchScalarField::typeName
    );

    forAll(mesh.boundary(), patchi)
    {
        if (mesh.boundary()[patchi].type() == "patch")
        {
            tracer.boundaryFieldRef().set
            (
                patchi,
                new inletOutletFvPatchScalarField
                (
                    mesh.boundary()[patchi],
                    tracer.internalField()
                )
            );
        }
    }
    tracer.correctBoundaryConditions();

    DynamicList<label> sourceCellBuffer;
    forAll(sourceNames, sourcei)
    {
        const label zoneId = mesh.cellZones().findZoneID(sourceNames[sourcei]);
        if (zoneId < 0)
        {
            FatalErrorInFunction << "Unknown source cellZone '" << sourceNames[sourcei]
                << "'. Valid zones: " << mesh.cellZones().names() << exit(FatalError);
        }
        sourceCellBuffer.append(mesh.cellZones()[zoneId]);
    }
    labelHashSet sourceCellSet(sourceCellBuffer);
    const labelList sourceCells(sourceCellSet.sortedToc());
    scalarField sourceValues(sourceCells.size(), 1.0);

    volScalarField gamma
    (
        IOobject("tracerDiffusivity", runTime.timeName(), mesh, IOobject::NO_READ, IOobject::NO_WRITE),
        rho*max(nut/schmidt, dimensionedScalar("minimumNut", nut.dimensions(), SMALL))
    );

    scalar change = GREAT;
    label iteration = 0;
    for (; iteration < maxIterations && change > tolerance; ++iteration)
    {
        const scalarField previous(tracer.primitiveField());
        fvScalarMatrix tracerEqn
        (
            fvm::div(phi, tracer, "div(phi,h)")
          - fvm::laplacian(gamma, tracer, "laplacian(tracerDiffusivity,tracer)")
        );
        tracerEqn.setValues(sourceCells, sourceValues);
        tracerEqn.solve();
        tracer.max(0.0);
        tracer.min(1.0);
        tracer.correctBoundaryConditions();
        change = gMax(mag(tracer.primitiveField() - previous));
        Info<< "Tracer iteration " << iteration + 1 << ": max change " << change << nl;
    }

    tracer.write();
    const surfaceScalarField faceTracer(fvc::interpolate(tracer));
    forAll(mesh.cellZones(), zonei)
    {
        const cellZone& zone = mesh.cellZones()[zonei];
        boolList inZone(mesh.nCells(), false);
        forAll(zone, i)
        {
            inZone[zone[i]] = true;
        }
        scalar weighted = 0;
        scalar volume = 0;
        forAll(zone, i)
        {
            const label celli = zone[i];
            weighted += mesh.V()[celli]*tracer[celli];
            volume += mesh.V()[celli];
        }
        reduce(weighted, sumOp<scalar>());
        reduce(volume, sumOp<scalar>());
        Info<< "ZONE_AVERAGE," << zone.name() << ','
            << (volume > VSMALL ? weighted/volume : 0) << nl;

        scalar incomingMass = 0;
        scalar incomingTracerMass = 0;
        const labelUList& owner = mesh.faceOwner();
        const labelUList& neighbour = mesh.faceNeighbour();
        forAll(neighbour, facei)
        {
            if (inZone[owner[facei]] == inZone[neighbour[facei]])
            {
                continue;
            }
            const scalar flux = phi[facei];
            const bool entersZone =
                (inZone[neighbour[facei]] && flux > 0)
             || (inZone[owner[facei]] && flux < 0);
            if (entersZone)
            {
                incomingMass += mag(flux);
                incomingTracerMass += mag(flux)*faceTracer[facei];
            }
        }
        forAll(mesh.boundary(), patchi)
        {
            const fvPatch& patch = mesh.boundary()[patchi];
            forAll(patch, patchFacei)
            {
                const label celli = patch.faceCells()[patchFacei];
                const scalar flux = phi.boundaryField()[patchi][patchFacei];
                if (inZone[celli] && flux < 0)
                {
                    incomingMass += mag(flux);
                    incomingTracerMass +=
                        mag(flux)*faceTracer.boundaryField()[patchi][patchFacei];
                }
            }
        }
        reduce(incomingMass, sumOp<scalar>());
        reduce(incomingTracerMass, sumOp<scalar>());
        Info<< "ZONE_MASS_INLET," << zone.name() << ','
            << (incomingMass > VSMALL ? incomingTracerMass/incomingMass : 0)
            << ',' << incomingMass << nl;
    }
    Info<< "Source zones: " << sourceNames << nl
        << "Iterations: " << iteration << nl
        << "Final max change: " << change << nl
        << "Tracer min/max: " << gMin(tracer) << ' ' << gMax(tracer) << nl
        << "End" << endl;
    return 0;
}

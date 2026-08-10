PROJECT: Zuken E3.series Database Editor Cable Automation Research

GOAL
====
I need to reverse-engineer and implement a Python automation workflow for Zuken E3.series Database Editor.

I am NOT trying to automate a normal E3 project.

I am specifically automating Zuken Database Editor through its Windows COM interface:

    import win32com.client as win32
    app = win32.Dispatch("CT.DBEApplication")

The eventual goal is to programmatically create cable definitions in the Zuken E3 library.

The finished Python workflow needs to be able to:

1. Create a new cable component in Database Editor.
2. Select/place/reference Connector A from the existing Zuken component database.
3. Select/place/reference Connector B.
4. Define the individual cable conductors/wires.
5. Assign wire properties/types where applicable.
6. Assign one end of each wire to a specific pin on Connector A.
7. Assign the other end to a specific pin on Connector B.
8. Support arbitrary mappings, for example:

       Connector A       Connector B
           Pin 1 ---------- Pin 1
           Pin 2 ---------- Pin 2
           Pin 3 ---------- Pin 7
           Pin 4 ---------- Pin 12

9. Save the completed cable definition into Database Editor.
10. Eventually expose this through a clean Python API so that a cable can be defined entirely from structured input data.

IMPORTANT SAFETY CONSTRAINT
===========================
The current database appears to be PRODUCTION.

SQL Server:
    sql-zuken-prd

Database:
    zuken_e3library

Provider:
    SQLOLEDB

DO NOT write directly to this SQL database.

SQL access during research must be READ ONLY.

Do NOT execute:

    INSERT
    UPDATE
    DELETE
    MERGE
    DROP
    ALTER
    TRUNCATE

Do not execute unknown stored procedures.

The SQL database should initially be used only to understand how Zuken stores cable/component/wire/pin relationships.

The preferred final implementation is through the supported Zuken DBE COM API, NOT direct SQL manipulation.


============================================================
WHAT HAS ALREADY BEEN DISCOVERED
============================================================

Python successfully connects with:

    import win32com.client as win32
    app = win32.Dispatch("CT.DBEApplication")

The COM object is a dynamic CDispatch.

It does NOT expose:

    _olerepr_

Therefore COM introspection should use:

    obj._oleobj_.GetTypeInfo()

instead.


KNOWN DBE APPLICATION METHODS
=============================

Methods discovered include:

    CreateDbeComponentObject
    CreateDbeModelObject
    CreateDbeModelPinObject
    CreateDbeNodeObject
    CreateDbeSlotObject
    CreateDbeSymbolObject
    CreateDbeAttributeObject

    NewComponent
    NewComponentWithPreconditions

    EditComponent
    EditComponentWithPreconditions

    Save

    GetComponentIds
    GetComponentList
    GetComponentDatabase
    GetComponentDatabaseTableSchema

    GetActiveComponentId

    SelectComponentFromTable

    GetModelIds
    GetModelList

    IsCable
    IsWire

There may be many more methods. Enumerate the complete typelib rather than relying on this list.


KNOWN METHOD SIGNATURES
=======================

COM type information revealed:

    NewComponent(
        name,
        version,
        baseName,
        baseVersion,
        flags
    )

and:

    EditComponent(
        name,
        version
    )

and:

    NewComponentWithPreconditions(
        name,
        version,
        baseName,
        baseVersion,
        preconditions,
        flags
    )

and:

    EditComponentWithPreconditions(
        name,
        version,
        preconditions,
        flags
    )

and:

    SelectComponentFromTable(
        ComponentName,
        ComponentVersion
    )


KNOWN DBE COMPONENT INTERFACE
=============================

DbeComponentObject exposes methods including:

    GetId()
    SetId(id)

    Save()
    Remove(save_changes)

    GetAttributeIds(ids, end, attnam)
    AddAttributeValue(name, value, end)

    GetName()
    GetVersion()

    GetSubType()
    SetSubType(subtype)

    GetComponentType()

    SetModelName(ModelName, flags)
    GetModelName(flags)

IMPORTANT:

AddRef, Release, and QueryInterface are standard COM IUnknown methods.

Do NOT interpret AddRef as a Zuken cable relationship method.


KNOWN EXISTING CABLE INFORMATION
================================

A manually created/opened cable produced approximately:

    Active component ID = 14237

    Name = manually assigned cable name

    ComponentType = 7

    Subtype = 0

    ModelName = None

GetAttributeIds returned approximately:

    (10, (...10 IDs...))

The exact structure still needs investigation.

IMPORTANT:

SetId(14237) did NOT successfully bind a newly created DbeComponentObject to the existing cable.

Do not assume internal component IDs are the intended public API mechanism.

The DBE API appears to rely heavily on:

    component name
    component version

through methods such as:

    EditComponent(name, version)
    SelectComponentFromTable(name, version)

Investigate these instead.


KNOWN MODEL OBJECT
==================

DbeModelObject exposes methods including:

    GetPinIds
    GetSlotIds
    PlaceSymbol
    SetId
    Save

and others.


KNOWN MODEL PIN OBJECT
======================

DbeModelPinObject exposes methods including:

    GetCrimpingRules
    GetPinProperties
    GetRoutingOffset

    SetCrimpingRules
    SetId
    SetPinProperties
    SetRoutingOffset

The full method signatures need to be extracted from COM type information.


KNOWN NODE OBJECT
=================

DbeNodeObject exposes methods including things such as:

    GetDirection
    GetPosition
    GetTextIds
    HasPassWires
    IsBusPin
    IsBusbarPin
    SetId

The presence of HasPassWires may or may not be relevant to cable connectivity.

Investigate rather than assume.


KNOWN DATABASE INFORMATION
==========================

GetActualDatabase() returned:

    zuken_e3library

GetComponentDatabase() returned a long connection/provider string.

Relevant portions include:

    Data Source = sql-zuken-prd
    Initial Catalog = zuken_e3library
    Provider = SQLOLEDB

GetComponentDatabaseTableSchema() returned:

    dbo

That method therefore appears to return the SQL schema NAME, not a description of the tables.

GetDefinedDatabases() returned three configured entries/databases/locations.

Again:

DO NOT MODIFY sql-zuken-prd DIRECTLY.


============================================================
PHASE 1 — BUILD A GENERAL COM INTROSPECTION UTILITY
============================================================

Create a Python research script.

Start with:

    import win32com.client as win32

    app = win32.Dispatch("CT.DBEApplication")


Implement:

    def dump_com(obj, title):
        print("\n" + "=" * 100)
        print(title)
        print("=" * 100)

        try:
            ti = obj._oleobj_.GetTypeInfo()
            ta = ti.GetTypeAttr()

            count = ta[6]

            for i in range(count):
                try:
                    fd = ti.GetFuncDesc(i)
                    memid = fd[0]
                    names = ti.GetNames(memid)

                    print(names)

                except Exception as e:
                    print("FUNC ERROR:", e)

        except Exception as e:
            print("TYPEINFO ERROR:", repr(e))


Use it against:

    dump_com(app, "DBE APPLICATION")


Then attempt all known factories:

    factories = {
        "COMPONENT": "CreateDbeComponentObject",
        "MODEL": "CreateDbeModelObject",
        "MODEL PIN": "CreateDbeModelPinObject",
        "NODE": "CreateDbeNodeObject",
        "SLOT": "CreateDbeSlotObject",
        "ATTRIBUTE": "CreateDbeAttributeObject",
        "SYMBOL": "CreateDbeSymbolObject",
        "GRAPH": "CreateDbeGraphObject",
        "TEXT": "CreateDbeTextObject",
        "JOB": "CreateDbeJobObject",
    }

    for title, method in factories.items():

        try:
            obj = getattr(app, method)()
            dump_com(obj, title)

        except Exception as e:
            print(title, "FAILED:", repr(e))


Do not fail the entire script if one factory does not exist.


============================================================
PHASE 2 — SEARCH COM METHODS FOR CABLE FUNCTIONALITY
============================================================

Search the DBE application type information for anything related to:

    cable
    wire
    core
    conductor
    connection
    connector
    pin
    terminal
    device
    component
    assignment
    end
    from
    to
    harness
    part


Example:

    keywords = [
        "cable",
        "wire",
        "core",
        "conductor",
        "connect",
        "connector",
        "pin",
        "terminal",
        "device",
        "component",
        "assign",
        "end",
        "harness",
        "part",
    ]

    ti = app._oleobj_.GetTypeInfo()
    ta = ti.GetTypeAttr()

    for i in range(ta[6]):

        try:
            fd = ti.GetFuncDesc(i)
            names = ti.GetNames(fd[0])

            text = " ".join(str(x) for x in names).lower()

            if any(k in text for k in keywords):
                print(names)

        except:
            pass


Save this output to a text file as well.


============================================================
PHASE 3 — ENUMERATE THE ENTIRE REGISTERED TYPE LIBRARY
============================================================

This is extremely important.

Do NOT assume that every useful interface is exposed through an obvious:

    CreateDbeXXXXObject()

method.

Get the containing typelib:

    ti = app._oleobj_.GetTypeInfo()

    typelib, index = ti.GetContainingTypeLib()

    print("TYPELIB:", typelib)
    print("INDEX:", index)

    attr = typelib.GetLibAttr()

    print("LIB ATTR:", attr)


Then enumerate EVERY registered type:

    count = typelib.GetTypeInfoCount()

    print("TYPE COUNT:", count)

    for i in range(count):

        try:
            info = typelib.GetTypeInfo(i)
            doc = typelib.GetDocumentation(i)

            print(i, doc)

        except Exception as e:
            print(i, "ERR", e)


Search those interface names for:

    cable
    wire
    core
    conductor
    connection
    connector
    pin
    harness
    component
    dbe


If interfaces exist with names conceptually resembling:

    DbeCable
    IDbeCable
    DbeWire
    DbeConnection
    CableCore
    Harness
    Connector

investigate those interfaces immediately.


============================================================
PHASE 4 — DUMP EVERY METHOD OF INTERESTING TYPELIB INTERFACES
============================================================

Use:

    keywords = [
        "cable",
        "wire",
        "connection",
        "connector",
        "conductor",
        "core",
        "pin",
        "component",
        "harness",
        "dbe",
    ]

    count = typelib.GetTypeInfoCount()

    for i in range(count):

        try:
            info = typelib.GetTypeInfo(i)
            doc = typelib.GetDocumentation(i)

            name = str(doc[0])

            if not any(k in name.lower() for k in keywords):
                continue

            print("\n")
            print("#" * 90)
            print("TYPE:", i, name)
            print("#" * 90)

            attr = info.GetTypeAttr()

            for j in range(attr[6]):

                try:
                    fd = info.GetFuncDesc(j)
                    names = info.GetNames(fd[0])

                    print(names)

                except:
                    pass

        except Exception as e:
            print("ERROR:", repr(e))


Write the complete typelib dump to:

    zuken_dbe_typelib.txt


For example:

    with open("zuken_dbe_typelib.txt", "w", encoding="utf-8") as f:

        count = typelib.GetTypeInfoCount()

        for i in range(count):

            try:
                info = typelib.GetTypeInfo(i)
                doc = typelib.GetDocumentation(i)

                f.write("\n")
                f.write("=" * 100 + "\n")
                f.write(f"TYPE {i}: {doc}\n")
                f.write("=" * 100 + "\n")

                attr = info.GetTypeAttr()

                for j in range(attr[6]):

                    try:
                        fd = info.GetFuncDesc(j)
                        names = info.GetNames(fd[0])

                        f.write(str(names) + "\n")

                    except:
                        pass

            except Exception as e:
                f.write("ERROR: " + repr(e) + "\n")


============================================================
PHASE 5 — TEST EXISTING CABLE ACCESS
============================================================

Create a very simple known-good cable manually in Database Editor.

Suggested:

    PYTHON_CABLE_TEST

Connector A:
    pins 1,2,3

Connector B:
    pins 1,2,3

Connections:

    A.1 -> B.1
    A.2 -> B.2
    A.3 -> B.3


Test:

    name = "PYTHON_CABLE_TEST"
    version = ""

    try:
        result = app.EditComponent(name, version)
        print("EditComponent:", result)

    except Exception as e:
        print("EditComponent error:", repr(e))


Immediately after:

    comp = app.CreateDbeComponentObject()

    tests = [
        "GetId",
        "GetName",
        "GetVersion",
        "GetComponentType",
        "GetSubType",
    ]

    for method in tests:

        try:
            print(method, getattr(comp, method)())

        except Exception as e:
            print(method, "ERROR:", repr(e))


Also:

    try:
        print("Model:", comp.GetModelName(0))

    except Exception as e:
        print("GetModelName:", repr(e))


Expected cable information based on prior testing:

    ComponentType = 7
    Subtype = 0
    ModelName = None


Determine whether EditComponent() causes CreateDbeComponentObject() to correctly bind to the currently edited component.


============================================================
PHASE 6 — TEST SelectComponentFromTable
============================================================

The signature is:

    SelectComponentFromTable(
        ComponentName,
        ComponentVersion
    )


Test:

    name = "PYTHON_CABLE_TEST"
    version = ""

    try:
        result = app.SelectComponentFromTable(
            name,
            version
        )

        print("Select result:", result)
        print("Active ID:", app.GetActiveComponentId())

    except Exception as e:
        print(repr(e))


Then:

    comp = app.CreateDbeComponentObject()

    print("name:", comp.GetName())
    print("type:", comp.GetComponentType())
    print("subtype:", comp.GetSubType())


Compare this behavior with EditComponent().


============================================================
PHASE 7 — INVESTIGATE COMPONENT ATTRIBUTES
============================================================

Known signature:

    GetAttributeIds(ids, end, attnam)


Try reasonable COM output-parameter calling patterns without modifying anything.

Examples:

    comp = app.CreateDbeComponentObject()

    tests = [
        lambda: comp.GetAttributeIds(),
        lambda: comp.GetAttributeIds(None, 0, ""),
        lambda: comp.GetAttributeIds([], 0, ""),
        lambda: comp.GetAttributeIds(None, -1, ""),
    ]

    for i, fn in enumerate(tests):

        try:
            print("TEST", i, fn())

        except Exception as e:
            print("TEST", i, "FAILED:", repr(e))


The existing cable previously returned something resembling:

    (10, (...10 IDs...))


If valid attribute IDs can be extracted, inspect them using:

    attr = app.CreateDbeAttributeObject()


Dump the full COM interface for the attribute object first.

Do not assume method names.

Determine whether cable-specific information such as:

    wire count
    conductor type
    connector information
    cable properties
    ends

is stored through attributes.


============================================================
PHASE 8 — INVESTIGATE MODEL PIN
============================================================

Create:

    pin = app.CreateDbeModelPinObject()

Run:

    dump_com(pin, "MODEL PIN")


Investigate exact signatures for:

    GetPinProperties
    SetPinProperties

    GetCrimpingRules
    SetCrimpingRules

    GetRoutingOffset
    SetRoutingOffset


Determine whether ModelPin represents:

A. physical connector model pin definitions only

or:

B. actual cable end-to-pin assignments.


Do not assume B.


============================================================
PHASE 9 — INVESTIGATE NODE
============================================================

Create:

    node = app.CreateDbeNodeObject()

Run:

    dump_com(node, "NODE")


Search:

    for name in dir(node):
        if "wire" in name.lower() or "pin" in name.lower():
            print(name)


Pay particular attention to:

    HasPassWires


Determine whether Node objects are related to cable electrical connectivity or are primarily graphical/model constructs.


============================================================
PHASE 10 — TEST NewComponent CLONING
============================================================

Do this ONLY with disposable test components.

Known signature:

    NewComponent(
        name,
        version,
        baseName,
        baseVersion,
        flags
    )


Make a manually created cable:

    PYTHON_CABLE_TEMPLATE


Then test:

    NEW_NAME = "PYTHON_CLONE_DELETE_ME"
    VERSION = ""

    BASE_NAME = "PYTHON_CABLE_TEMPLATE"
    BASE_VERSION = ""

    FLAGS = 0

    try:

        result = app.NewComponent(
            NEW_NAME,
            VERSION,
            BASE_NAME,
            BASE_VERSION,
            FLAGS
        )

        print("NEW COMPONENT RESULT:", result)

    except Exception as e:
        print("ERROR:", repr(e))


Inspect the resulting component manually.

Determine whether cloning preserves:

    cable type
    connector definitions
    connector placement
    conductors
    wire properties
    pin assignments
    attributes


If cloning preserves the complete cable structure, template cloning may be an excellent implementation strategy.


============================================================
PHASE 11 — TEST SAVE SEMANTICS
============================================================

ONLY on disposable test components.

Test component-level:

    try:
        print("COMP SAVE:", comp.Save())

    except Exception as e:
        print(repr(e))


And application-level:

    try:
        print("APP SAVE:", app.Save(0))

    except Exception as e:
        print(repr(e))


Determine which operation actually commits DBE changes.


============================================================
PHASE 12 — SEARCH INSTALLED ZUKEN FILES FOR EXAMPLES
============================================================

This is high priority.

The local E3 installation may contain example VBScript/Python/C#/COM automation code.

Use PowerShell.

Search for DBEApplication:

    Get-ChildItem "C:\Program Files\Zuken" -Recurse -File -ErrorAction Silently

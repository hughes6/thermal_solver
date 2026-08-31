#!/usr/bin/env bash
# Clean-build semiFrozenChtMultiRegionFoam with a source-bound runtime identity.
# Run only inside an initialized OpenFOAM environment after the resource gate.

set -euo pipefail

usage() {
    printf '%s\n' \
        "Usage: $0 --evidence PATH [--expected-source-sha SHA256] [--negative-mode-case CASE] [--scratch-root DIR]" \
        "" \
        "The evidence path is create-only. This script never starts WSL or" \
        "loads OpenFOAM itself; FOAM_API, WM_PROJECT_VERSION, WM_OPTIONS," \
        "FOAM_USER_APPBIN, wclean, wmake, and python3 must already be available."
}

evidence_path=""
expected_source_sha=""
negative_mode_case=""
scratch_root=""
while (($#)); do
    case "$1" in
        --evidence)
            (($# >= 2)) || { usage >&2; exit 64; }
            evidence_path="$2"
            shift 2
            ;;
        --expected-source-sha)
            (($# >= 2)) || { usage >&2; exit 64; }
            expected_source_sha="$2"
            shift 2
            ;;
        --negative-mode-case)
            (($# >= 2)) || { usage >&2; exit 64; }
            negative_mode_case="$2"
            shift 2
            ;;
        --scratch-root)
            (($# >= 2)) || { usage >&2; exit 64; }
            scratch_root="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'ERROR: unknown argument: %s\n' "$1" >&2
            usage >&2
            exit 64
            ;;
    esac
done

[[ -n "$evidence_path" ]] || {
    printf 'ERROR: --evidence is required.\n' >&2
    exit 64
}
[[ ! -e "$evidence_path" ]] || {
    printf 'ERROR: refusing to overwrite evidence: %s\n' "$evidence_path" >&2
    exit 65
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
solver_dir="$repo_root/openfoam_semifrozen_solver"
attester="$repo_root/tools/openfoam_semifrozen_attestation.py"

for command_name in python3 wclean wmake cp mv chmod mktemp mkdir rm basename cmp; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'ERROR: required initialized-environment command is absent: %s\n' \
            "$command_name" >&2
        exit 66
    }
done
for variable_name in FOAM_API WM_PROJECT_VERSION WM_OPTIONS FOAM_USER_APPBIN; do
    value="${!variable_name:-}"
    [[ -n "$value" ]] || {
        printf 'ERROR: required OpenFOAM identity variable is unset: %s\n' \
            "$variable_name" >&2
        exit 66
    }
done
for value in "$FOAM_API" "$WM_PROJECT_VERSION" "$WM_OPTIONS"; do
    [[ "$value" =~ ^[A-Za-z0-9._+-]+$ ]] || {
        printf 'ERROR: unsafe OpenFOAM identity value: %s\n' "$value" >&2
        exit 66
    }
done
[[ -d "$FOAM_USER_APPBIN" ]] || {
    printf 'ERROR: FOAM_USER_APPBIN is not a directory: %s\n' \
        "$FOAM_USER_APPBIN" >&2
    exit 66
}

source_sha="$(
    python3 "$attester" --repo-root "$repo_root" --print-source-sha
)"
[[ "$source_sha" =~ ^[0-9a-f]{64}$ ]] || {
    printf 'ERROR: canonical source fingerprint is malformed: %s\n' \
        "$source_sha" >&2
    exit 67
}
if [[ -n "$expected_source_sha" ]]; then
    [[ "$expected_source_sha" =~ ^[0-9a-f]{64}$ ]] || {
        printf 'ERROR: expected source fingerprint is malformed: %s\n' \
            "$expected_source_sha" >&2
        exit 67
    }
    [[ "$source_sha" == "$expected_source_sha" ]] || {
        printf '%s\n' \
            "ERROR: bundled solver source does not match the exported case pin;" \
            "expected=$expected_source_sha observed=$source_sha" >&2
        exit 67
    }
fi

# Build from a short, disposable path.  This avoids GNU make ambiguity in a
# repository path containing spaces and ensures Make artefacts and the
# generated identity header never become source-tree evidence.
build_root="$(mktemp -d "${TMPDIR:-/tmp}/thermal_sim_semifrozen_build.XXXXXX")"
[[ -d "$build_root" && "$(basename "$build_root")" == thermal_sim_semifrozen_build.* ]] || {
    printf 'ERROR: unsafe temporary build directory: %s\n' "$build_root" >&2
    exit 67
}
build_solver_dir="$build_root/openfoam_semifrozen_solver"
mkdir -p "$build_solver_dir"
cp -a -- "$solver_dir/Make" "$build_solver_dir/Make"
cp -- "$solver_dir/semiFrozenChtMultiRegionFoam.C" "$build_solver_dir/"
copied_source_sha="$(
    python3 "$attester" --repo-root "$build_root" --print-source-sha
)"
[[ "$copied_source_sha" == "$source_sha" ]] || {
    printf '%s\n' \
        "ERROR: source changed while creating the disposable build tree;" \
        "refusing to compile an ambiguous snapshot." >&2
    exit 67
}

header_dir="$build_solver_dir/generated"
header_path="$header_dir/solverBuildAttestation.H"
mkdir -p "$header_dir"
temporary_header="$(mktemp "$header_dir/.solverBuildAttestation.XXXXXX")"
cleanup_build_tree() {
    if [[ -n "${temporary_header:-}" && -e "$temporary_header" ]]; then
        rm -f -- "$temporary_header"
    fi
    if
        [[
            -n "${build_root:-}"
         && -d "$build_root"
         && "$(basename "$build_root")" == thermal_sim_semifrozen_build.*
        ]]
    then
        rm -rf -- "$build_root"
    fi
    if [[ -n "${install_temporary:-}" && -e "$install_temporary" ]]; then
        rm -f -- "$install_temporary"
    fi
}
trap cleanup_build_tree EXIT
{
    printf '%s\n' '#ifndef THERMAL_SIM_SOLVER_BUILD_ATTESTATION_H'
    printf '%s\n' '#define THERMAL_SIM_SOLVER_BUILD_ATTESTATION_H'
    printf '#define THERMAL_SIM_SOLVER_PROJECT_SOURCE_SHA256 "%s"\n' "$source_sha"
    printf '#define THERMAL_SIM_SOLVER_FOAM_API "%s"\n' "$FOAM_API"
    printf '#define THERMAL_SIM_SOLVER_WM_PROJECT_VERSION "%s"\n' \
        "$WM_PROJECT_VERSION"
    printf '#define THERMAL_SIM_SOLVER_WM_OPTIONS "%s"\n' "$WM_OPTIONS"
    printf '%s\n' '#endif'
} >"$temporary_header"
mv -f -- "$temporary_header" "$header_path"
temporary_header=""

deployed_target="$FOAM_USER_APPBIN/semiFrozenChtMultiRegionFoam"
temporary_appbin="$build_root/bin"
mkdir -p "$temporary_appbin"
built_target="$temporary_appbin/semiFrozenChtMultiRegionFoam"
printf 'Canonical repository-local solver source SHA-256: %s\n' "$source_sha"
printf 'Cleaning empty disposable build tree: %s\n' \
    "$build_solver_dir"
FOAM_USER_APPBIN="$temporary_appbin" wclean "$build_solver_dir"
if [[ -e "$built_target" || -L "$built_target" ]]; then
    printf '%s\n' \
        "ERROR: wclean left a target in the disposable app bin; refusing a" \
        "build that could be mistaken for a clean result:" \
        "$built_target" >&2
    exit 68
fi

printf 'Building solver for FOAM_API=%s WM_PROJECT_VERSION=%s WM_OPTIONS=%s\n' \
    "$FOAM_API" "$WM_PROJECT_VERSION" "$WM_OPTIONS"
FOAM_USER_APPBIN="$temporary_appbin" wmake "$build_solver_dir"
[[ -f "$built_target" && -x "$built_target" && ! -L "$built_target" ]] || {
    printf 'ERROR: clean build did not produce executable target: %s\n' \
        "$built_target" >&2
    exit 69
}
built_binary_sha256="$(
    python3 -c \
        'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
        "$built_target"
)"
[[ "$built_binary_sha256" =~ ^[0-9a-f]{64}$ ]] || {
    printf 'ERROR: built binary SHA-256 is malformed: %s\n' \
        "$built_binary_sha256" >&2
    exit 69
}

preinstall_evidence="$build_root/preinstall_attestation.json"
preinstall_verify_command=(
    python3 "$attester"
    --repo-root "$repo_root"
    --binary "$built_target"
    --expected-foam-api "$FOAM_API"
    --expected-wm-project-version "$WM_PROJECT_VERSION"
    --expected-wm-options "$WM_OPTIONS"
    --expected-binary-sha256 "$built_binary_sha256"
    --evidence "$preinstall_evidence"
)
if [[ -n "$negative_mode_case" ]]; then
    preinstall_verify_command+=(--negative-mode-case "$negative_mode_case")
fi
if [[ -n "$scratch_root" ]]; then
    preinstall_verify_command+=(--scratch-root "$scratch_root")
fi
"${preinstall_verify_command[@]}"

previous_target="$build_root/previous-semiFrozenChtMultiRegionFoam"
previous_target_present=0
if [[ -L "$deployed_target" ]]; then
    printf 'ERROR: refusing to replace deployed solver symlink: %s\n' \
        "$deployed_target" >&2
    exit 70
elif [[ -f "$deployed_target" ]]; then
    cp -p -- "$deployed_target" "$previous_target"
    previous_target_present=1
elif [[ -e "$deployed_target" ]]; then
    printf 'ERROR: deployed target exists but is not a regular file: %s\n' \
        "$deployed_target" >&2
    exit 70
fi

restore_previous_target() {
    if ((previous_target_present)); then
        install_temporary="$(
            mktemp "$FOAM_USER_APPBIN/.semiFrozenChtMultiRegionFoam.rollback.XXXXXX"
        )"
        cp -p -- "$previous_target" "$install_temporary"
        mv -f -- "$install_temporary" "$deployed_target"
        install_temporary=""
    else
        rm -f -- "$deployed_target"
    fi
}

# Copy into the destination filesystem, then rename there so readers see
# either the previous complete binary or the newly attested complete binary.
install_temporary="$(
    mktemp "$FOAM_USER_APPBIN/.semiFrozenChtMultiRegionFoam.install.XXXXXX"
)"
cp -- "$built_target" "$install_temporary"
chmod --reference="$built_target" "$install_temporary"
mv -f -- "$install_temporary" "$deployed_target"
install_temporary=""
if [[ ! -f "$deployed_target" || ! -x "$deployed_target" || -L "$deployed_target" ]] \
    || ! cmp -s -- "$built_target" "$deployed_target"; then
    printf 'ERROR: atomic install did not produce a regular, byte-identical executable: %s\n' \
        "$deployed_target" >&2
    restore_previous_target
    exit 70
fi

final_verify_command=(
    python3 "$attester"
    --repo-root "$repo_root"
    --binary "$deployed_target"
    --expected-foam-api "$FOAM_API"
    --expected-wm-project-version "$WM_PROJECT_VERSION"
    --expected-wm-options "$WM_OPTIONS"
    --expected-binary-sha256 "$built_binary_sha256"
    --evidence "$evidence_path"
)
if [[ -n "$negative_mode_case" ]]; then
    final_verify_command+=(--negative-mode-case "$negative_mode_case")
fi
if [[ -n "$scratch_root" ]]; then
    final_verify_command+=(--scratch-root "$scratch_root")
fi

if ! "${final_verify_command[@]}"; then
    printf '%s\n' \
        "ERROR: installed-binary verification failed; restoring the previous" \
        "deployed state." >&2
    restore_previous_target
    exit 71
fi
printf 'Clean build and runtime attestation passed: %s\n' "$evidence_path"

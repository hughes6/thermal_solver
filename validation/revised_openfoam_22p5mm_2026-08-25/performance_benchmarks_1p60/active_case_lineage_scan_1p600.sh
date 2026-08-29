#!/usr/bin/env bash
set -euo pipefail

case_dir="${1:?usage: $0 CASE_DIR [CHECKPOINT] [RANKS]}"
checkpoint="${2:-1.6000000000000001}"
ranks="${3:-4}"
failures=0
checked_fields=0

for ((rank=0; rank<ranks; ++rank)); do
    time_dir="$case_dir/processor${rank}/${checkpoint}"
    for field in T U p p_rgh phi rho k omega nut alphat; do
        ((checked_fields+=1))
        if [[ ! -f "$time_dir/fluid/$field" ]]; then
            printf 'MISSING processor%d/%s/fluid/%s\n' "$rank" "$checkpoint" "$field"
            ((failures+=1))
        fi
    done
    for region_dir in "$case_dir/processor${rank}/constant"/*; do
        [[ -d "$region_dir/polyMesh" ]] || continue
        region="${region_dir##*/}"
        [[ "$region" == fluid ]] && continue
        ((checked_fields+=1))
        if [[ ! -f "$time_dir/$region/T" ]]; then
            printf 'MISSING processor%d/%s/%s/T\n' "$rank" "$checkpoint" "$region"
            ((failures+=1))
        fi
    done
done

start_from=$(awk '/^[[:space:]]*startFrom[[:space:]]+/ { value=$2; sub(/;.*/,"",value); print value; exit }' "$case_dir/system/controlDict")
start_time=$(awk '/^[[:space:]]*startTime[[:space:]]+/ { value=$2; sub(/;.*/,"",value); print value; exit }' "$case_dir/system/controlDict")

future_directories=0
while IFS= read -r directory; do
    candidate="${directory##*/}"
    if awk -v value="$candidate" -v common="$checkpoint" 'BEGIN { scale=(value<0?-value:value); other=(common<0?-common:common); if(other>scale)scale=other; if(scale<1)scale=1; exit !(value>common+1e-9*scale) }'; then
        printf 'FUTURE_DIRECTORY %s\n' "${directory#"$case_dir/"}"
        ((future_directories+=1))
        ((failures+=1))
    fi
done < <(find "$case_dir/postProcessing" -mindepth 1 -type d -printf '%p\n' | awk -F/ '$NF ~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { print }' | sort)

future_files=0
while IFS=$'\t' read -r report_file future_sample; do
    [[ -n "$report_file" && -n "$future_sample" ]] || continue
    printf 'FUTURE_SAMPLE %s t=%s\n' "${report_file#"$case_dir/"}" "$future_sample"
    ((future_files+=1))
    ((failures+=1))
done < <(find "$case_dir/postProcessing" -type f \( -name '*.dat' -o -name '*.csv' \) -exec awk -v common="$checkpoint" '
        {
            line=$0
            sub(/^[[:space:]]+/,"",line)
            count=split(line,column,/[[:space:],]+/)
            if(count<1) next
            value=column[1]
            if(value !~ /^[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) next
            scale=(value<0?-value:value)
            other=(common<0?-common:common)
            if(other>scale)scale=other
            if(scale<1)scale=1
            if(value>common+1e-9*scale) {
                print FILENAME "\t" value
                nextfile
            }
        }
    ' {} +)

printf 'CHECKPOINT=%s\n' "$checkpoint"
printf 'RANKS=%d\n' "$ranks"
printf 'CHECKED_REQUIRED_FIELDS=%d\n' "$checked_fields"
printf 'START_FROM=%s\n' "$start_from"
printf 'START_TIME=%s\n' "$start_time"
printf 'FUTURE_DIRECTORIES=%d\n' "$future_directories"
printf 'FUTURE_REPORT_FILES=%d\n' "$future_files"
printf 'FAILURES=%d\n' "$failures"

((failures==0))

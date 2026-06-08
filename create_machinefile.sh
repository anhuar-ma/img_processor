#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source_path=${1:-"$script_dir/machinefile_all"}
output_path=${2:-"$script_dir/machinefile"}

if [[ ! -f "$source_path" ]]; then
  printf '{"ok":false,"error":"No existe el archivo %s"}\n' "$source_path"
  exit 1
fi

mkdir -p "$(dirname "$output_path")"
: > "$output_path"

active_hosts=()
inactive_hosts=()
active_count=0
inactive_count=0
total_slots=0
json_hosts=""
first_host=1

while IFS= read -r raw_line || [[ -n "$raw_line" ]]; do
  line=${raw_line#${raw_line%%[!$' \t']*}}
  line=${line%${line##*[!$' \t']}}

  [[ -z "$line" || ${line:0:1} == '#' ]] && continue

  read -r host _rest <<< "$line"
  slots=1
  for token in $line; do
    case $token in
      slots=*)
        value=${token#slots=}
        if [[ $value =~ ^[0-9]+$ ]] && (( value > 0 )); then
          slots=$value
        fi
        break
        ;;
    esac
  done

  if ping -c 1 -W 1 "$host" >/dev/null 2>&1; then
    printf '%s\n' "$line" >> "$output_path"
    active_hosts+=("$host:$slots")
    ((active_count += 1))
    ((total_slots += slots))
    if (( first_host )); then
      json_hosts+="{\"host\":\"$host\",\"slots\":$slots}"
      first_host=0
    else
      json_hosts+=",{\"host\":\"$host\",\"slots\":$slots}"
    fi
  else
    inactive_hosts+=("$host")
    ((inactive_count += 1))
  fi
done < "$source_path"

json_inactive=""
for host in "${inactive_hosts[@]}"; do
  if [[ -z "$json_inactive" ]]; then
    json_inactive="\"$host\""
  else
    json_inactive+=",\"$host\""
  fi
done

printf '{'
printf '"ok":%s,' "$([[ $active_count -gt 0 ]] && printf true || printf false)"
printf '"source_file":"%s",' "$source_path"
printf '"output_file":"%s",' "$output_path"
printf '"active_hosts":[%s],' "$json_hosts"
printf '"inactive_hosts":[%s],' "$json_inactive"
printf '"active_count":%d,' "$active_count"
printf '"inactive_count":%d,' "$inactive_count"
printf '"total_slots":%d' "$total_slots"
printf '}\n'

if (( active_count == 0 )); then
  printf 'No responsive hosts found in %s\n' "$source_path" >&2
  exit 2
fi

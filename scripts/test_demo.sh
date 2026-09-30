#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
port="${DEMO_OPRF_PORT:-50141}"

server="$repo_dir/oprf/oprf_server"
if [[ ! -x "$server" ]]; then
  echo "error: OPRF server not found at $server" >&2
  exit 1
fi

server_log="$(mktemp -t fuzzy-pets-demo-oprf.XXXXXX)"
server_pid=""
cleanup() {
  if [[ -n "$server_pid" ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -f "$server_log"
}
trap cleanup EXIT INT TERM

"$repo_dir/fuzzy_pets_demo" --through=e2lsh
"$repo_dir/fuzzy_pets_demo" --through=cuckoo

"$server" -port "$port" -prf GCAES -loop >"$server_log" 2>&1 &
server_pid=$!
sleep 1

"$repo_dir/fuzzy_pets_demo" --through=oprf --oprf-addr="127.0.0.1:$port"
"$repo_dir/fuzzy_pets_demo" --through=batchpir --oprf-addr="127.0.0.1:$port"

echo "Staged demo tests completed successfully."

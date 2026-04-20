_preset := if os() == "macos" { "debug" } else { "linux-debug" }
_gles3_preset := if os() == "macos" { "gles3-debug" } else { "linux-gles3-debug" }

gen-arb:
  cmake -S neo --preset {{ _preset }}

build-arb: gen-arb
  cmake --build build/arb

assets:
  #!/usr/bin/env bash
  set -euo pipefail
  if [ -d build/base ]; then
    echo "Assets already present at build/base, skipping download."
    exit 0
  fi
  mkdir -p build
  gcloud storage cp gs://private.chinmaygarde.com/Archive/doom3REbase.zip build/doom3REbase.zip
  unzip -o build/doom3REbase.zip -d build/
  rm build/doom3REbase.zip

gen-gles3:
  cmake -S neo --preset {{ _gles3_preset }}

build-gles3: gen-gles3
  cmake --build build/gles3

run-gles3: build-gles3 assets
  #!/usr/bin/env bash
  set -euo pipefail
  if [ "{{ os() }}" = "macos" ]; then
    open build/gles3/dhewm3.app
  else
    build/gles3/dhewm3 +set fs_basepath build/
  fi

run-arb: build-arb assets
  #!/usr/bin/env bash
  set -euo pipefail
  if [ "{{ os() }}" = "macos" ]; then
    open build/arb/dhewm3.app
  else
    build/arb/dhewm3 +set fs_basepath build/
  fi

clean-arb:
  rm -rf build

_preset := if os() == "macos" { "debug" } else { "linux-debug" }

gen:
  cmake -S neo --preset {{ _preset }}

build: gen
  cmake --build build

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

run: build assets
  #!/usr/bin/env bash
  set -euo pipefail
  if [ "{{ os() }}" = "macos" ]; then
    open build/dhewm3.app
  else
    build/dhewm3
  fi

clean:
  rm -rf build

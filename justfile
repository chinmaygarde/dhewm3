gen:
  cmake -S neo --preset debug

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
  open build/dhewm3.app

clean:
  rm -rf build

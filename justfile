gen:
  cmake -S neo --preset debug

build: gen
  cmake --build build

clean:
  rm -rf build

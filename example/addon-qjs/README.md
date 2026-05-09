# Example Program: QuickJS Addon

## Build

```bash
cmake -B build -S .

cmake --build build --config Debug --target ALL_BUILD
```

After the build is complete, you need to copy the build artifacts `qjs.exe`, `example-addon-qjs.so/dll` to the current directory.

Make sure the path structure is as follows:

```
/jspp/example/addon-qjs/
    CMakeLists.txt
    example.js
    example.cc

    qjs.exe
    example-addon-qjs.so/dll
```

## Run

```bash
./qjs.exe -m ./example.js
```

## Note

Make sure `QjsInitializeFlags` is set correctly when creating the engine.

If the target host is QuickJS embedded in another project, be sure to ensure ABI compatibility.

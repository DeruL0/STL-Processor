# STL Processor

A DirectX 12 application for STL mesh repair and visualization with Dear ImGui control panels.

## Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Run

```powershell
Set-Location "build/src/App/Release"
./App.exe
```

## Project Structure

- `src/MeshCore`: CPU-only mesh processing library
- `src/DX12Engine`: DirectX 12 engine library
- `src/GUI`: Dear ImGui integration and parameter panels
- `src/App`: executable application
- `third_party/eigen`: Eigen headers
- `third_party/d3dx12`: d3dx12 single-header
- `third_party/imgui`: Dear ImGui `v1.92.6` (fixed, vendored)
- `assets`: models, shaders, textures

## Usage

The app starts by loading the original source STL mesh (no auto-repair on startup).

Default source mesh path is configured in `STLApp`:

```cpp
std::string modelDirectory = "assets/Models/";
std::string sourceModelName = "bunnyhole";
```

Runtime panels:

- `Mesh Info`: vertex/triangle counts, current task, status
- `Repair`: tune parameters and run `RepairNormal`, `HolesFilling`, `Denoise`, `Smoothing`, `Apply All`, `Reset`
- `Polycube`: tune placeholder parameters and invoke polycube pipeline call

Background task model:

- Mesh processing runs on a worker thread
- Submitting while busy keeps only the latest pending request (`latest-wins`)
- GPU mesh upload is applied on the main render thread

## Camera Controls

- `W` / `S`: move forward / backward
- `A` / `D`: strafe left / right
- Mouse drag: rotate view

## License

MIT

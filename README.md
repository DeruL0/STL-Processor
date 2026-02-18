# STL Processor

A DirectX 12 application for STL mesh repair and visualization. Loads STL triangle meshes, performs automatic repair (hole filling, normal correction, denoising, smoothing), and renders the result in real time.

## Features

- **Half-Edge Data Structure** — efficient adjacency queries for traversal, neighbor lookups, and topological operations
- **Hole Filling** — identifies boundary loops and fills holes using a minimum-area triangulation (DP), with optional centroid-based refinement and Delaunay edge relaxation
- **Normal Repair** — detects and corrects inconsistent face orientations using Möller–Trumbore ray casting and BFS propagation
- **Denoising** — bilateral normal filtering with vertex-position update
- **Mesh Smoothing / Fairing** — supports uniform, scale-dependent, mean-value (tangent), and cotangent weights
- **Vertex Normal Computation** — area-weighted per-vertex normals from surrounding faces
- **Export** — repaired mesh exportable as STL or VTK (ASCII)
- **Real-time DirectX 12 Rendering** — triple-buffered frame resources, Phong shading via HLSL, free-fly camera

## Requirements

- Windows 10 / 11 with DirectX 12 support
- Visual Studio 2019 or later (MSVC, `v142` / `v143` toolset)
- Windows SDK 10.0+
- [Eigen](https://eigen.tuxfamily.org) (header-only, used for circumsphere checks in Delaunay refinement)

## Build

1. Open `DX12.sln` in Visual Studio.
2. Set the **Eigen** include path in `Project Properties → C/C++ → Additional Include Directories`.
3. Select the **x64 / Debug** (or Release) configuration and build (`Ctrl+Shift+B`).

## Project Structure

```
DX12/
├── STLApp.cpp / DX12.cpp      # Application entry point and DX12 base layer
├── HalfEdgeStructure.cpp/.h   # Core mesh data structure and all repair algorithms
├── FrameResource.cpp/.h       # Per-frame GPU resource management
├── Camera.cpp/.h              # Free-fly camera
├── GameTimer.cpp/.h           # High-resolution timer
├── DX12util.cpp/.h            # DX12 utility helpers and exception wrapper
├── Shaders/                   # HLSL vertex/pixel shaders
├── Models/                    # Sample STL / VTK meshes
└── Common/                    # Shared helpers (MathHelper, UploadBuffer, …)
```

## Usage

Input/output paths are currently set in `STLApp::BuildSkullGeometry()`:

```cpp
std::string read_file_path  = "D:/Projects/DX12/DX12/Models/";
std::string read_file_name  = "bunnyhole";   // loads bunnyhole.stl

meshData.ReadSTL(read_file_path, read_file_name);
meshData.HolesFilling(AREA, REFINEMENT, NO_FAIRING, 1);
meshData.RepairNormal();
// meshData.Denoise(20, 0.3f);
// meshData.Smoothing(1, TAN);
meshData.GetVertexesNormal();

std::string export_file_path = "D:/Projects/DX12/DX12/Models/";
std::string export_file_name = "bunnyRepaired";
meshData.ExportSTL(export_file_path, export_file_name);
```

### Camera Controls

| Key | Action |
|-----|--------|
| `W` / `S` | Move forward / backward |
| `A` / `D` | Strafe left / right |
| Mouse drag | Rotate view |

## Algorithms

### Hole Filling
Uses the minimum-area triangulation algorithm (Liepa 2003):
- Dynamic programming over boundary loop vertices to minimize total patch area
- Optional centroid insertion (refinement) with sigma-based criteria
- Delaunay edge relaxation via circumsphere test projected onto the local plane

### Normal Repair
- Selects a seed face with an even ray-cast intersection count (outward-facing)
- Propagates consistent orientation to neighbors via BFS
- Inconsistent faces are flipped by swapping vertex order and updating half-edge connectivity

### Fairing Weights
| Mode | Formula |
|------|---------|
| `UNIFORM` | $w_{ij} = 1$ |
| `SCALE` | $w_{ij} = \|v_i - v_j\|$ |
| `TAN` | $w_{ij} = (\tan\frac{\alpha}{2} + \tan\frac{\beta}{2}) / \|v_i - v_j\|$ |
| `COT` | $w_{ij} = \cot\alpha + \cot\beta$ |

## License

MIT

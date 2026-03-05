#pragma once

#include <string>

class HE_MeshData;

struct PolycubeOptions {
    int IterationBudget = 20;
    float AlignmentWeight = 0.5f;
    bool PreserveSharpFeatures = true;
};

struct PolycubeResult {
    bool Ok = false;
    std::string Error;
};

class Polycube {
public:
    PolycubeResult Generate(HE_MeshData& mesh, const PolycubeOptions& options = {});
};

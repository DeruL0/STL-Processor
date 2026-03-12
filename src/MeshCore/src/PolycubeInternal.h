#pragma once

#include <Eigen/Core>

#include <array>
#include <vector>

namespace PolycubeInternal {

struct FaceGeometry {
    double Area = 0.0;
    Eigen::Vector3d UnnormalizedNormal = Eigen::Vector3d::Zero();
    Eigen::Vector3d UnitNormal = Eigen::Vector3d::Zero();
};

FaceGeometry ComputeFaceGeometry(const std::array<Eigen::Vector3d, 3>& points);
double SmoothedAbs(double value, double epsilon);
Eigen::Matrix3d NearestRotation(const Eigen::Matrix3d& matrix);
double SignedTetVolume(const std::array<Eigen::Vector3d, 4>& tet);
double TetArapEnergy(
    const std::array<Eigen::Vector3d, 4>& restTet,
    const std::array<Eigen::Vector3d, 4>& deformedTet
);
int DominantAxisLabel(const Eigen::Vector3d& normal);
std::vector<int> CleanupPatchLabels(
    const std::vector<std::array<int, 3>>& faces,
    const std::vector<Eigen::Vector3d>& positions,
    const std::vector<int>& initialLabels
);

} // namespace PolycubeInternal

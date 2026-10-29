#pragma once
// Symmetric Jacobi eigenvalue solver written from first principles in C++17.
// Repeatedly applies Givens rotations J(p, q, theta) to eliminate the largest off-diagonal entries
// of a real symmetric n x n matrix until off(A) < epsilon.
// Time complexity: O(n^3) per sweep; convergence is quadratic for symmetric matrices.

#include <vector>

namespace scx {

struct EigenResult {
    std::vector<double> eigenvalues;                 // sorted ascending: lambda_1 <= lambda_2 <= ... <= lambda_n
    std::vector<std::vector<double>> eigenvectors;   // eigenvectors[k] is the normalized eigenvector for eigenvalues[k]
    int iterations = 0;
    int sweeps = 0;
    bool converged = false;
};

// Computes all eigenvalues and eigenvectors of a real symmetric matrix A.
// Throws std::invalid_argument if A is not square.
// Tolerance defaults to 1e-10.
EigenResult solveSymmetricEigenvalues(const std::vector<std::vector<double>>& matrix,
                                      double tolerance = 1e-10,
                                      int maxSweeps = 100);

}  // namespace scx


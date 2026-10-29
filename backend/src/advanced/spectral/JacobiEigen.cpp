#include "advanced/spectral/JacobiEigen.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace scx {

namespace {

double sgn(double x) {
    return (x >= 0.0) ? 1.0 : -1.0;
}

double offDiagonalNorm(const std::vector<std::vector<double>>& A, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            sum += A[i][j] * A[i][j];
        }
    }
    return std::sqrt(2.0 * sum);
}

}  // namespace

EigenResult solveSymmetricEigenvalues(const std::vector<std::vector<double>>& matrix,
                                      double tolerance,
                                      int maxSweeps) {
    const int n = static_cast<int>(matrix.size());
    if (n == 0) {
        return EigenResult{{}, {}, 0, 0, true};
    }
    for (int i = 0; i < n; ++i) {
        if (static_cast<int>(matrix[i].size()) != n) {
            throw std::invalid_argument("matrix must be square");
        }
    }

    if (n == 1) {
        return EigenResult{{matrix[0][0]}, {{1.0}}, 0, 0, true};
    }

    // Working copies: A is transformed to diagonal form, V accumulates Givens rotations.
    auto A = matrix;
    std::vector<std::vector<double>> V(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) V[i][i] = 1.0;

    EigenResult result;
    result.converged = false;
    int totalRotations = 0;

    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        result.sweeps = sweep + 1;
        const double offNorm = offDiagonalNorm(A, n);
        if (offNorm <= tolerance) {
            result.converged = true;
            break;
        }

        // Cyclic Jacobi sweep over all pairs p < q
        for (int p = 0; p < n - 1; ++p) {
            for (int q = p + 1; q < n; ++q) {
                const double apq = A[p][q];
                if (std::abs(apq) <= 1e-15) continue;

                const double app = A[p][p];
                const double aqq = A[q][q];
                const double tau = (aqq - app) / (2.0 * apq);
                const double t = (std::abs(tau) > 1e15) ? (1.0 / (2.0 * tau)) : (sgn(tau) / (std::abs(tau) + std::sqrt(1.0 + tau * tau)));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double s = c * t;

                // Update A: rotate rows and columns p and q
                A[p][p] = app - t * apq;
                A[q][q] = aqq + t * apq;
                A[p][q] = 0.0;
                A[q][p] = 0.0;

                for (int r = 0; r < n; ++r) {
                    if (r == p || r == q) continue;
                    const double arp = A[r][p];
                    const double arq = A[r][q];
                    const double new_arp = c * arp - s * arq;
                    const double new_arq = s * arp + c * arq;
                    A[r][p] = new_arp;
                    A[p][r] = new_arp;
                    A[r][q] = new_arq;
                    A[q][r] = new_arq;
                }

                // Accumulate eigenvectors in V: column updates
                for (int r = 0; r < n; ++r) {
                    const double vrp = V[r][p];
                    const double vrq = V[r][q];
                    V[r][p] = c * vrp - s * vrq;
                    V[r][q] = s * vrp + c * vrq;
                }

                totalRotations++;
            }
        }
    }

    if (!result.converged) {
        result.converged = (offDiagonalNorm(A, n) <= tolerance * 100.0);
    }
    result.iterations = totalRotations;

    // Extract eigenvalues and permute to ascending order
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int i, int j) {
        return A[i][i] < A[j][j];
    });

    result.eigenvalues.resize(n);
    result.eigenvectors.resize(n, std::vector<double>(n));

    for (int k = 0; k < n; ++k) {
        const int origCol = order[k];
        result.eigenvalues[k] = A[origCol][origCol];
        for (int row = 0; row < n; ++row) {
            result.eigenvectors[k][row] = V[row][origCol];
        }
        // Normalize eigenvector sign convention: first non-zero entry positive
        double leading = 0.0;
        for (int row = 0; row < n; ++row) {
            if (std::abs(result.eigenvectors[k][row]) > 1e-12) {
                leading = result.eigenvectors[k][row];
                break;
            }
        }
        if (leading < 0.0) {
            for (int row = 0; row < n; ++row) {
                result.eigenvectors[k][row] = -result.eigenvectors[k][row];
            }
        }
    }

    return result;
}

}  // namespace scx


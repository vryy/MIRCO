#include <Epetra_SerialSpdDenseSolver.h>
#include <Epetra_SerialSymDenseMatrix.h>
#include <omp.h>
#include <unistd.h>
#include <Teuchos_Assert.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace std;
#include "contactpredictors.h"
#include "contactstatus.h"
#include "evaluate.h"
#include "linearsolver.h"
#include "matrixsetup.h"
#include "nonlinearsolver.h"
#include "setparameters.h"
#include "topology.h"
#include "topologyutilities.h"
#include "warmstart.h"
#include "writetofile.h"

void Evaluate(const std::string &inputFileName, double &force)
{
  omp_set_num_threads(6);  // 6 seems to be optimal

  auto start = std::chrono::high_resolution_clock::now();

  // set flagwarm --> true for using the warm starter. It predicts the nodes coming into contact in
  // the next iteration and hence speeds up the computation.
  bool flagwarm;
  // n --> resolution parameter
  int n;
  // E1 and E2 are the Young's moduli of bodies 1 and 2 respectively.
  // nu1 and nu2 are the Poisson's ratios.
  // G1 and G2 are respective Shear moduli.
  // E is the composite Young's modulus.
  // alpha --> shape factor for this problem
  // k_el --> Elastic compliance correction
  // delta --> grid size
  // nnodi --> total number of nodes
  // tol --> tolerance for the convergence of force.
  // lato --> Lateral side of the surface [micrometers]
  // errf --> initialing the error in the force vector.
  // Delta --> far-field displacement.
  double nu1, nu2, G1, G2, E, alpha, k_el, delta, nnodi, to1, E1, E2, lato, errf, Delta;
  // set rmg_flag --> true to use the random mid-point generator to generate a topology.
  // set rmg_flag --> flase to read the topology from an input file.
  bool rmg_flag;
  // set rand_sed_flag --> true to fix the seed to generate psuedo random topology to reproduce
  // results.
  bool rand_seed_flag;
  // Hurst --> Hurst Exponent (Used in random mid-point generator)
  double Hurst;
  // the actual path of the topology file.
  string zfilePath;
  // rmg_seed --> set the value of seed for the random mid-point generator
  int rmg_seed;
  // max_iter --> maximum number of iterations for the force to converge.
  int max_iter;

  // SetParameters is used to set all the simulation specific, material, and
  // geometrical parameters.
  SetParameters(E1, E2, lato, nu1, nu2, G1, G2, E, alpha, k_el, delta, nnodi, errf, to1, Delta,
      zfilePath, n, inputFileName, rmg_flag, Hurst, rand_seed_flag, rmg_seed, flagwarm, max_iter);

  time_t now = time(0);
  tm *ltm = localtime(&now);
  std::cout << "Time is: " << ltm->tm_hour << ":";
  std::cout << 1 + ltm->tm_min << endl;

  // Meshgrid-Command
  // Identical Vectors/Matricies, therefore only created one here.
  // Replacement for "for (double i = delta / 2; i < lato; i = i + delta)"
  int iter = int(ceil((lato - (delta / 2)) / delta));
  std::vector<double> x(iter);
  CreateMeshgrid(x, iter, delta);

  // Initialise Topology matrix and solution for contact force matrix y.
  Epetra_SerialDenseMatrix topology, y;
  int N = pow(2, n);
  topology.Shape(N + 1, N + 1);

  std::shared_ptr<TopologyGeneration> surfacegenerator;
  // creating the correct surface object
  CreateSurfaceObject(n, Hurst, rand_seed_flag, zfilePath, rmg_flag, rmg_seed, surfacegenerator);

  surfacegenerator->GetSurface(topology);

  // Initialise the maximum and mean value of topology.
  double zmax = 0;
  double zmean = 0;

  // Calculate the maximum and mean value of topology.
  ComputeMaxAndMean(topology, zmax, zmean);

  // Initialise the area vector and force vector. Each element containing the
  // area and force calculated at every iteration.
  vector<double> area0;
  vector<double> force0;

  // Initialise elastic correction w_el (I think this is not necessary. It could
  // be useful if we also calculate the derivative of force as it will help to
  // converge to solution faster; for that we have to add a line to calcualte
  // the elastic correction. Currently it remains zero.) Initialise final value
  // of contact area
  double w_el = 0, area = 0;

  // Initialise number of iteration, k, and initial number of predicted contact
  // nodes, n0.
  int k = 0, n0 = 0;

  // xv0,yv0 --> coordinates of the points predicted to be in contact in
  // function ContactSetPredictor for a particular iteration. xvf,yvf -->
  // coordinates of the points in contact in the previous iteration. pf -->
  // contact force at (xvf,yvf) predicted in the previous iteration. b0 -->
  // indentation value of the half space at the point of contact.
  std::vector<double> xv0, yv0, b0, xvf, yvf, pf;

  // x0 --> contact forces at (xvf,yvf) predicted in the previous iteration but
  // are a part of currect predicted contact set. x0 is calculated in the
  // Warmstart function to be used in the NNLS to accelerate the simulation.
  Epetra_SerialDenseMatrix x0;

  // nf --> the number of nodes in contact in the previous iteration.
  int nf = 0;

  // A --> the influence coefficient matrix (Discreet version of Green Function)
  Epetra_SerialDenseMatrix A;

  while (errf > to1 && k < max_iter)
  {
    // First predictor for contact set
    // All points, for which gap is bigger than the displacement of the rigid
    // indenter, cannot be in contact and thus are not checked in nonlinear
    // solve
    // @{
    ContactSetPredictor(n0, xv0, yv0, b0, zmax, Delta, w_el, x, topology);

    A.Shape(xv0.size(), xv0.size());
    // Construction of the Matrix A
    MatrixGeneration matrix1;
    matrix1.SetUpMatrix(A, xv0, yv0, delta, E, n0);

    // Second predictor for contact set
    // @{
    // The aim of this function is to guess the set of nodes in contact among the nodes predicted in
    // the ContactSetPredictor function. It uses Warmstart to make an initial guess of the nodes in
    // contact in this iteration based on the previous iteration.
    InitialGuessPredictor(flagwarm, k, n0, nf, xv0, yv0, pf, x0, b0, xvf, yvf);
    // }

    // {
    Epetra_SerialDenseMatrix b0new;
    b0new.Shape(b0.size(), 1);
    // } Parallel region makes around this makes program slower
    // b0new is same as b0 (This unnecessary copying step could be avoided for
    // optimization)
#pragma omp parallel for schedule(static, 16)  // Always same workload -> Static!
    for (long unsigned int i = 0; i < b0.size(); i++)
    {
      b0new(i, 0) = b0[i];
    }

    // w --> Defined as (u - u(bar)) in (Bemporad & Paggi, 2015)
    // http://dx.doi.org/10.1016/j.ijsolstr.2015.06.005
    Epetra_SerialDenseMatrix w;

    // use Nonlinear solver --> Non-Negative Least Squares (NNLS) as in
    // (Bemporad & Paggi, 2015)
    NonLinearSolver solution2;
    solution2.NonlinearSolve(A, b0new, x0, w,
        y);  // y -> sol, w -> wsol; x0 -> y0

    // Compute number of contact node
    // @{
    ComputeContactNodes(xvf, yvf, pf, nf, y, xv0, yv0);
    // }

    // Compute contact force and contact area
    // @{
    ComputeContactForceAndArea(force0, area0, w_el, nf, pf, k, delta, lato, k_el);
    // }

    // Compute error due to nonlinear correction
    // @{
    if (k > 0)
    {
      errf = abs(force0[k] - force0[k - 1]) / force0[k];
    }
    k += 1;
    // }
  }

  TEUCHOS_TEST_FOR_EXCEPTION(errf > to1, std::out_of_range,
      "The solution did not converge in the maximum "
      "number of iternations defined");
  // @{

  // Calculate the final force and area value at the end of iteration.
  force = force0[k - 1];
  area = area0[k - 1];

  // Mean pressure
  double sigmaz = force / pow(lato, 2);
  // Pressure unit per depth
  double pressz = sigmaz;
  cout << "k= " << k << " nf= " << nf << endl;
  cout << "Force= " << force << endl;
  cout << "area= " << area << endl;
  cout << "Mean pressure is:" + std::to_string(sigmaz) +
              " ; pressure unit per depth is:" + std::to_string(pressz) + " . \n";
  if (abs(sigmaz - 0.246623) > to1) cout << "Differenz ist zu groß!" << std::endl;

  auto finish = std::chrono::high_resolution_clock::now();
  double elapsedTime2 = std::chrono::duration_cast<std::chrono::seconds>(finish - start).count();
  std::cout << "Elapsed time is: " + to_string(elapsedTime2) + "s." << endl;

  writeForceToFile(y, zfilePath);
}

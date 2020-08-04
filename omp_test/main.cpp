#include <omp.h> // omp
#include <unistd.h>
#include <cmath>  //pow
#include <cstdio>
#include <fstream>   //ifstream, ofstream
#include <iostream>  //ifstream, ofstream
#include <string>    //std::to_string, std::stod
#include <vector>
#include "../include/Epetra_SerialSpdDenseSolver.h"
#include "../include/Epetra_SerialSymDenseMatrix.h"
#include <chrono> // time stuff

using namespace std;

/*
void TestingCode() { // Contains former test code
	double time=10000;

	  for(int i=0; i<1000; i++) {
		  const int size = 262144;
		  double sinTable[size];
		  auto start = std::chrono::high_resolution_clock::now();
	  
	#pragma omp parallel for
		  for(int n=0; n<size; ++n) {
			  sinTable[n] = std::sin(2 * M_PI * n / size);
		  }

		  auto finish = std::chrono::high_resolution_clock::now();
		  std::chrono::duration<double> elapsed = finish - start;

		  if (elapsed.count()<time) { time=elapsed.count(); }
	  }

	  std::cout << "Elapsed time: " << time << " s\n";
		
	  	  #pragma omp parallel 
	  { 
		  printf("Hello World from thread %d\n", omp_get_thread_num());
		  if (omp_get_thread_num() == 0) { printf("Number of threads is %d\n", omp_get_num_threads()); 
	  }
	  }
}
*/

void CreateTopology(int systemsize, Epetra_SerialDenseMatrix& topology,
                    string filePath) {
  // Readin for amount of lines -> dimension of matrix
  ifstream reader(filePath);
  string blaLine;
  int dimension = 0;
  while (getline(reader, blaLine)) {
    dimension += 1;
  }
  reader.close();
  topology.Shape(dimension, dimension);
  int lineCounter = 0;
  float elements[264];
  int position = 0;
  ifstream stream(filePath);
  string line;
  while (getline(stream, line)) {
    // Split up Values into Double-Array
    int separatorPosition = 0;
    lineCounter += 1;  // Has to happen here, since baseline value is 0.

    for (int i = 0; i < dimension; i++) {  // prevent duplication of values!
      separatorPosition = line.find_first_of(';');
      string container = line.substr(0, separatorPosition);
      line = line.substr(separatorPosition + 1, line.length());

      double value = stod(container);

      topology(lineCounter - 1, i) = value;

      position += 1;
    }
  }
  stream.close();
}

// Generating necessary constants
void SetParameters(double& E1, double& E2, int& csteps, int& flagwarm,
                   double& lato, double& zref, double& ampface, double& nu1,
                   double& nu2, double& G1, double& G2, double& E, double& G,
                   double& nu, double& alpha, double& H, double& rnd,
                   double& k_el, double& delta, double& nnodi, double& errf,
                   double& to1) {
  E1 = 1;
  E2 = 1;
  nu1 = 0.3;
  nu2 = 0.3;
  G1 = E1 / (2 * (1 + nu1));
  G2 = E2 / (2 * (1 + nu2));
  E = 1 / ((1 - pow(nu1, 2)) / E1 + (1 - pow(nu2, 2) / E2));
  G = 1 / ((2 - nu1) / (4 * G1) + (2 - nu2) / (4 * G2));
  nu = E / (2 * G) - 1;

  vector<double> alpha_con{0.778958541513360, 0.805513388666376,
                           0.826126871395416, 0.841369158110513,
                           0.851733020725652, 0.858342234203154,
                           0.862368243479785, 0.864741597831785};
  int nn = 7;  // Matrix sent has the parameter nn=2!
  alpha = alpha_con[nn - 1];
  csteps = 1;
  ampface = 1;
  flagwarm = 0;
  lato = 1000;  // Lateral side of the surface [micrometers]
  H = 0.1;      // Hurst Exponent (D = 3 - H)
  rnd = 95.0129;
  zref = 50;  // Reference for the Scaling, former value = 25
  k_el = lato * E / alpha;
  delta = lato / (pow(2, nn) + 1);
  nnodi = pow(pow(2, nn + 1), 2);

  errf = 100000000;
  to1 = 0.01;
}

void LinearSolve(Epetra_SerialSymDenseMatrix& matrix,
                 Epetra_SerialDenseMatrix& vector_x,
                 Epetra_SerialDenseMatrix& vector_b) {
  Epetra_SerialSpdDenseSolver solver;
  int err = solver.SetMatrix(matrix);
  if (err != 0) {
    std::cout << "Error setting matrix for linear solver (1)";
  }

  err = solver.SetVectors(vector_x, vector_b);
  if (err != 0) {
    std::cout << "Error setting vectors for linear solver (2)";
  }

  err = solver.Solve();
  if (err != 0) {
    std::cout << "Error setting up solver (3)";
  }
}

void SetUpMatrix(Epetra_SerialDenseMatrix& A, std::vector<double> xv0,
                 std::vector<double> yv0, double delta, double E,
                 int systemsize, int k) {
  double r;
  double pi = atan(1) * 4;
  double raggio = delta / 2;
  double C = 1 / (E * pi * raggio);

#pragma omp parallel for
  for (int i = 0; i < systemsize; i++) {
    A(i, i) = 1 * C;
  }

  for (int i = 0; i < systemsize; i++) {
    for (int j = 0; j < i; j++) {
      r = sqrt(pow((xv0[j] - xv0[i]), 2) + pow((yv0[j] - yv0[i]), 2));
      A(i, j) = C * asin(raggio / r);
      A(j, i) = C * asin(raggio / r);
    }
  }
}

void NonlinearSolve(Epetra_SerialDenseMatrix& matrix,
                    Epetra_SerialDenseMatrix& b0, std::vector<double>& y0,
                    Epetra_SerialDenseMatrix& w, Epetra_SerialDenseMatrix& y, int& counter) {
  // matrix -> A, b0 -> b, y0 -> y0 , y -> y, w-> w; nnstol, iter, maxiter ->
  // unused
  double nnlstol = 1.0000e-08;
  double maxiter = 10000;
  double eps = 2.2204e-16;
  double alphai = 0;
  double alpha = 100000000;
  int iter = 0;
  bool init = false;
  int n0 = b0.M();
  y.Shape(n0, 1);
  Epetra_SerialDenseMatrix s0;
  vector<int> P(n0);
  Epetra_SerialDenseMatrix vector_x, vector_b;
  Epetra_SerialSymDenseMatrix solverMatrix;

  // Initialize active set
  vector<int> positions;
  counter = 0;
  for (int i = 0; i < y0.size(); i++) {
    if (y0[i] >= nnlstol) {
      positions.push_back(i);
      counter += 1;
    }
  }

  w.Reshape(b0.M(), b0.N());

  if (counter == 0) {
    for (int x = 0; x < b0.M(); x++) {
      w(x, 0) = -b0(x, 0);
    }
    init = false;
  } else {
    for (int i = 0; i < counter; i++) {
      P[i] = positions[i];
    }
    init = true;
  }

  s0.Shape(n0, 1);  // Replacement for s

  bool aux1 = true, aux2 = true;

  while (aux1 == true) {
    // [wi,i]=min(w);
    double minValue = w(0, 0), minPosition = 0;
    for (int i = 0; i < w.M(); i++) {
      if (minValue > w(i, 0)) {
        minValue = w(i, 0);
        minPosition = i;
      }
    }

    if (((counter == n0) || (minValue > -nnlstol) || (iter >= maxiter)) &&
        (init == false)) {
      aux1 = false;
    } else {
      if (init == false) {
        P[counter] = minPosition;
        counter += 1;
      } else {
        init = false;
      }
    }

    int j = 0;
    aux2 = true;
    while (aux2 == true) {
      iter++;

      vector_x.Shape(counter, 1);
      vector_b.Shape(counter, 1);
      solverMatrix.Shape(counter, counter);

      for (int x = 0; x < counter; x++) {
        vector_b(x, 0) = b0(P[x], 0);

        for (int z = 0; z < counter; z++) {
          if (x >= z)
            solverMatrix(x, z) = matrix(P[x], P[z]);
          else
            solverMatrix(z, x) = matrix(P[x], P[z]);
        }
      }

      LinearSolve(solverMatrix, vector_x, vector_b);

      for (int x = 0; x < counter; x++) {
        s0(P[x], 0) = vector_x(x, 0);
      }

      bool allBigger = true;
      for (int x = 0; x < counter; x++) {
        if (s0(P[x], 0) < nnlstol) {
          allBigger = false;
        }
      }

      if (allBigger == true) {
        aux2 = false;
        for (int x = 0; x < counter; x++) {
          y(P[x], 0) = s0(P[x], 0);
        }

        // w=A(:,P(1:nP))*y(P(1:nP))-b;
        w.Scale(0.0);
        for (int a = 0; a < matrix.M(); a++) {
          w(a, 0) = 0;
          for (int b = 0; b < counter; b++) {
            w(a, 0) += (matrix(P[b], a) * y(P[b], 0));
          }
          w(a, 0) -= b0(a, 0);
        }
      } else {
        j = 0;
        alpha = 1.0e8;
        for (int i = 0; i < counter; i++) {
          if (s0(P[i], 0) < nnlstol) {
            alphai = y(P[i], 0) / (eps + y(P[i], 0) - s0(P[i], 0));
            if (alphai < alpha) {
              alpha = alphai;
              j = i;
            }
          }
        }

        for (int a = 0; a < counter; a++)
          y(P[a], 0) = y(P[a], 0) + alpha * (s0(P[a], 0) - y(P[a], 0));
        if (j > 0) {
          // jth entry in P leaves active set
          s0(P[j], 0) = 0;

          P.erase(P.begin() + j);
          counter -= 1;
        }
      }
    }
  }
}

void SetUpMatrix_Static(Epetra_SerialDenseMatrix& A, std::vector<double> xv0,
                 std::vector<double> yv0, double delta, double E,
                 int systemsize, double& time, int cachesize) {
  double r;
  double pi = atan(1) * 4;
  double raggio = delta / 2;
  double C = 1 / (E * pi * raggio);

  // MULTITHREADING
  // Reading in time
  auto start = std::chrono::high_resolution_clock::now();
  
#pragma omp parallel for schedule(static, cachesize)
  for (int i = 0; i < systemsize; i++) {
    A(i, i) = 1 * C;
  }

  // Writing time to console
  auto finish = std::chrono::high_resolution_clock::now();
  time = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
  // Hint: Micro is 10^-6
  
  // std::chrono::duration<double> elapsed = finish - start;
  // time = elapsed.count();
  
  // std::cout << "\n	Calculating time for setting up A. \n";
  // std::cout << "Elapsed time: " << time << " s\n"; 
  // MULTITHREADING END
  		  
  for (int i = 0; i < systemsize; i++) {
    for (int j = 0; j < i; j++) {
      r = sqrt(pow((xv0[j] - xv0[i]), 2) + pow((yv0[j] - yv0[i]), 2));
      A(i, j) = C * asin(raggio / r);
      A(j, i) = C * asin(raggio / r);
    }
  }
}

void SetUpMatrix_Dynamic(Epetra_SerialDenseMatrix& A, std::vector<double> xv0,
                 std::vector<double> yv0, double delta, double E,
                 int systemsize, double& time, int cachesize) {
  double r;
  double pi = atan(1) * 4;
  double raggio = delta / 2;
  double C = 1 / (E * pi * raggio);

  // MULTITHREADING
  // Reading in time
  auto start = std::chrono::high_resolution_clock::now();
  
#pragma omp parallel for schedule(dynamic, cachesize)
  for (int i = 0; i < systemsize; i++) {
    A(i, i) = 1 * C;
  }

  // Writing time to console
  auto finish = std::chrono::high_resolution_clock::now();
  time = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
  // Hint: Micro is 10^-6
  
  // std::chrono::duration<double> elapsed = finish - start;
  // time = elapsed.count();
  
  // std::cout << "\n	Calculating time for setting up A. \n";
  // std::cout << "Elapsed time: " << time << " s\n"; 
  // MULTITHREADING END
  		  
  for (int i = 0; i < systemsize; i++) {
    for (int j = 0; j < i; j++) {
      r = sqrt(pow((xv0[j] - xv0[i]), 2) + pow((yv0[j] - yv0[i]), 2));
      A(i, j) = C * asin(raggio / r);
      A(j, i) = C * asin(raggio / r);
    }
  }
}

void SetUpMatrix_Guided(Epetra_SerialDenseMatrix& A, std::vector<double> xv0,
                 std::vector<double> yv0, double delta, double E,
                 int systemsize, double& time, int cachesize) {
  double r;
  double pi = atan(1) * 4;
  double raggio = delta / 2;
  double C = 1 / (E * pi * raggio);

  // MULTITHREADING
  // Reading in time
  auto start = std::chrono::high_resolution_clock::now();
  
#pragma omp parallel for schedule(guided, cachesize)
  for (int i = 0; i < systemsize; i++) {
    A(i, i) = 1 * C;
  }

  // Writing time to console
  auto finish = std::chrono::high_resolution_clock::now();
  time = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
  // Hint: Micro is 10^-6
  
  // std::chrono::duration<double> elapsed = finish - start;
  // time = elapsed.count();
  
  // std::cout << "\n	Calculating time for setting up A. \n";
  // std::cout << "Elapsed time: " << time << " s\n"; 
  // MULTITHREADING END
  		  
  for (int i = 0; i < systemsize; i++) {
    for (int j = 0; j < i; j++) {
      r = sqrt(pow((xv0[j] - xv0[i]), 2) + pow((yv0[j] - yv0[i]), 2));
      A(i, j) = C * asin(raggio / r);
      A(j, i) = C * asin(raggio / r);
    }
  }
}

void calculateTimes_Static(double& elapsedTime1, double& elapsedTime2, int cachesize, string randomPath) {
	// Setup constants
	int csteps, flagwarm;
	double nu1, nu2, G1, G2, E, G, nu, alpha, H, rnd, k_el, delta, nnodi, to1, E1,
	      E2, lato, zref, ampface, errf;
	double Delta = 50;  // TODO only used for debugging
	
	// std::cout << "Test file for generating data is: " + randomPath + ".\n";

	SetParameters(E1, E2, csteps, flagwarm, lato, zref, ampface, nu1, nu2, G1, G2,
	                E, G, nu, alpha, H, rnd, k_el, delta, nnodi, errf, to1);
	vector<double> x;
	for (double i = delta / 2; i < lato; i = i + delta) {
		x.push_back(i);
	}

	// Setup Topology
	Epetra_SerialDenseMatrix topology, y;
	CreateTopology(topology.N(), topology, randomPath);
	
	double zmax = 0;
	double zmean = 0;
	
	for (int i = 0; i < topology.M(); i++) {
		for (int j = 0; j < topology.N(); j++) {
				zmean += topology(i, j);
				if (topology(i, j) > zmax) zmax = topology(i, j);
		}
	}
	zmean = zmean / (topology.N() * topology.M());
	
	vector<double> force0, area0;
	double w_el = 0, area = 0, force = 0;
	int k = 0;
	int n0;
	std::vector<double> xv0, yv0, b0, x0, xvf, yvf, pf;  // x0: initialized in Warmstart!
	double nf, xvfaux, yvfaux, pfaux;
	Epetra_SerialDenseMatrix A;
	// At this point would have been a while-loop, but not necessary since only one calculation
	// is necessary
	
	// while(...)
	vector<int> col, row;
	double value = zmax - Delta - w_el;
	
	for (int i = 0; i < topology.N(); i++) {
		//      cout << "x= " << x[i] << endl;
		for (int j = 0; j < topology.N(); j++) {
			if (topology(i, j) >= value) {
				row.push_back(i);
				col.push_back(j);
			}
		}
	}
	n0 = col.size();
	
	for (int b = 0; b < n0; b++) {
		xv0.push_back(x[col[b]]);
	}
	for (int b = 0; b < n0; b++) {
		yv0.push_back(x[row[b]]);
	}
	
	for (int b = 0; b < n0; b++) {
		b0.push_back(Delta + w_el - (zmax - topology(row[b], col[b])));
	}
	
	int err = A.Shape(xv0.size(), xv0.size());
	
	// Construction of the Matrix H = A
	SetUpMatrix_Static(A, xv0, yv0, delta, E, n0, elapsedTime1, cachesize);
	
	// MULTITHREADING
	y.Shape(A.M(), 1);
	auto start = std::chrono::high_resolution_clock::now();
	
#pragma omp parallel for schedule(static, cachesize)
	// y = A * b (Matrix * Vector)
	for (int a = 0; a < A.M(); a++) {
		for (int b = 0; b < A.N(); b++) {
			y(a, 0) += (A(b, a) * b0[b]);
		}
	}
	
	// Writing time to console
	auto finish = std::chrono::high_resolution_clock::now();
	elapsedTime2 = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
	// Hint: Micro is 10^-6
	
	// std::chrono::duration<double> elapsed = finish - start;
	// elapsedTime2 = elapsed.count();
	
	// MULTITHREADING END
}

void calculateTimes_Dynamic(double& elapsedTime1, double& elapsedTime2, int cachesize, string randomPath) {
	// Setup constants
	int csteps, flagwarm;
	double nu1, nu2, G1, G2, E, G, nu, alpha, H, rnd, k_el, delta, nnodi, to1, E1,
	      E2, lato, zref, ampface, errf;
	double Delta = 50;  // TODO only used for debugging
	
	// std::cout << "Test file for generating data is: " + randomPath + ".\n";

	SetParameters(E1, E2, csteps, flagwarm, lato, zref, ampface, nu1, nu2, G1, G2,
	                E, G, nu, alpha, H, rnd, k_el, delta, nnodi, errf, to1);
	vector<double> x;
	for (double i = delta / 2; i < lato; i = i + delta) {
		x.push_back(i);
	}

	// Setup Topology
	Epetra_SerialDenseMatrix topology, y;
	CreateTopology(topology.N(), topology, randomPath);
	
	double zmax = 0;
	double zmean = 0;
	
	for (int i = 0; i < topology.M(); i++) {
		for (int j = 0; j < topology.N(); j++) {
				zmean += topology(i, j);
				if (topology(i, j) > zmax) zmax = topology(i, j);
		}
	}
	zmean = zmean / (topology.N() * topology.M());
	
	vector<double> force0, area0;
	double w_el = 0, area = 0, force = 0;
	int k = 0;
	int n0;
	std::vector<double> xv0, yv0, b0, x0, xvf, yvf, pf;  // x0: initialized in Warmstart!
	double nf, xvfaux, yvfaux, pfaux;
	Epetra_SerialDenseMatrix A;
	// At this point would have been a while-loop, but not necessary since only one calculation
	// is necessary
	
	// while(...)
	vector<int> col, row;
	double value = zmax - Delta - w_el;
	
	for (int i = 0; i < topology.N(); i++) {
		//      cout << "x= " << x[i] << endl;
		for (int j = 0; j < topology.N(); j++) {
			if (topology(i, j) >= value) {
				row.push_back(i);
				col.push_back(j);
			}
		}
	}
	n0 = col.size();
	
	for (int b = 0; b < n0; b++) {
		xv0.push_back(x[col[b]]);
	}
	for (int b = 0; b < n0; b++) {
		yv0.push_back(x[row[b]]);
	}
	
	for (int b = 0; b < n0; b++) {
		b0.push_back(Delta + w_el - (zmax - topology(row[b], col[b])));
	}
	
	int err = A.Shape(xv0.size(), xv0.size());
	
	// Construction of the Matrix H = A
	SetUpMatrix_Dynamic(A, xv0, yv0, delta, E, n0, elapsedTime1, cachesize);
	
	// MULTITHREADING
	y.Shape(A.M(), 1);
	auto start = std::chrono::high_resolution_clock::now();
	
#pragma omp parallel for schedule(dynamic, cachesize)
	// y = A * b (Matrix * Vector)
	for (int a = 0; a < A.M(); a++) {
		for (int b = 0; b < A.N(); b++) {
			y(a, 0) += (A(b, a) * b0[b]);
		}
	}
	
	// Writing time to console
	auto finish = std::chrono::high_resolution_clock::now();
	elapsedTime2 = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
	// Hint: Micro is 10^-6
	
	// std::chrono::duration<double> elapsed = finish - start;
	// elapsedTime2 = elapsed.count();
	
	// MULTITHREADING END
}

void calculateTimes_Guided(double& elapsedTime1, double& elapsedTime2, int cachesize, string randomPath) {
	// Setup constants
	int csteps, flagwarm;
	double nu1, nu2, G1, G2, E, G, nu, alpha, H, rnd, k_el, delta, nnodi, to1, E1,
	      E2, lato, zref, ampface, errf;
	double Delta = 50;  // TODO only used for debugging
	
	// std::cout << "Test file for generating data is: " + randomPath + ".\n";

	SetParameters(E1, E2, csteps, flagwarm, lato, zref, ampface, nu1, nu2, G1, G2,
	                E, G, nu, alpha, H, rnd, k_el, delta, nnodi, errf, to1);
	vector<double> x;
	for (double i = delta / 2; i < lato; i = i + delta) {
		x.push_back(i);
	}

	// Setup Topology
	Epetra_SerialDenseMatrix topology, y;
	CreateTopology(topology.N(), topology, randomPath);
	
	double zmax = 0;
	double zmean = 0;
	
	for (int i = 0; i < topology.M(); i++) {
		for (int j = 0; j < topology.N(); j++) {
				zmean += topology(i, j);
				if (topology(i, j) > zmax) zmax = topology(i, j);
		}
	}
	zmean = zmean / (topology.N() * topology.M());
	
	vector<double> force0, area0;
	double w_el = 0, area = 0, force = 0;
	int k = 0;
	int n0;
	std::vector<double> xv0, yv0, b0, x0, xvf, yvf, pf;  // x0: initialized in Warmstart!
	double nf, xvfaux, yvfaux, pfaux;
	Epetra_SerialDenseMatrix A;
	// At this point would have been a while-loop, but not necessary since only one calculation
	// is necessary
	
	// while(...)
	vector<int> col, row;
	double value = zmax - Delta - w_el;
	
	for (int i = 0; i < topology.N(); i++) {
		//      cout << "x= " << x[i] << endl;
		for (int j = 0; j < topology.N(); j++) {
			if (topology(i, j) >= value) {
				row.push_back(i);
				col.push_back(j);
			}
		}
	}
	n0 = col.size();
	
	for (int b = 0; b < n0; b++) {
		xv0.push_back(x[col[b]]);
	}
	for (int b = 0; b < n0; b++) {
		yv0.push_back(x[row[b]]);
	}
	
	for (int b = 0; b < n0; b++) {
		b0.push_back(Delta + w_el - (zmax - topology(row[b], col[b])));
	}
	
	int err = A.Shape(xv0.size(), xv0.size());
	
	// Construction of the Matrix H = A
	SetUpMatrix_Guided(A, xv0, yv0, delta, E, n0, elapsedTime1, cachesize);
	
	// MULTITHREADING
	y.Shape(A.M(), 1);
	auto start = std::chrono::high_resolution_clock::now();
	
#pragma omp parallel for schedule(guided, cachesize)
	// y = A * b (Matrix * Vector)
	for (int a = 0; a < A.M(); a++) {
		for (int b = 0; b < A.N(); b++) {
			y(a, 0) += (A(b, a) * b0[b]);
		}
	}
	
	// Writing time to console
	auto finish = std::chrono::high_resolution_clock::now();
	elapsedTime2 = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
	// Hint: Micro is 10^-6
	
	// std::chrono::duration<double> elapsed = finish - start;
	// elapsedTime2 = elapsed.count();
	
	// MULTITHREADING END
}

double generateMinimum(vector<double> values){
	int min = values[0];
	for (int i = 1; i < values.size(); i++){
		if (min > values[i]) { min = values[i]; }
	}
	return min;
}

vector<int> readinP(string filePath){
	vector<int> values;
	ifstream stream(filePath);
	string line;
	while (getline(stream, line)) {	
		values.push_back(stoi(line));	
	}
	stream.close();
	return values;
}

void writeToFile(string filepath, Epetra_SerialDenseMatrix values, int dim1, int dim2){
	// Write file in MatLab style to increase data flexibility and handling
	ofstream outfile; outfile.open(filepath);
	outfile << std::scientific;
	for (int y = 0; y < dim2; y++){
		for (int x = 0; x < dim1; x++){
			if (x == (dim1 - 1)){
				outfile << to_string(values(x, y)) << endl;
			} else {
				outfile << to_string(values(x, y)) + ",";
			}
		}
	}
	outfile.close();
}

Epetra_SerialDenseMatrix generateMatrix(string randomPath, Epetra_SerialDenseMatrix& b1, int& count){
	omp_set_num_threads(2);

	  int csteps, flagwarm;
	  double nu1, nu2, G1, G2, E, G, nu, alpha, H, rnd, k_el, delta, nnodi, to1, E1,
	      E2, lato, zref, ampface, errf;

	  double Delta = 50;  // TODO only used for debugging

	  SetParameters(E1, E2, csteps, flagwarm, lato, zref, ampface, nu1, nu2, G1, G2,
	                E, G, nu, alpha, H, rnd, k_el, delta, nnodi, errf, to1);

	  // Meshgrid-Command
	  // Identical Vectors/Matricies, therefore only created one here.
	  vector<double> x;
	  for (double i = delta / 2; i < lato; i = i + delta) {
	    x.push_back(i);
	  }

	  // Setup Topology
	  Epetra_SerialDenseMatrix topology, y;
	  CreateTopology(topology.N(), topology, randomPath);

	  double zmax = 0;
	  double zmean = 0;

	  for (int i = 0; i < topology.M(); i++)
	    for (int j = 0; j < topology.N(); j++) {
	      {
	        zmean += topology(i, j);
	        if (topology(i, j) > zmax) zmax = topology(i, j);
	      }
	    }
	  zmean = zmean / (topology.N() * topology.M());

	  vector<double> force0, area0;
	  double w_el = 0;
	  double area = 0, force = 0;
	  int k = 0;
	  int n0;
	    Epetra_SerialDenseMatrix b0new;
	  std::vector<double> xv0, yv0, b0, x0, xvf, yvf,
	      pf;  // x0: initialized in Warmstart!
	  double nf, xvfaux, yvfaux, pfaux;

	  Epetra_SerialDenseMatrix A;

	  while (errf > to1 && k < 100) {
	    // First predictor for contact set
	    // All points, for which gap is bigger than the displacement of the rigid
	    // indenter, cannot be in contact and thus are not checked in nonlinear
	    // solve
	    // @{

	    // [ind1,ind2]=find(z>=(zmax-(Delta(s)+w_el(k))));
	    vector<int> col, row;
	    double value = zmax - Delta - w_el;

	    row.clear();
	    col.clear();
	    for (int i = 0; i < topology.N(); i++) {
	      //      cout << "x= " << x[i] << endl;
	      for (int j = 0; j < topology.N(); j++) {
	        if (topology(i, j) >= value) {
	          row.push_back(i);
	          col.push_back(j);
	        }
	      }
	    }
	    n0 = col.size();

	    // Works until this point

	    xv0.clear();
	    yv0.clear();
	    b0.clear();
	    for (int b = 0; b < n0; b++) {
	      xv0.push_back(x[col[b]]);
	    }

	    for (int b = 0; b < n0; b++) {
	      yv0.push_back(x[row[b]]);
	    }

	    for (int b = 0; b < n0; b++) {
	      b0.push_back(Delta + w_el - (zmax - topology(row[b], col[b])));
	    }

	    int err = A.Shape(xv0.size(), xv0.size());

	    // Construction of the Matrix H = A
	    SetUpMatrix(A, xv0, yv0, delta, E, n0, k);

	    // Second predictor for contact set
	    // @{

	    x0.clear();
	    x0.resize(b0.size());

	    b0new.Shape(b0.size(), 1);

	    for (int i = 0; i < b0.size(); i++) {
	      b0new(i, 0) = b0[i];
	    }

	    Epetra_SerialDenseMatrix w;
	    NonlinearSolve(A, b0new, x0, w, y, count);  // y -> sol, w -> wsol; x0 -> y0

	    // Compute residial
	    // @{
	    Epetra_SerialDenseMatrix res1;
	    if (A.M() != y.N()) {
	      std::runtime_error("Error 1: Matrix dimensions imcompatible");
	    }
	    res1.Shape(A.M(), A.M());
	    // res1=A*sol-b0(:,k)-wsol;
	    // Should work now.

	    for (int x = 0; x < A.N(); x++) {
	      for (int z = 0; z < y.M(); z++) {
	        res1(x, 0) += A(x, z) * y(z, 0);
	      }
	      res1(x, 0) -= b0new(x, 0) + w(x, 0);  // [...]-b0(:,k) - wsol;
	    }
	    // }

	    // Compute number of contact nodes
	    // @{
	    int cont = 0;
	    xvf.clear();
	    yvf.clear();
	    xvf.resize(y.M());
	    yvf.resize(y.M());
	    pf.resize(y.M());
	    for (int i = 0; i < y.M(); i++) {
	      if (y(i, 0) != 0) {
	        xvf[cont] = xv0[i];
	        yvf[cont] = yv0[i];
	        pf[cont] = y(i, 0);
	        cont += 1;
	      }
	    }
	    nf = cont;

	    // }

	    // Compute contact force and contact area
	    // @{
	    force0.push_back(0);
	    for (int i = 0; i < nf; i++) {
	      force0[k] = force0[k] + pf[i];
	    }
	    area0.push_back(nf * (pow(delta, 2) / pow(lato, 2)) * 100);
	    w_el = force0[k] / k_el;
	    // }

	    // Compute error because of nonlinear correction
	    // @{
	    if (k > 0) {
	      errf = abs(force0[k] - force0[k - 1]) / force0[k];

	      // errw(k) = abs((w_el0(k+1)-w_el0(k))/w_el0(k+1));
	      // It appears that this is only a debugging variable without any uses,
	      // therefore im not gonna implement this here.
	    }
	    k += 1;
	    // }
	  }
	  b1 = b0new;
	  return A;
}

void calculateTimes2_Static(Epetra_SerialDenseMatrix& matrix, Epetra_SerialDenseMatrix& y,
		Epetra_SerialDenseMatrix& b0, vector<int>& P, double& time, int cachesize, int counter){
	
	Epetra_SerialDenseMatrix w; w.Shape(matrix.M(), 1);
	auto start = std::chrono::high_resolution_clock::now();
	
#pragma omp parallel for schedule(static, cachesize)
	
	for (int a = 0; a < matrix.M(); a++) {
		w(a, 0) = 0;
		for (int b = 0; b < counter; b++) {
			w(a, 0) += (matrix(P[b], a) * y(P[b], 0));
		}
		w(a, 0) -= b0(a, 0);
	}
	
	auto finish = std::chrono::high_resolution_clock::now();
	time = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
}

void calculateTimes2_Dynamic(Epetra_SerialDenseMatrix matrix, Epetra_SerialDenseMatrix y,
		Epetra_SerialDenseMatrix b0, vector<int> P, double& time, int cachesize, int counter){
	Epetra_SerialDenseMatrix w; w.Shape(matrix.M(), 1);
	
	auto start = std::chrono::high_resolution_clock::now();
	
#pragma omp parallel for schedule(dynamic, cachesize)
	
	for (int a = 0; a < matrix.M(); a++) {
		w(a, 0) = 0;
		for (int b = 0; b < counter; b++) {
			w(a, 0) += (matrix(P[b], a) * y(P[b], 0));
		}
		w(a, 0) -= b0(a, 0);
	}
	
	auto finish = std::chrono::high_resolution_clock::now();
	time = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
}

void calculateTimes2_Guided(Epetra_SerialDenseMatrix matrix, Epetra_SerialDenseMatrix y,
		Epetra_SerialDenseMatrix b0, vector<int> P, double& time, int cachesize, int counter){
	Epetra_SerialDenseMatrix w; w.Shape(matrix.M(), 1);
	
	auto start = std::chrono::high_resolution_clock::now();
	
#pragma omp parallel for schedule(guided, cachesize)
	
	for (int a = 0; a < matrix.M(); a++) {
		w(a, 0) = 0;
		for (int b = 0; b < counter; b++) {
			w(a, 0) += (matrix(P[b], a) * y(P[b], 0));
		}
		w(a, 0) -= b0(a, 0);
	}
	
	auto finish = std::chrono::high_resolution_clock::now();
	time = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count();
}

Epetra_SerialDenseMatrix generateY(Epetra_SerialDenseMatrix matrix, vector<int> P,
		Epetra_SerialDenseMatrix b0){
	Epetra_SerialDenseMatrix y; y. Shape(matrix.N(), 1);
	for (int a = 0; a < matrix.N(); a++){
		for (int b = 0; b < matrix.M(); b++){
			y(a, 0) = matrix(P[a], P[b]) * b0(P[b], 0);
		}
	}
	return y;
}

int main(int argc, char* argv[]) {
	// Setup Thread Amount and Cache_Size
	int maxThreads = 12, maxCache = 18;
	double time1 = 0, time2 = 0, min1 = 0, min2 = 0;
	vector<double> times1, times2, mins1, mins2;
	Epetra_SerialDenseMatrix matrix1, matrix2;
	string filePath = "sup7.dat";
	matrix1.Shape(maxCache, maxThreads); matrix2.Shape(maxCache, maxThreads);
	
	std::cout << "Generating pre-work data." << endl;
	
	// PHASE 2
	Epetra_SerialDenseMatrix matrix, y, b0;
	vector<int> P = readinP("p_" + filePath);
	int counter = 0;
	try{
		
	} catch 
	matrix = generateMatrix(filePath, b0, counter);
	
	std::cout << "Generating y" << endl;
	
	y = generateY(matrix, P, b0);
	
	std::cout << "Starting Static Runtime" << endl;
	
	/*
	 * PHASE 1
	for (int cachesize = 1; cachesize < (maxCache + 1); cachesize++){
		for (int threadAmount = 1; threadAmount < (maxThreads + 1); threadAmount++){
			// Generate runtime-data
			omp_set_num_threads(threadAmount);
			for (int i = 0; i < 100; i++){ // Should be sufficient
				calculateTimes_Static(time1, time2, cachesize, filePath);
				times1.push_back(time1);
				times2.push_back(time2);
			}
			min1 = generateMinimum(times1);
			min2 = generateMinimum(times2);
			// std::cout << to_string(min1) << endl;
			matrix1(cachesize, threadAmount) = min1;
			matrix2(cachesize, threadAmount) = min2;
			min1 = 0; times1.clear();
			min2 = 0; times2.clear();
			std::cout << "Thread " + to_string(threadAmount) + " done." << endl;
		}
		std::cout << "Cache " + to_string(cachesize) + " done." << endl;
	}
			
	writeToFile("datatimes1_static_" + filePath, matrix1, maxCache, maxThreads);
	writeToFile("datatimes2_static_" + filePath, matrix2, maxCache, maxThreads);
	*/
	
	// PHASE 2
	
	for (int threadAmount = 1; threadAmount < (maxThreads + 1); threadAmount++){
		omp_set_num_threads(threadAmount);
		for (int cachesize = 1; cachesize < (maxCache + 1); cachesize++){
			for (int i = 0; i < 100; i++){
				calculateTimes2_Static(matrix, y, b0, P, time1, cachesize, counter); // TODO: y, b0, P
				times1.push_back(time1);
				std::cout << "Cache " + to_string(cachesize) + " done." << endl;
			}
			min1 = generateMinimum(times1);
			// std::cout << to_string(min1) << endl;
			matrix1(cachesize, threadAmount) = min1;
			min1 = 0; times1.clear();
		}
		std::cout << "Thread " + to_string(threadAmount) + " done." << endl;
	}
	
	writeToFile("datatimes3_static_" + filePath, matrix1, maxCache, maxThreads);
	
	std::cout << "Starting Dynamic Runtime" << endl;
		
	/*
	for (int cachesize = 1; cachesize < (maxCache + 1); cachesize++){
		for (int threadAmount = 1; threadAmount < (maxThreads + 1); threadAmount++){
			// Generate runtime-data
			omp_set_num_threads(threadAmount);
			for (int i = 0; i < 100; i++){ // Should be sufficient
				calculateTimes_Dynamic(time1, time2, cachesize, filePath);
				times1.push_back(time1);
				times2.push_back(time2);
			}
			min1 = generateMinimum(times1);
			min2 = generateMinimum(times2);
			// std::cout << to_string(min1) << endl;
			matrix1(cachesize, threadAmount) = min1;
			matrix2(cachesize, threadAmount) = min2;
			min1 = 0; times1.clear();
			min2 = 0; times2.clear();
			std::cout << "Thread " + to_string(threadAmount) + " done." << endl;
		}
		std::cout << "Cache " + to_string(cachesize) + " done." << endl;
	}
				
	writeToFile("datatimes1_dynamic_" + filePath, matrix1, maxCache, maxThreads);
	writeToFile("datatimes2_dynamic_" + filePath, matrix2, maxCache, maxThreads);
	*/
	
	// PHASE 2
	for (int threadAmount = 1; threadAmount < (maxThreads + 1); threadAmount++){
		omp_set_num_threads(threadAmount);
		for (int cachesize = 1; cachesize < (maxCache + 1); cachesize++){
			for (int i = 0; i < 100; i++){
				calculateTimes2_Dynamic(matrix, y, b0, P, time1, cachesize, counter); // TODO: y, b0, P
				times1.push_back(time1);
				std::cout << "Cache " + to_string(cachesize) + " done." << endl;
			}
			min1 = generateMinimum(times1);
			// std::cout << to_string(min1) << endl;
			matrix1(cachesize, threadAmount) = min1;
			min1 = 0; times1.clear();
		}
		std::cout << "Thread " + to_string(threadAmount) + " done." << endl;
	}
	
	writeToFile("datatimes3_dynamic_" + filePath, matrix1, maxCache, maxThreads);
	
	std::cout << "Starting Guided Runtime" << endl;
	
	/*
	for (int cachesize = 1; cachesize < (maxCache + 1); cachesize++){
		for (int threadAmount = 1; threadAmount < (maxThreads + 1); threadAmount++){
			// Generate runtime-data
			omp_set_num_threads(threadAmount);
			for (int i = 0; i < 100; i++){ // Should be sufficient
				calculateTimes_Static(time1, time2, cachesize, filePath);
				times1.push_back(time1);
				times2.push_back(time2);
			}
			min1 = generateMinimum(times1);
			min2 = generateMinimum(times2);
			// std::cout << to_string(min1) << endl;
			matrix1(cachesize, threadAmount) = min1;
			matrix2(cachesize, threadAmount) = min2;
			min1 = 0; times1.clear();
			min2 = 0; times2.clear();
			std::cout << "Thread " + to_string(threadAmount) + " done." << endl;
		}
		std::cout << "Cache " + to_string(cachesize) + " done." << endl;
	}
			
	writeToFile("datatimes1_guided_" + filePath, matrix1, maxCache, maxThreads);
	writeToFile("datatimes2_guided_" + filePath, matrix2, maxCache, maxThreads);
	*/
	
	// PHASE 2
	for (int threadAmount = 1; threadAmount < (maxThreads + 1); threadAmount++){
		omp_set_num_threads(threadAmount);
		for (int cachesize = 1; cachesize < (maxCache + 1); cachesize++){
			for(int i = 0; i < 100; i++){
				calculateTimes2_Guided(matrix, y, b0, P, time1, cachesize, counter); // TODO: y, b0, P
				times1.push_back(time1);
				std::cout << "Cache " + to_string(cachesize) + " done." << endl;
			}
			min1 = generateMinimum(times1);
			// std::cout << to_string(min1) << endl;
			matrix1(cachesize, threadAmount) = min1;
			min1 = 0; times1.clear();
		}
		std::cout << "Thread " + to_string(threadAmount) + " done." << endl;
	}
	
	writeToFile("datatimes3_guided_" + filePath, matrix1, maxCache, maxThreads);
	
	std::cout << "All jobs done!" << endl;
}
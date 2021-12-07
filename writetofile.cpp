#include <fstream>                       //ifstream
#include <iostream>                      //ifstream
#include <string>                        //std::to_string, std::stod
#include <vector>                        // Seems obvious
#include <Epetra_SerialSpdDenseSolver.h> // Seems obvious
#include <Epetra_SerialSymDenseMatrix.h> // Seems obvious
using namespace std;
#include "writetofile.h"

void writeForceToFile(Epetra_SerialDenseMatrix &y, string pathName)
{

  int i;

  std::size_t botDirPos = pathName.find_last_of("/") + 1;
  // get directory
  std::string dir = pathName.substr(0, botDirPos);
  // get file name
  std::string dat_file = pathName.substr(botDirPos, pathName.length());

std:
  string file = dir + "result_force_" + dat_file;

  ofstream outfile;
  outfile.open(file, std::ofstream::trunc);

  for (int i = 0; i < y.M(); i++)
  {
    if (y(i, 0) != 0)
    {
      outfile << y(i, 0) << ";";
    }
  }
}
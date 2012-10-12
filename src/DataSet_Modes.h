#ifndef INC_DATASET_MODES_H
#define INC_DATASET_MODES_H
#include "DataSet.h"
#include "Frame.h"
#include "DataSet_Matrix.h"
/// Hold eigenvalues/eigenvectors and optionally averaged coords.
class DataSet_Modes : public DataSet {
  public:
    DataSet_Modes();
    ~DataSet_Modes();

    // ---------- DataSet routines
    int Size() { return nmodes_; }
    void WriteToFile(CpptrajFile&);
    // ---------------------------

    void SetAvgCoords(int, const double*);
    int CalcEigen(DataSet_Matrix&,int); // TODO: Make const ref
    void PrintModes();
    int EigvalToFreq();
    int MassWtEigvect( const double* );
    int ReduceCovar();
    int ReduceDistCovar(int);

    void SetType( DataSet_Matrix::MatrixType typeIn ) { type_ = typeIn; }
    int Nmodes() { return nmodes_; }
    const double* Eigenvalues() { return evalues_; }
    double Eigenvalue(int i) { return evalues_[i]; }
    const double* Eigenvector(int i) {
      return evectors_ + (i * vecsize_);
    }
  private:
    Frame avg_;
    double* evalues_;
    double* evectors_;
    int nmodes_;
    int vecsize_;
    DataSet_Matrix::MatrixType type_;
    bool reduced_;
};
#endif

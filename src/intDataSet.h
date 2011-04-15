#ifndef INC_INTDATASET_H
#define INC_INTDATASET_H
/* Class: intDataSet
 * Test of the C++ STL map class instead of a straight array of ints. This
 * will allow Y values with non-consecutive X values to be stored. This is the
 * case e.g. when an action is not active for a certain part of the analysis.
 */
#include <map>
#include "DataSet.h"
class intDataSet : public DataSet {
    std::map<int,int> Data;
    std::map<int,int>::iterator it;
  public:
    intDataSet();

    int Xmax();
    int isEmpty(int);
    void Add( int, void * );
    char *Write(char *, int);
    int Width();
    int Sync();
};
#endif

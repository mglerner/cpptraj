#include <cstdio> //sprintf
#include <cmath> // exp
#include "Action_Rms2d.h"
#include "CpptrajStdio.h"
#include "ProgressBar.h"

// CONSTRUCTOR
Rms2d::Rms2d() {
  //fprintf(stderr,"Rms2d Con\n");
  nofit=false;
  useMass_=false;
  mass_setup=false;
  rmsdFile=NULL;
  RefTraj=NULL;
  RefParm=NULL;
  corrfilename=NULL;
  mass_ptr=NULL;
} 

// DESTRUCTOR
Rms2d::~Rms2d() {
  if (RefTraj!=NULL) delete RefTraj; 
}

// Rms2d::SeparateInit()
/** For use when not part of the action list, i.e. using the Rms2d action
  * to calculate a distance matrix for e.g. clustering.
  */
int Rms2d::SeparateInit(bool nofitIn, char *maskIn) {
  isSeparate_ = true;
  useMass_=false;
  nofit = nofitIn;
  FrameMask.SetMaskString(maskIn);
  return 0;
};

// Rms2d::init()
/** Expected call: rms2d <mask> <refmask> rmsout <filename> [nofit] 
  *                [reftraj <traj> [parm <parmname> | parmindex <#>]]
  *                [corr <corrfilename>] 
  */
// Dataset name will be the last arg checked for. Check order is:
//    1) Keywords
//    2) Masks
//    3) Dataset name
int Rms2d::init() {
  char *mask0, *maskRef, *reftraj;

  // Get keywords
  nofit = actionArgs.hasKey("nofit");
  useMass_ = actionArgs.hasKey("mass"); 
  rmsdFile = actionArgs.getKeyString("rmsout",NULL);
  reftraj = actionArgs.getKeyString("reftraj",NULL);
  if (reftraj!=NULL) {
    RefParm = PFL->GetParm(actionArgs);
    if (RefParm==NULL) {
      mprinterr("Error: Rms2d: Could not get parm for reftraj %s.\n",reftraj);
      return 1;
    }
  }
  // Check for correlation; if so, reftraj not supported
  corrfilename = actionArgs.getKeyString("corr",NULL);
  if (corrfilename!=NULL && reftraj!=NULL) {
    mprinterr("Error: Rms2d: Keyword 'corr' not supported with 'reftraj'\n");
    return 1;
  }
  // Require an output filename if corr not specified
  if (rmsdFile==NULL && corrfilename==NULL) {
    mprinterr("Error: Rms2d: No output filename specified; use 'rmsout' keyword.\n");
    return 1;
  }

  // Get the RMS mask string for frames
  mask0 = actionArgs.getNextMask();
  FrameMask.SetMaskString(mask0);

  // Check if reference will be a series of frames from a trajectory
  if (reftraj!=NULL) {
    // Get RMS mask string for reference trajectory
    maskRef = actionArgs.getNextMask();
    // If no reference mask specified, make same as RMS mask
    if (maskRef==NULL) maskRef=mask0;
    RefMask.SetMaskString(maskRef);
    // Attempt to set up reference trajectory
    RefTraj = new TrajectoryFile();
    if (RefTraj->SetupRead(reftraj, NULL, RefParm)) {
      mprinterr("Error: Rms2d: Could not set up reftraj %s.\n",reftraj);
      return 1;
    }
  }

  mprintf("    RMS2D: Mask [%s]",FrameMask.MaskString());
  if (reftraj!=NULL) {
    // Set up reference trajectory and open
    mprintf(", ref traj %s (mask [%s]) %i frames",RefTraj->TrajName(),
            RefMask.MaskString(),RefTraj->Total_Read_Frames());
  }
  if (nofit)
    mprintf(" (no fitting)");
  if (useMass_)
    mprintf(" (mass-weighted)");
  if (rmsdFile!=NULL) 
    mprintf(" output to %s",rmsdFile);
  mprintf("\n");
  if (corrfilename!=NULL)
    mprintf("           RMSD auto-correlation will be calculated and output to %s\n",
            corrfilename);

  return 0;
}

// Rms2d::setup()
/** Set up frame mask so that only selected atoms in frames will be stored.
  */
int Rms2d::setup() {
  if ( currentParm->SetupIntegerMask(FrameMask) ) {
    mprinterr("Error: Rms2d::setup: Could not set up mask [%s] for parm %s\n",
              FrameMask.MaskString(), currentParm->c_str());
    return 1;
  }
  if (FrameMask.None()) {
    mprinterr("Error: Rms2d::setup: No atoms selected for mask [%s], parm %s\n",
              FrameMask.MaskString(), currentParm->c_str());
    return 1;
  }
  // If useMass, store mass information here. If mass information changes
  // (i.e. a different parm than the first is loaded) this makes calculating
  // the 2drms much more complicated since each frame could have a different
  // set of masses. To simplify things, only allow mass-weighting when reading
  // one parm.
  if (useMass_) {
    if (!mass_setup) {
      mass_ptr = currentParm->Mass();
      mass_setup = true;
    } else {
      mprintf("Warning: Rms2d::Setup: 'mass' only allowed with one parm. Disabling 'mass'.\n");
      mass_ptr=NULL;
    }
  }
  return 0;  
}

// Rms2d::action()
/** Store current frame coords according to mask.
  */
int Rms2d::action() {
 
  if (ReferenceCoords.AddFrameByMask(*currentFrame, FrameMask)) 
    return 1;

  return 0;
} 

// Rms2d::Calc2drms()
/** Calculate the RMSD of each frame in ReferenceCoords to each other frame.
  * Since this results in a symmetric matrix use TriangleMatrix to store
  * results.
  */
void Rms2d::Calc2drms(TriangleMatrix *Distances) {
  Frame RefFrame;
  Frame TgtFrame;
  double U[9], Trans[6];
  float R;
 
  int totalref = ReferenceCoords.Ncoords();
  Distances->Setup( totalref );
  int max = Distances->Nelements();
  mprintf("  RMS2D: Calculating RMSDs between each frame (%i total).\n  ",max);

  // Set up progress Bar
  ProgressBar *progress = new ProgressBar(max);
  int current = 0;
  progress->Update(current);

  if (mass_ptr!=NULL) {
    // Set up mass info. If mass info present this means only 1 parm used,
    // so tgt and ref will always have same # of atoms. Use first frame
    // in ref coords to set up target and reference frames.
    useMass_=true;
    RefFrame.SetupFrameFromMask(FrameMask,mass_ptr);
    TgtFrame.SetupFrameFromMask(FrameMask,mass_ptr);
    // If no mass info ensure that RefFrame and TgtFrame are large enough to
    // hold the largest set of coords in ReferenceCoords. No mass.
  } else {
    useMass_=false;
    int maxrefnatom = ReferenceCoords.MaxNatom();
    RefFrame.SetupFrame( maxrefnatom );
    TgtFrame.SetupFrame( maxrefnatom );
  }

  // LOOP OVER REFERENCE FRAMES
  for (int nref=0; nref < totalref - 1; nref++) {
    // Get the current reference frame
    RefFrame = ReferenceCoords[nref];
    // Select and pre-center reference atoms (if fitting)
    if (!nofit)
      RefFrame.CenterReference(Trans+3, useMass_);
  
    // LOOP OVER TARGET FRAMES
    for (int nframe=nref+1; nframe < totalref; nframe++) {
      ++current;
      // Get the current target frame
      TgtFrame = ReferenceCoords[nframe];
      // Ensure # ref atoms == # tgt atoms
      if (RefFrame.Natom() != TgtFrame.Natom()) {
        mprintf("\tWarning: Rms2d: # atoms in ref %i (%i) != # atoms in tgt %i (%i)\n",
                nref+1,RefFrame.Natom(),nframe+1,TgtFrame.Natom());
        mprintf("\t         Assigning this pair RMSD of -1.0\n");
        R = -1.0;
      } else if (nofit) {
        // Perform no fit RMS calculation
        R = (float) TgtFrame.RMSD(&RefFrame, useMass_);
      } else {
        // Perform fit RMS calculation
        R = (float) TgtFrame.RMSD_CenteredRef(RefFrame, U, Trans, useMass_);
      }
      Distances->AddElement( R );
      // DEBUG
      //mprinterr("%12i %12i %12.4lf\n",nref,nframe,R);
    } // END loop over target frames
    progress->Update(current-1);
  } // END loop over reference frames
  //progress->Update(max);
  delete progress;
}

// Rms2d::CalcRmsToTraj()
/** Calc RMSD of every frame in reference traj to every frame in 
  * ReferenceCoords.
  */
void Rms2d::CalcRmsToTraj() {
  Frame RefFrame;
  Frame SelectedTgt;
  Frame SelectedRef;
  DataSet *rmsdata;
  char setname[256];
  double U[9], Trans[6];
  float R;

  // Set up reference mask for reference parm
  if (RefParm->SetupIntegerMask(RefMask)) {
    mprinterr("Error: Could not set up reference mask [%s] for parm %s\n",
              RefMask.MaskString(), RefParm->c_str());
    return;
  }
  // Setup frame for selected reference atoms
  SelectedRef.SetupFrameFromMask(RefMask, RefParm->Mass()); 
  RefFrame.SetupFrame(RefParm->Natom(),RefParm->Mass());
  int totalref = RefTraj->Total_Read_Frames();

  int totaltgt = ReferenceCoords.Ncoords();
  int max = totalref * totaltgt;
  mprintf("  RMS2D: Calculating RMSDs between each input frame and each reference\n"); 
  mprintf("         trajectory %s frame (%i total).\n  ",
          RefTraj->TrajName(), max);
  if (RefTraj->BeginTraj(false)) {
    mprinterr("Error: Rms2d: Could not open reference trajectory.\n");
    return;
  }
  // Set up progress Bar
  ProgressBar *progress = new ProgressBar(max);
  int current=0;
  progress->Update(current);

  if (mass_ptr!=NULL) {
    // Set up selected target mass info
    SelectedTgt.SetupFrameFromMask(FrameMask,mass_ptr);
    useMass_=true;
  } else {
    // If no mass, ensure SelectedTgt can hold max #atoms in ReferenceCoords
    useMass_=false;
    int maxtgtnatom = ReferenceCoords.MaxNatom();
    SelectedTgt.SetupFrame( maxtgtnatom );
  }
  // LOOP OVER REFERENCE FRAMES
  for (int nref=0; nref < totalref; nref++) {
    // Get the current reference frame from trajectory
    RefTraj->GetNextFrame(RefFrame);
  
    // Set up dataset for this reference frame
    sprintf(setname,"Frame_%i",nref+1);
    rmsdata = RmsData.Add(DataSet::FLOAT, setname, "Rms2d");
    DFL->Add(rmsdFile,rmsdata);
    // Set reference atoms and pre-center if fitting
    SelectedRef.SetCoordinates(RefFrame, RefMask);
    if (!nofit)
      SelectedRef.CenterReference(Trans+3, useMass_);

    // LOOP OVER TARGET FRAMES
    for (int nframe=0; nframe < totaltgt; nframe++) {
      ++current;
      // Get selected atoms of the current target frame
      SelectedTgt = ReferenceCoords[nframe];
      // Ensure # ref atoms == # tgt atoms
      if (SelectedRef.Natom() != SelectedTgt.Natom()) {
        mprintf("\tWarning: Rms2d: Selected # atoms in ref %i (%i) != selected # atoms\n",
                nref+1, SelectedRef.Natom());
        mprintf("\t         in tgt %i (%i).Assigning this pair RMSD of -1.0\n",
                nframe+1, SelectedTgt.Natom());
        R = -1.0;
      } else if (nofit) {
        // Perform no fit RMS calculation
        R = (float) SelectedTgt.RMSD(&SelectedRef, useMass_);
      } else {
        // Perform fit RMS calculation
        R = (float) SelectedTgt.RMSD_CenteredRef(SelectedRef, U, Trans, useMass_);
      }
      RmsData.AddData(nframe, &R, nref);
      // DEBUG
      //mprinterr("%12i %12i %12.4lf\n",nref,nframe,R);
    } // END loop over target frames
    progress->Update(current-1);
  } // END loop over reference frames
  //progress->Update(max);
  delete progress;
  RefTraj->EndTraj();
}

// Rms2d::AutoCorrelate()
/** Calculate the autocorrelation of the RMSDs. For proper weighting
  * exp[ -RMSD(framei, framei+lag) ] is used. This takes advantage of
  * the fact that 0.0 RMSD essentially means perfect correlation (1.0).
  */
int Rms2d::AutoCorrelate(TriangleMatrix &Distances) {
  double ct;
  int lagmax = ReferenceCoords.Ncoords();
  int N = ReferenceCoords.Ncoords();

  // Set up output dataset and add it to the data file list.
  if (Ct.Setup((char*)"RmsCorr", lagmax)) return 1;
  DFL->Add(corrfilename, &Ct);

  // By definition for lag == 0 RMS is 0 for all frames,
  // translates to correlation of 1.
  ct = 1;
  Ct.Add(0, &ct); 
 
  for (int i = 1; i < lagmax; i++) {
    ct = 0;
    int jmax = N - i;
    for (int j = 0; j < jmax; j++) {
      // When i == j GetElement returns 0.0
      double rmsval = Distances.GetElement(j, j+i);
      ct += exp( -rmsval );
    }
    ct /= jmax;
    Ct.Add(i, &ct);
  }

  return 0;
}

// Rms2d::print()
/** Perform the rms calculation of each frame to each other frame.
  */
void Rms2d::print() {
  TriangleMatrix *Distances;
  char setname[256];

  if (RefTraj==NULL) {
    Distances = new TriangleMatrix(); 
    Calc2drms(Distances);
    // Convert TriangleMatrix to a dataset.
    // NOTE: This currently uses way more memory than it needs to.
    // TriangleMatrix should just be a dataset that can be output.
    for (int nref=0; nref < ReferenceCoords.Ncoords(); nref++) {
      // Set up dataset for this reference frame
      sprintf(setname,"Frame_%i",nref+1);
      DataSet *rmsdata = RmsData.Add(DataSet::FLOAT, setname, "Rms2d");
      DFL->Add(rmsdFile,rmsdata);
      for (int nframe=0; nframe < ReferenceCoords.Ncoords(); nframe++) {
        float R = Distances->GetElementF(nref, nframe);
        RmsData.AddData(nframe, &R, nref);
      }
    }
    // Calculate correlation
    if (corrfilename!=NULL) AutoCorrelate( *Distances );
    delete Distances;
  } else {
    // Sanity check
    if (rmsdFile==NULL) {
      mprinterr("Error: Rms2d: 'reftraj' no output file specified with 'rmsout'.\n");
      return;
    }
    CalcRmsToTraj();
  }
}


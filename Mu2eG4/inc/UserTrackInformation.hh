#ifndef Mu2eG4_UserTrackInformation_hh
#define Mu2eG4_UserTrackInformation_hh
//
// Mu2e specific information about one G4 track.
//
// $Id: UserTrackInformation.hh,v 1.6 2013/12/02 20:12:58 genser Exp $
// $Author: genser $
// $Date: 2013/12/02 20:12:58 $
//
// Original author Rob Kutschke
//

// Mu2e includes
#include "MCDataProducts/inc/ProcessCode.hh"

// Geant4 includes
#include "G4VUserTrackInformation.hh"

namespace mu2e{

  class UserTrackInformation : public G4VUserTrackInformation{

  public:
    UserTrackInformation();
    virtual ~UserTrackInformation();

    void setProcessCode ( ProcessCode code){
      _forcedStop = true;
      _code = code;
    }

    void setMuCapCode ( ProcessCode code){
      _muCapCode = code;
    }

    bool         isForced() const { return _forcedStop; }
    ProcessCode  code()    const { return _code; }
    ProcessCode  muCapCode() const { return _muCapCode; }

    virtual void Print() const;

  private:

    // Did Mu2e user stepping action force a stop?
    bool _forcedStop;

    // If it did, then this is the reason why.
    ProcessCode _code;

    // Label of muMinusCaptureAtRest daugter particles (if any)

    ProcessCode _muCapCode;

  };

} // end namespace mu2e

#endif /* Mu2eG4_UserTrackInformation_hh */

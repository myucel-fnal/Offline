//
// Define a sensitive detector for extinction monitor tof
//
// $Id: ExtMonUCITofSD.cc,v 1.1 2011/12/30 20:31:46 youzy Exp $
// $Author: youzy $
// $Date: 2011/12/30 20:31:46 $
//

#include <cstdio>
#include <fstream>

// Framework includes
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "cetlib/exception.h"

// G4 includes
#include "G4RunManager.hh"
#include "G4Step.hh"
#include "G4ThreeVector.hh"
#include "G4RotationMatrix.hh"
#include "G4ios.hh"

// Mu2e includes
#include "Mu2eG4/inc/ExtMonUCITofSD.hh"
#include "Mu2eG4/inc/PhysicsProcessInfo.hh"
#include "Mu2eUtilities/inc/SimpleConfig.hh"
#include "GeometryService/inc/GeomHandle.hh"
#include "GeometryService/inc/WorldG4.hh"

using namespace std;

namespace mu2e {

  ExtMonUCITofSD::ExtMonUCITofSD(G4String name, const SimpleConfig& config):
    G4VSensitiveDetector(name),
    _collection(0),
    _processInfo(0),
    _mu2eOrigin(GeomHandle<WorldG4>()->mu2eOriginInWorld()),
    _debugList(0),
    _sizeLimit(config.getInt("g4.stepsSizeLimit",0)),
    _currentSize(0),
    _simID(0),
    _event(0)
  {

    // Get list of events for which to make debug printout.
    string key("g4.extMonUCITofSDEventList");
    if ( config.hasName(key) ){
      vector<int> list;
      config.getVectorInt(key,list);
      _debugList.add(list);
    }

  }


  ExtMonUCITofSD::~ExtMonUCITofSD(){ }

  void ExtMonUCITofSD::Initialize(G4HCofThisEvent* HCE){

    _currentSize=0;

  }


  G4bool ExtMonUCITofSD::ProcessHits(G4Step* aStep,G4TouchableHistory*){

    _currentSize += 1;

    if( _sizeLimit>0 && _currentSize>_sizeLimit ) {
      if( (_currentSize - _sizeLimit)==1 ) {
        mf::LogWarning("G4") << "Maximum number of particles reached in ExtMonUCITofSD: "
                              << _currentSize << endl;
      }
      return false;
    }

    //G4Event const* event = G4RunManager::GetRunManager()->GetCurrentEvent();
    //const G4TouchableHandle & touchableHandle = aStep->GetPreStepPoint()->GetTouchableHandle();
    //G4int eventId = event->GetEventID();
    //G4int trackId = aStep->GetTrack()->GetTrackID();
    //G4int copyNo = touchableHandle->GetCopyNumber();

    // Get tof ID
    //G4int copyNo = aStep->GetPreStepPoint()->GetTouchableHandle()->GetCopyNumber(2);

    // Which process caused this step to end?
    G4String const& pname  = aStep->GetPostStepPoint()->GetProcessDefinedStep()->GetProcessName();
    ProcessCode endCode(_processInfo->findAndCount(pname));

    // Add the hit to the framework collection.
    // The point's coordinates are saved in the mu2e coordinate system.
    _collection->
      push_back(StepPointMC(art::Ptr<SimParticle>( *_simID, aStep->GetTrack()->GetTrackID(), _event->productGetter(*_simID) ),
                            aStep->GetPreStepPoint()->GetTouchableHandle()->GetVolume()->GetCopyNo(),
                            aStep->GetTotalEnergyDeposit(),
                            aStep->GetNonIonizingEnergyDeposit(),
                            aStep->GetPreStepPoint()->GetGlobalTime(),
                            aStep->GetPreStepPoint()->GetProperTime(),
                            aStep->GetPreStepPoint()->GetPosition() - _mu2eOrigin,
                            aStep->GetPreStepPoint()->GetMomentum(),
                            aStep->GetStepLength(),
                            endCode
                            ));

      int static const verbosityLevel = 0;
      if (verbosityLevel >0) {
       cout << __func__ << " Event " << 
         G4RunManager::GetRunManager()->GetCurrentEvent()->GetEventID() <<
         " ExtMonTof " << aStep->GetPreStepPoint()->GetTouchableHandle()->GetVolume()->GetName() << " " <<
         aStep->GetPreStepPoint()->GetTouchableHandle()->GetVolume()->GetCopyNo() <<
         " hit at: " << aStep->GetPreStepPoint()->GetPosition() - _mu2eOrigin << endl;
       }

    return true;

  }


  void ExtMonUCITofSD::EndOfEvent(G4HCofThisEvent*){

    if( _sizeLimit>0 && _currentSize>=_sizeLimit ) {
      mf::LogWarning("G4") << "Total of " << _currentSize
                            << " ExtMonUCI Tof hits were generated in the event."
                            << endl
                            << "Only " << _sizeLimit << " are saved in output collection."
                            << endl;
      cout << "Total of " << _currentSize
           << " ExtMonUCI Tof hits were generated in the event."
           << endl
           << "Only " << _sizeLimit << " are saved in output collection."
           << endl;
    }

    if (verboseLevel>0) {
      G4int NbHits = _collection->size();
      G4cout << "\n-------->Hits Collection: in this event they are " << NbHits
             << " hits in the ExtMonUCI Tof detectors: " << G4endl;
      for (G4int i=0;i<NbHits;i++) (*_collection)[i].print(G4cout);
    }

  }

  void ExtMonUCITofSD::beforeG4Event(StepPointMCCollection& outputHits,
                                    PhysicsProcessInfo& processInfo,
                                    art::ProductID const& simID,
                                    art::Event const & event ){
    _collection    = &outputHits;
    _processInfo   = &processInfo;
    _simID         = &simID;
    _event         = &event;

    return;
  } // end of beforeG4Event

} //namespace mu2e

//
// This module transforms StrawDigi objects into StrawHit objects
// It also builds the truth match map (if MC truth info for the StrawDigis exists)
//
// $Id: StrawHitsFromStrawDigis_module.cc,v 1.2 2013/12/12 19:08:29 brownd Exp $
// $Author: brownd $ 
// $Date: 2013/12/12 19:08:29 $
//
// Original author David Brown, LBNL
//
// framework
#include "art/Framework/Principal/Event.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Handle.h"
#include "GeometryService/inc/GeomHandle.hh"
#include "art/Framework/Core/EDProducer.h"
#include "GeometryService/inc/DetectorSystem.hh"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Services/Optional/TFileService.h"
// conditions
#include "ConditionsService/inc/ConditionsHandle.hh"
#include "ConditionsService/inc/TrackerCalibrations.hh"
#include "GeometryService/inc/getTrackerOrThrow.hh"
#include "TTrackerGeom/inc/TTracker.hh"
#include "ConfigTools/inc/ConfigFileLookupPolicy.hh"
#include "TrackerMC/inc/StrawElectronics.hh"
//CLHEP
#include "CLHEP/Random/RandGaussQ.h"
// data
#include "RecoDataProducts/inc/StrawDigiCollection.hh"
#include "RecoDataProducts/inc/StrawHitCollection.hh"
#include "MCDataProducts/inc/PtrStepPointMCVectorCollection.hh"

using namespace std;
namespace mu2e {

  class StrawHitsFromStrawDigis : public art::EDProducer {

  public:
    explicit StrawHitsFromStrawDigis(fhicl::ParameterSet const& pset);
    // Accept compiler written d'tor.

    virtual void beginJob();
    virtual void beginRun( art::Run& run );
    void produce( art::Event& e);

  private:

    // Diagnostics level.
    int _diagLevel;

    // Limit on number of events for which there will be full printout.
    int _maxFullPrint;

    // Name of the StrawDigi collection
    std::string _strawDigis;
    std::string _strawDigiMCPtrs;
    double _G4dEdQ; // G4 equivalence of energy (MeV) and ionization charge (pCoulomb).  Should
    // come from a materials database, it depends FIXME!!
    StrawElectronics _strawele; // models of straw response to stimuli
  };

  StrawHitsFromStrawDigis::StrawHitsFromStrawDigis(fhicl::ParameterSet const& pset) :
    _diagLevel(pset.get<int>("diagLevel",0)),
    _strawDigis(pset.get<string>("StrawDigis")),
    _strawDigiMCPtrs(pset.get<string>("StrawDigiMCPtrs","StrawDigiMCPtr")),
    _G4dEdQ(pset.get<double>("G4EnergyperCharge",169)), // 169 MeV/pCoulombs
    _strawele(pset.get<fhicl::ParameterSet>("StrawElectronics",fhicl::ParameterSet()))
    {
      produces<StrawHitCollection>();
      produces<PtrStepPointMCVectorCollection>("StrawHitMCPtr");
    }

  void StrawHitsFromStrawDigis::beginJob(){
  }

  void StrawHitsFromStrawDigis::beginRun( art::Run& run ){
  }

  void StrawHitsFromStrawDigis::produce(art::Event& event) {
    unique_ptr<StrawHitCollection>             strawHits(new StrawHitCollection);
    unique_ptr<PtrStepPointMCVectorCollection> mcptrHits(new PtrStepPointMCVectorCollection);

    // find the digis
    art::Handle<mu2e::StrawDigiCollection> strawdigisH; 
    const StrawDigiCollection* strawdigis(0);
    if(event.getByLabel(_strawDigis,strawdigisH))
      strawdigis = strawdigisH.product();
    if(strawdigis == 0)
      throw cet::exception("RECO")<<"mu2e::StrawHitsFromStrawDigis: No StrawDigi collection found for label " <<  _strawDigis << endl;

  // find the associated MC truth collection.  Note this doesn't have to exist!
    const PtrStepPointMCVectorCollection * mcptrdigis(0);
    art::Handle<PtrStepPointMCVectorCollection> mcptrdigiH;
    if(event.getByLabel(_strawDigiMCPtrs,"StrawDigiMCPtr",mcptrdigiH))
      mcptrdigis = mcptrdigiH.product();
  // loop over digis.  Note the MC truth is in sequence
    size_t ndigi = strawdigis->size();
    if(mcptrdigis != 0 && mcptrdigis->size() != ndigi)
      throw cet::exception("RECO")<<"mu2e::StrawHitsFromStrawDigis: MCPtrDigi collection size doesn't match StrawDigi collection size" << endl;
    for(size_t isd=0;isd<ndigi;++isd){
      StrawDigi const& digi = (*strawdigis)[isd];
// convert the digi to a hit
      array<double,2> times;
      _strawele.tdcTimes(digi.TDC(),times);
// hit wants primary time and dt.  Note we may have conflicting definitions of the sign of dt, FIXME!!
      double time = times[0];
      double dt = times[1]-times[0];
// to get the charge we should fit the whole waveform: for now, just take the maximum value minus the baseline
// FIXME!!
      StrawDigi::ADCWaveform const& adc = digi.adcWaveform();
      auto minadc = adc[0];
      auto maxadc = adc[0];
      for(auto iadc=adc.begin();iadc!=adc.end();++iadc){
	if(*iadc > maxadc)maxadc = *iadc;
      }
      double charge = _strawele.adcCharge(maxadc-minadc);
// convert charge to energy using G4 conversion factor
      double energy = charge*_G4dEdQ;
  // crate the straw hit and append it to the list
      StrawHit newhit(digi.strawIndex(),time,dt,energy);
      strawHits->push_back(newhit);
// copy MC truth from digi to hit.  These are exactly the same as for the digi
      if(mcptrdigis != 0){
	mcptrHits->push_back((*mcptrdigis)[isd]);
      }
    }
// put objects into event
    event.put(std::move(strawHits));
    if(mcptrdigis != 0)event.put(std::move(mcptrHits),"StrawHitMCPtr");

  }
}


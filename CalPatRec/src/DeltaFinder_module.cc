/////////////////////////////////////////////////////////////////////////////
// framework
//
// parameter defaults: CalPatRec/fcl/prolog.fcl
//////////////////////////////////////////////////////////////////////////////
#include "fhiclcpp/types/Atom.h"
#include "fhiclcpp/types/Sequence.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Core/EDProducer.h"
#include "art_root_io/TFileService.h"

#include "art/Utilities/make_tool.h"
#include "Offline/Mu2eUtilities/inc/ModuleHistToolBase.hh"

#include "Offline/GeometryService/inc/GeomHandle.hh"
#include "Offline/GeometryService/inc/DetectorSystem.hh"
#include "Offline/TrackerGeom/inc/Tracker.hh"
#include "Offline/CalorimeterGeom/inc/DiskCalorimeter.hh"

// conditions
#include "Offline/ConditionsService/inc/ConditionsHandle.hh"

// data
#include "Offline/RecoDataProducts/inc/ComboHit.hh"
#include "Offline/RecoDataProducts/inc/StrawHit.hh"
#include "Offline/RecoDataProducts/inc/StrawHitPosition.hh"
#include "Offline/RecoDataProducts/inc/StereoHit.hh"
#include "Offline/RecoDataProducts/inc/StrawHitFlag.hh"
#include "Offline/RecoDataProducts/inc/CaloCluster.hh"
#include "Offline/RecoDataProducts/inc/TimeCluster.hh"
#include "Offline/RecoDataProducts/inc/IntensityInfoTimeCluster.hh"

// diagnostics

#include "Offline/CalPatRec/inc/DeltaFinder_types.hh"
#include "Offline/CalPatRec/inc/DeltaFinderAlg.hh"

#include <algorithm>
#include <cmath>

using namespace std;

namespace mu2e {

  using namespace DeltaFinderTypes;

  class DeltaFinder: public art::EDProducer {
  public:

    struct Config {
      using Name    = fhicl::Name;
      using Comment = fhicl::Comment;
      fhicl::Atom<art::InputTag>   sschCollTag            {Name("sschCollTag"       )    , Comment("SS ComboHit collection name") };
      fhicl::Atom<art::InputTag>   chCollTag              {Name("chCollTag"         )    , Comment("ComboHit collection Name"   ) };
      fhicl::Atom<art::InputTag>   sdmcCollTag            {Name("sdmcCollTag"       )    , Comment("StrawDigiMC collection Name") };
      fhicl::Atom<int>             debugLevel             {Name("debugLevel"        )    , Comment("debug level"                ) };
      fhicl::Atom<int>             diagLevel              {Name("diagLevel"         )    , Comment("diag level"                 ) };
      fhicl::Atom<int>             printErrors            {Name("printErrors"       )    , Comment("print errors"               ) };
      fhicl::Atom<int>             writeFilteredComboHits {Name("writeFilteredComboHits"), Comment("1: write filtered CH coll"  ) };
      fhicl::Atom<int>             writeStrawHitFlags     {Name("writeStrawHitFlags")    , Comment("1: write SH flag coll"      ) };
      fhicl::Atom<int>             testOrder              {Name("testOrder"         )    , Comment("1: test order"              ) };
      fhicl::Atom<bool>            testHitMask            {Name("testHitMask"       )    , Comment("true: test hit mask"        ) };
      fhicl::Sequence<std::string> goodHitMask            {Name("goodHitMask"       )    , Comment("good hit mask"              ) };
      fhicl::Sequence<std::string> bkgHitMask             {Name("bkgHitMask"        )    , Comment("background hit mask"        ) };

      fhicl::Table<DeltaFinderTypes::Config> diagPlugin      {Name("diagPlugin"      ), Comment("Diag plugin"           ) };
      fhicl::Table<DeltaFinderAlg::Config>   finderParameters{Name("finderParameters"), Comment("finder alg parameters" ) };
    };

  protected:
//-----------------------------------------------------------------------------
// talk-to parameters: input collections and algorithm parameters
//-----------------------------------------------------------------------------
    art::InputTag   _sschCollTag;
    art::InputTag   _chCollTag;
    art::InputTag   _sdmcCollTag;

    int             _writeFilteredComboHits;   // write filtered combo hits
    int             _writeStrawHitFlags;

    int             _debugLevel;
    int             _diagLevel;
    int             _printErrors;
    int             _testOrder;

    StrawHitFlag    _bkgHitMask;

    std::unique_ptr<ModuleHistToolBase> _hmanager;
//-----------------------------------------------------------------------------
// cache event/geometry objects
//-----------------------------------------------------------------------------
    const ComboHitCollection*    _sschColl ;

    const Tracker*               _tracker;
    const DiskCalorimeter*       _calorimeter;

    DeltaFinderTypes::Data_t     _data;              // all data used
    int                          _testOrderPrinted;

    DeltaFinderAlg*              _finder;
//-----------------------------------------------------------------------------
// functions
//-----------------------------------------------------------------------------
  public:
    explicit DeltaFinder(const art::EDProducer::Table<Config>& config);

  private:

    bool         findData            (const art::Event&  Evt);
    void         initTimeCluster     (DeltaCandidate* Delta, TimeCluster* Tc);
//-----------------------------------------------------------------------------
// overloaded methods of the module class
//-----------------------------------------------------------------------------
    void         beginJob() override;
    void         beginRun(art::Run& ARun) override;
    void         endJob  () override;
    void         produce (art::Event& E ) override;
  };

//-----------------------------------------------------------------------------
  DeltaFinder::DeltaFinder(const art::EDProducer::Table<Config>& config):
    art::EDProducer{config},
    _sschCollTag           (config().sschCollTag()       ),
    _chCollTag             (config().chCollTag()         ),
    _sdmcCollTag           (config().sdmcCollTag()       ),
    _writeFilteredComboHits(config().writeFilteredComboHits()    ),
    _writeStrawHitFlags    (config().writeStrawHitFlags()),
    _debugLevel            (config().debugLevel()        ),
    _diagLevel             (config().diagLevel()         ),
    _printErrors           (config().printErrors()       ),
    _testOrder             (config().testOrder()         ),
    _bkgHitMask            (config().bkgHitMask()        )
  {

    consumesMany<ComboHitCollection>(); // ??? Necessary because fillStrawHitIndices calls getManyByType.

    produces<IntensityInfoTimeCluster>();
    produces<StrawHitFlagCollection>("ComboHits");
    if (_writeStrawHitFlags     == 1) produces<StrawHitFlagCollection>("StrawHits");
    if (_writeFilteredComboHits == 1) produces<ComboHitCollection>    ("");

                                        // this is a list of delta-electron candidates
    produces<TimeClusterCollection>();

    _finder = new DeltaFinderAlg(config().finderParameters,&_data);

    _testOrderPrinted = 0;

    if (_diagLevel != 0) _hmanager = art::make_tool  <ModuleHistToolBase>(config().diagPlugin,"diagPlugin");
    else                 _hmanager = std::make_unique<ModuleHistToolBase>();

    _data.chCollTag      = _chCollTag;
    _data.sdmcCollTag    = _sdmcCollTag;
    _data._finder        = _finder;         // for diagnostics
  }

  //-----------------------------------------------------------------------------
  void DeltaFinder::beginJob() {
    if (_diagLevel > 0) {
      art::ServiceHandle<art::TFileService> tfs;
      _hmanager->bookHistograms(tfs);
    }
  }

  //-----------------------------------------------------------------------------
  void DeltaFinder::endJob() {
  }

//-----------------------------------------------------------------------------
// create a Z-ordered representation of the tracker
//-----------------------------------------------------------------------------
  void DeltaFinder::beginRun(art::Run& aRun) {

    _data.InitGeometry();
//-----------------------------------------------------------------------------
// it is enough to print that once
//-----------------------------------------------------------------------------
    if (_testOrder && (_testOrderPrinted == 0)) {
      _data.testOrderID  ();
      _data.testdeOrderID();
      _testOrderPrinted = 1;
    }

    if (_diagLevel != 0) _hmanager->debug(&_data,1);
  }

//-----------------------------------------------------------------------------
  bool DeltaFinder::findData(const art::Event& Evt) {

    auto chcH   = Evt.getValidHandle<mu2e::ComboHitCollection>(_chCollTag);
    _data.chcol = chcH.product();

    auto sschcH = Evt.getValidHandle<mu2e::ComboHitCollection>(_sschCollTag);
    _sschColl   = sschcH.product();

    return (_data.chcol != nullptr) and (_sschColl != nullptr);
  }

//-----------------------------------------------------------------------------
// define the time cluster parameters for found DeltaCandidates
//-----------------------------------------------------------------------------
  void DeltaFinder::initTimeCluster(DeltaCandidate* Dc, TimeCluster* Tc) {
  }

//-----------------------------------------------------------------------------
  void DeltaFinder::produce(art::Event& Event) {
    if (_debugLevel) printf("* >>> DeltaFinder::produce  event number: %10i\n",Event.event());
//-----------------------------------------------------------------------------
// clear memory in the beginning of event processing and cache event pointer
//-----------------------------------------------------------------------------
    _data.InitEvent(&Event,_debugLevel);
//-----------------------------------------------------------------------------
// process event
//-----------------------------------------------------------------------------
    if (! findData(Event)) {
      const char* message = "mu2e::DeltaFinder_module::produce: data missing or incomplete";
      throw cet::exception("RECO")<< message << endl;
    }

    _data._nComboHits = _data.chcol->size();
    _data._nStrawHits = _sschColl->size();
//-----------------------------------------------------------------------------
// run delta finder, it also finds proton time clusters
//-----------------------------------------------------------------------------
    _finder->run();
//-----------------------------------------------------------------------------
// form output - flag combo hits -
// if flagged combo hits are written out, likely don't need writing out the flags
// need to drop previously set 'energy' flag
//-----------------------------------------------------------------------------
    unique_ptr<StrawHitFlagCollection> up_chfcol(new StrawHitFlagCollection(_data._nComboHits));
    _data.outputChfColl = up_chfcol.get();

    for (int i=0; i<_data._nComboHits; i++) {
//-----------------------------------------------------------------------------
// initialize output flags to the flags of the input combo hits, in parallel
// count the number of hits in the potentially to be written out straw hit collection
//-----------------------------------------------------------------------------
      const ComboHit* ch = &(*_data.chcol)[i];
      StrawHitFlag* flag = &(*_data.outputChfColl)[i];
      flag->merge(ch->flag());
//-----------------------------------------------------------------------------
// always flag delta hits here, don't need previously set delta bits
// if flag protons, flag all hits as good
//-----------------------------------------------------------------------------
      flag->clear(StrawHitFlag::bkg);
      if (_finder->flagProtonHits()) flag->merge(StrawHitFlag::energysel);
    }

    const ComboHit* ch0(0);
    if (_data._nComboHits > 0) ch0 = &_data.chcol->at(0);

    unique_ptr<TimeClusterCollection>  tcColl(new TimeClusterCollection);
//-----------------------------------------------------------------------------
// set delta flags
//-----------------------------------------------------------------------------
    int ndeltas = _data.nDeltaCandidates();
    for (int i=0; i<ndeltas; i++) {
      DeltaCandidate* dc = _data.deltaCandidate(i);
//-----------------------------------------------------------------------------
// skip merged in delta candidates
// also require a delta candidate to have at least 5 hits (in the mask, set by
// the delta finder)
// do not consider proton stub candidates (those with <EDep> > 0.004)
//-----------------------------------------------------------------------------
      if (dc->Active() == 0)                                          continue;
      if (dc->Mask()   != 0)                                          continue;
      for (int is=dc->fFirstStation; is<=dc->fLastStation; is++) {
        DeltaSeed* ds = dc->Seed(is);
        if (ds != nullptr) {
//-----------------------------------------------------------------------------
// loop over the hits and flag each of them as delta
//-----------------------------------------------------------------------------
          for (int face=0; face<kNFaces; face++) {
            const HitData_t* hd = ds->HitData(face);
            if (hd == nullptr)                                        continue;
            int loc = hd->fHit-ch0;
            StrawHitFlag* flag = &(*_data.outputChfColl)[loc];
            flag->merge(StrawHitFlag::bkg);           // set delta-electron bit
          }
        }
      }
    }
//-----------------------------------------------------------------------------
// set proton flags, 'energy' now means 'proton'
//-----------------------------------------------------------------------------
    int np15(0);
    int nprotons = _data.nProtonCandidates();
    for (int i=0; i<nprotons; i++) {
      ProtonCandidate* pc = _data.protonCandidate(i);
      if (pc->nHitsTot() >= 15) np15++;
      if (pc->nStationsWithHits() == 1) {
//-----------------------------------------------------------------------------
// for proton candidates with just one station require eDep > 4 KeV
// this adds a little inefficiency for proton reco,
// but reduces overefficiency of flagging the CE hits
//-----------------------------------------------------------------------------
        if (pc->eDep() < 0.004) continue;
      }
      for (int is=pc->fFirstStation; is<=pc->fLastStation; is++) {
        for (int face=0; face<kNFaces; face++) {
          int nh = pc->nHits(is,face);
          for (int ih=0; ih<nh; ih++) {
            const HitData_t* hd = pc->hitData(is,face,ih);
            int loc = hd->fHit-ch0;
            StrawHitFlag* flag = &(*_data.outputChfColl)[loc];
            flag->clear(StrawHitFlag::energysel);
          }
        }
      }
//-----------------------------------------------------------------------------
// (later) make a time cluster out of each ProtonCandidate
//-----------------------------------------------------------------------------
      // TimeCluster new_tc;
      // initTimeCluster(dc,&new_tc);
      // tcColl->push_back(new_tc);
    }

    Event.put(std::move(tcColl));
//-----------------------------------------------------------------------------
// 'ppii' - proton counting-based proxy to the stopped muon rate
//-----------------------------------------------------------------------------
    std::unique_ptr<IntensityInfoTimeCluster> ppii(new IntensityInfoTimeCluster(np15));
    Event.put(std::move(ppii));
//-----------------------------------------------------------------------------
// in the end of event processing fill diagnostic histograms
//-----------------------------------------------------------------------------
    if (_diagLevel  > 0) _hmanager->fillHistograms(&_data);
    if (_debugLevel > 0) _hmanager->debug(&_data,2);

    if (_writeFilteredComboHits) {
//-----------------------------------------------------------------------------
// write out filtered out collection of ComboHits with right flags, use deep copy
// the output ComboHit collection doesn't need a parallel flag collection
//-----------------------------------------------------------------------------
      auto outputChColl = std::make_unique<ComboHitCollection>();
      outputChColl->reserve(_data._nComboHits);

      outputChColl->setParent(_data.chcol->parent());
      for (int i=0; i<_data._nComboHits; i++) {
        StrawHitFlag const* flag = &(*_data.outputChfColl)[i];
        if (flag->hasAnyProperty(_bkgHitMask))                        continue;
//-----------------------------------------------------------------------------
// for the moment, assume bkgHitMask to be empty, so write out all hits
// normally, don't need delta and proton hits
//-----------------------------------------------------------------------------
        const ComboHit* ch = &(*_data.chcol)[i];
        outputChColl->push_back(*ch);
        outputChColl->back()._flag.merge(*flag);
      }
      Event.put(std::move(outputChColl));
    }
//-----------------------------------------------------------------------------
// create the collection of StrawHitFlag for the StrawHitCollection
// why would that be needed? - straw hits are also combo hits and have flags
// stored in their data
//-----------------------------------------------------------------------------
    if (_writeStrawHitFlags == 1) {
                                        // first, copy over the original flags

      std::unique_ptr<StrawHitFlagCollection> shfcol(new StrawHitFlagCollection(_data._nStrawHits));

      for (int i=0; i<_data._nStrawHits; i++) {
        StrawHitFlag&   flag =  (*shfcol)[i]; // should be empty at this point
        flag.merge((*_sschColl)[i].flag());     // merge in the original flag
        flag.clear(StrawHitFlag::bkg       ); // clear delta selection
        flag.merge(StrawHitFlag::energysel ); // and assume all hits are not from protons
      }
//-----------------------------------------------------------------------------
// after that, loop over [flagged] combo hits and update 'delta'(bkg) and 'proton'(energysel)
// flags for flagged hits
//-----------------------------------------------------------------------------
      for (int ich=0; ich<_data._nComboHits; ich++) {
        const ComboHit* ch   = &(*_data.chcol )[ich];
        StrawHitFlag    flag =  (*_data.outputChfColl)[ich];
        flag.merge(ch->flag());
        for (auto ish : ch->indexArray()) {
          if (not flag.hasAnyProperty(StrawHitFlag::energysel)) (*shfcol)[ish].clear(StrawHitFlag::energysel);
          if (    flag.hasAnyProperty(StrawHitFlag::bkg      )) (*shfcol)[ish].merge(StrawHitFlag::energysel);
        }
      }

      Event.put(std::move(shfcol),"StrawHits");
    }
//-----------------------------------------------------------------------------
// moving in the end, after diagnostics plugin routines have been called - move
// invalidates the original pointer...
//-----------------------------------------------------------------------------
    Event.put(std::move(up_chfcol),"ComboHits");
  }
}
//-----------------------------------------------------------------------------
// macro that makes this class a module.
//-----------------------------------------------------------------------------
DEFINE_ART_MODULE(mu2e::DeltaFinder)
//-----------------------------------------------------------------------------
// done
//-----------------------------------------------------------------------------

//
//
// An EDProducer Module that creates a compressed representation of Calorimeter StepPointMCs
//
// Original author Bertrand Echenard
//
// Note: Replace std::map<art:Ptr<SimParticle, XXXXX> > by std::unordered_map<art::Ptr<SimParticle>, xxx> when the art::Ptr hashing 
//       function is implemented in a future release (recommended to not implement your own, it will clash with art)
//
//
// This modules compresses calorimeter StepPointMCs into CaloShower objects, and flags "interesting" SimParticles. 
// We call a SimParticle entering the calorimeter an Ancestore SimParticle. This ancestor will generate a shower of SimParticles 
// and StepPointMcs in the crystal. 
// Note: if a SimParticvle enters the calorimeter, generates a secondary SimParticle that hit another section of the calorimeter 
// (e.g. a leakage photon in the3 first disk hits the second disk), then the SimParticle hitting the second section is trated 
// as an ancestor SimParticle
//
// Basic idea: We recollect all StepPointMCS attached to a SimParticle ancestor. If the SimParticle is compressible, all StepPointsMC 
// are collapsed into CaloShowerStep objects. If not, we create a CaloShowerStep object for each SimParticle created by the ancestor
// SimParticle (basically, compress the StepPointMC for each SimParticle). We do the same thing for readout hits but without
// compressing them. At the end of the modules, all StepPointMCs can be dropped, as well as a large fraction of SimParticles 
// with almost no loss of information.

// The compressibility is determined by looking at the interaction codes of the StepPointMCs. 
// Particles are compressed in small intervals of time and crystal longitudinal slices.
//
// See doc-db XXXX for more details
//


// C++ includes.
#include <iostream>
#include <string>
#include <cmath>
#include <map>
#include <vector>
#include <unordered_set>
#include <utility>

// Framework includes.
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Selector.h"
#include "art/Framework/Principal/SubRun.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Services/Optional/TFileDirectory.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"


// Mu2e includes.
#include "CalorimeterGeom/inc/Calorimeter.hh"
#include "GeometryService/inc/GeometryService.hh"
#include "GeometryService/inc/GeomHandle.hh"
#include "CaloMC/inc/ShowerStepUtil.hh"
#include "MCDataProducts/inc/PtrStepPointMCVectorCollection.hh"
#include "MCDataProducts/inc/StepPointMCCollection.hh"
#include "MCDataProducts/inc/SimParticleCollection.hh"
#include "MCDataProducts/inc/SimParticlePtrCollection.hh"
#include "MCDataProducts/inc/CaloShowerStepCollection.hh"
#include "MCDataProducts/inc/PhysicalVolumeInfoMultiCollection.hh"
#include "Mu2eUtilities/inc/PhysicalVolumeMultiHelper.hh"


#include "CLHEP/Vector/ThreeVector.h"
#include "TH2F.h"





namespace mu2e {



    // Anonymous namespace to hold some helper classes.
    namespace {

         class CaloCompressUtil {

             public:

                 CaloCompressUtil() : _steps(), _sims(), _procs() {}

                 const std::vector<const StepPointMC* >&     steps()        const {return _steps;}
                 const std::vector<art::Ptr<SimParticle> >&  sims()         const {return _sims;}
                 const std::unordered_set<int>&              processCodes() const {return _procs;}

                 void fill(StepPointMC const* step, std::vector<art::Ptr<SimParticle> > sims)
                 {
                     _steps.push_back(step);
                     _procs.insert(step->endProcessCode());               
                     for (const auto& sim: sims) _sims.push_back(sim); 
                 }
                 
               
              private:
              
                 std::vector<const StepPointMC* >     _steps;
                 std::vector<art::Ptr<SimParticle> >  _sims;
                 std::unordered_set<int>              _procs;                              
         }; 
    } 














  class CaloShowerStepFromStepPt : public art::EDProducer {

       public:

          explicit CaloShowerStepFromStepPt(fhicl::ParameterSet const& pset) :
             numZSlices_(              pset.get<int>        ("numZSlices") ),
             deltaTime_(               pset.get<double>     ("deltaTime") ),
             calorimeterStepPoints_(   pset.get<std::string>("calorimeterStepPoints") ),
             calorimeterROStepPoints_( pset.get<std::string>("calorimeterROStepPoints") ),
             physVolInfoInput_(        pset.get<std::string>("physVolInfoInput") ),
             caloMaterial_(            pset.get<std::vector<std::string> >("caloMaterial") ),
             diagLevel_(               pset.get<int>        ("diagLevel",0) ),
             messageCategory_("CaloCompressHits"),
             vols_(),
             procCodes_(),
             zSliceSize_(0)
          {
              produces<CaloShowerStepCollection>("calorimeter");
              produces<CaloShowerStepCollection>("calorimeterRO");
              produces<SimParticlePtrCollection>();
          }

          virtual      ~CaloShowerStepFromStepPt() {}
          virtual void beginJob();
                  void produce( art::Event& e);
                  void beginSubRun(art::SubRun& sr) override;



       private:

          typedef std::vector< art::Handle<StepPointMCCollection> > HandleVector;
          typedef art::Ptr<SimParticle> SimPtr;

          int                                            numZSlices_;
          double                                         deltaTime_;
          std::string                                    calorimeterStepPoints_;
          std::string                                    calorimeterROStepPoints_;
          art::InputTag                                  physVolInfoInput_;
          std::vector<std::string>                       caloMaterial_;
          int                                            diagLevel_;  
          const std::string                              messageCategory_;
          const PhysicalVolumeInfoMultiCollection*       vols_;
          std::map<int,std::set<int> >                   procCodes_;
          double                                         zSliceSize_;
          std::unordered_set<const PhysicalVolumeInfo*>  mapPhysVol_;


          TH2F*  hStartPos_;
          TH2F*  hStopPos_;
          TH1F*  hStopPos2_;
          TH1F*  hStartPos2_;
          TH1F*  hZpos_;
          TH1F*  hEtot_;
          TH2F*  hZpos2_;
          TH1F*  hGenId_;
          double totalEdep_;
          int    totalStep_;



         void makeCompressedHits(const HandleVector&, const HandleVector&, CaloShowerStepCollection&,  
                                 CaloShowerStepCollection&, SimParticlePtrCollection&);
         void collectStepBySimAncestor(const Calorimeter&, const PhysicalVolumeMultiHelper& , 
                                       const HandleVector&, std::map<SimPtr,CaloCompressUtil>&);
         void collectStepBySim(const HandleVector&, std::map<SimPtr,std::vector<const StepPointMC*> >&);
         bool isInsideCalorimeter(const PhysicalVolumeMultiHelper&, const art::Ptr<SimParticle>&);
         bool isCompressible(int simPdgId, const  std::unordered_set<int>&, const  SimParticlePtrCollection&);
         void compressSteps(const Calorimeter&, CaloShowerStepCollection&, bool isCrystal,
                            int volId, const SimPtr&, std::vector<const StepPointMC*>&);

    };






    //--------------------------------------------------------------------
    void CaloShowerStepFromStepPt::beginJob()
    {            
        // procCodes are the process codes for the StepPointMC.
        // see MCDataProducts/inc/ProcessCode.hh for code numbering scheme
        //
        // --- These are hardcoded to make sure changes are intended and carefully considered ---
        //      
        procCodes_[11].insert(   {2,16,17,21,23,29,40,49,58} );    // electron
        procCodes_[-11].insert(  {2,16,17,21,23,29,40,49,58} );    // positron
        procCodes_[22].insert(   {2,12,16,17,21,23,29,40,49,58} ); // photon
        procCodes_[2112].insert( {2,16,17,21,23,29,40,49,58,74} ); // neutron
        procCodes_[2212].insert( {16,17,21,23,29,40,45,49,58} );   // proton      

        if (diagLevel_ > 2)
	{
	   art::ServiceHandle<art::TFileService> tfs;
           hStartPos_  = tfs->make<TH2F>("hStartPos", "Sim start position",  1000,  5000, 15000, 200, 0, 1000);
           hStopPos_   = tfs->make<TH2F>("hStopPos",  "Sim stop position",   1000,  5000, 15000, 200, 0, 1000);
           hStartPos2_ = tfs->make<TH1F>("hStartPos2","Sim stop position",   1000, 10000, 13000);
           hStopPos2_  = tfs->make<TH1F>("hStopPos2", "Sim stop position",   1000, 10000, 13000);
           hZpos_      = tfs->make<TH1F>("hZpos",     "Step z pos",            20,     0,    20);
           hZpos2_     = tfs->make<TH2F>("hZpos2",    "Step z pos",            20,     0,    20, 100, 0, 5);
           hEtot_      = tfs->make<TH1F>("hEtot",     "Sim stop position",    150,     0,   150);
           hGenId_     = tfs->make<TH1F>("hSimId",    "Gen Id",               150,    -10,  140);
	}   
    }

    void CaloShowerStepFromStepPt::beginSubRun(art::SubRun& sr) 
    {
        art::Handle<PhysicalVolumeInfoMultiCollection> volh;
        sr.getByLabel(physVolInfoInput_, volh);
        vols_ = &*volh;
        
        mapPhysVol_.clear();
        for (const auto& vol : *volh)
        {        
           for (const auto& mv : vol.second) 
           {
               std::string material = mv.second.materialName();
               for (const std::string& caloMaterial : caloMaterial_) 
                   if (material == caloMaterial) mapPhysVol_.insert(&mv.second);
           }                  
        }
        
    }




    //--------------------------------------------------------------------
    void  CaloShowerStepFromStepPt::produce(art::Event& event)
    {
        
        totalEdep_=0.0;
        totalStep_=0;
        
        if ( diagLevel_ > 0) std::cout << "CaloShowerStepFromStepPt: produce() begin" << std::endl;

        // Check that calorimeter geometry description exists
        art::ServiceHandle<GeometryService> geom;    
        if( !(geom->hasElement<Calorimeter>()) ) return;

        // A container to hold the output hits.
        std::unique_ptr<CaloShowerStepCollection> caloShowerStepMCs(new CaloShowerStepCollection);
        std::unique_ptr<CaloShowerStepCollection> caloROShowerStepMCs(new CaloShowerStepCollection);
        std::unique_ptr<SimParticlePtrCollection> simsToKeep(new SimParticlePtrCollection());

        // These selectors will select data products with the given instance name, and ignore all other fields of the product ID.
        art::ProductInstanceNameSelector getCrystalSteps(calorimeterStepPoints_);
        art::ProductInstanceNameSelector getReadoutSteps(calorimeterROStepPoints_);

        // Get the StepPointMCs from the event.
        HandleVector crystalStepsHandles, readoutStepsHandles;
        event.getMany( getCrystalSteps, crystalStepsHandles);
        event.getMany( getReadoutSteps, readoutStepsHandles);


        makeCompressedHits(crystalStepsHandles,readoutStepsHandles,*caloShowerStepMCs,*caloROShowerStepMCs,*simsToKeep);

        // Add the output hit collection to the event
        event.put(std::move(caloShowerStepMCs),"calorimeter");
        event.put(std::move(caloROShowerStepMCs),"calorimeterRO");
        event.put(std::move(simsToKeep));

        if ( diagLevel_ > 0) std::cout << "Total energy deposited / number of stepPointMC: " <<totalEdep_<<" / "<<totalStep_<<std::endl;
        if ( diagLevel_ > 0) std::cout << "CaloShowerStepFromStepPt: produce() end" << std::endl;

    } 



    void CaloShowerStepFromStepPt::makeCompressedHits(const HandleVector& crystalStepsHandles, 
                                                      const HandleVector& readoutStepsHandles, 
                                                      CaloShowerStepCollection& caloShowerStepMCs, 
                                                      CaloShowerStepCollection& caloROShowerStepMCs,
                                                      SimParticlePtrCollection& simsToKeep)
    {

         PhysicalVolumeMultiHelper vi(*vols_);

         const Calorimeter& cal = *(GeomHandle<Calorimeter>());
         zSliceSize_             = (2.0*cal.caloGeomInfo().crystalHalfLength()+0.01)/float(numZSlices_);


         // Collect the StepPointMC's produced by each SimParticle Ancestor
         //-----------------------------------------------------------------

         std::map<SimPtr,CaloCompressUtil> crystalAncestorsMap; 
         collectStepBySimAncestor(cal,vi,crystalStepsHandles,crystalAncestorsMap);



         //Loop over ancestor simParticles, check if they are compressible, and produce the corresponding caloShowerStepMC
         //---------------------------------------------------------------------------------------------------------------
         int nCompress(0),nCompressAll(0);

         for (const auto& iter : crystalAncestorsMap )
         {
              const SimPtr&           sim  = iter.first;
              const CaloCompressUtil& info = iter.second;

              bool doCompress = isCompressible(sim->pdgId(),info.processCodes(),info.sims());

              std::map<int,std::vector<const StepPointMC*> > crystalMap;            
              for (const StepPointMC* step : info.steps()) crystalMap[step->volumeId()].push_back(step);

              for (const auto& iterCrystal : crystalMap)
              {
                   int crid = iterCrystal.first;
                   std::vector<const StepPointMC*> steps = iterCrystal.second;

                   if ( doCompress )
                   {
                        simsToKeep.push_back(sim);
                        compressSteps(cal, caloShowerStepMCs, true, crid, sim, steps);

                        if (diagLevel_ > 2)
                        {
                            CLHEP::Hep3Vector startSection = cal.toSectionFrame(0,sim->startPosition());
                            CLHEP::Hep3Vector endSection   = cal.toSectionFrame(0,sim->endPosition());
                            double rStart = sqrt(startSection.x()*startSection.x()+startSection.y()*startSection.y());
                            double rEnd   = sqrt(endSection.x()*endSection.x()+endSection.y()*endSection.y());
                            
			    hStartPos_->Fill(sim->startPosition().z(),rStart);
                            hStopPos_->Fill( sim->endPosition().z(),  rEnd);
                            for (const auto& simD: info.sims())
                            { 
                                 hStartPos2_->Fill(simD->startPosition().z());
                                 hStopPos2_->Fill(simD->endPosition().z());
                            }  
                        }              
                   }
                   
                   else 
                   {
                        std::map<SimPtr, std::vector<const StepPointMC*> > newSimStepMap;
                        for (const StepPointMC* step : steps) newSimStepMap[step->simParticle()].push_back(step);
                        for (auto& iter : newSimStepMap)
                        { 
                           compressSteps(cal, caloShowerStepMCs, true, crid, iter.first, iter.second);
                           simsToKeep.push_back(iter.first);
                        }   
                   }
              }   
              ++nCompressAll;
              if (doCompress) ++nCompress;
         }

         // Do the same for the readouts, but there is no need to compress
         //---------------------------------------------------------------

         std::map<SimPtr,std::vector<const StepPointMC*> > simStepROMap;
         collectStepBySim(readoutStepsHandles, simStepROMap);

         for (const auto& iter : simStepROMap )
         {
              const SimPtr& sim = iter.first;
              const std::vector<const StepPointMC*>& steps = iter.second;

              std::map<int,std::vector<const StepPointMC*> > crystalMap;            
              for (const StepPointMC* step : steps) crystalMap[step->volumeId()].push_back(step);

              for (const auto& iterCrystal : crystalMap)
              {
                   int roid = iterCrystal.first;
                   std::vector<const StepPointMC*> steps = iterCrystal.second;                                 
                   compressSteps(cal, caloROShowerStepMCs, false, roid, sim, steps);
              }   
         }
         
         
         if (diagLevel_ > 2) hEtot_->Fill(totalEdep_);
         
         //Final statistics
         if (diagLevel_ > 1) std::cout<<"CaloShowerStepFromStepPt compressed "<<nCompress<<" / "<<nCompressAll<<" incoming SimParticles"<<std::endl;
         if (diagLevel_ > 1) std::cout<<"CaloShowerStepFromStepPt keeping "<<simsToKeep.size()<<" CaloShowerSteps"<<std::endl;

    } 











    //------------------------------------------------------------------------------------------------------------------
    void CaloShowerStepFromStepPt::collectStepBySimAncestor(const Calorimeter& cal, 
                                                            const PhysicalVolumeMultiHelper& vi,
                                                            const HandleVector& stepsHandles,
                                                            std::map<SimPtr,CaloCompressUtil>& ancestorsMap)
    {       

         std::map<SimPtr,SimPtr> simToAncestorMap;

         for ( HandleVector::const_iterator i=stepsHandles.begin(), e=stepsHandles.end(); i != e; ++i )
         {     
              const art::Handle<StepPointMCCollection>& handle(*i);
              const StepPointMCCollection& steps(*handle);

              if (diagLevel_ > 0) totalStep_ += steps.size();

              for (const auto& step : steps )
              {
                    SimPtr sim = step.simParticle();

                    SimParticlePtrCollection inspectedSims;              
                    while (sim->hasParent() && isInsideCalorimeter(vi,sim) )  
                    {
                        //simparticle starting in one section and ending in another one see note above
                        if (!cal.isContainedSection(sim->startPosition(),sim->endPosition()) ) break; 
                        
                        auto const alreadyInspected = simToAncestorMap.find(sim);
                        if (alreadyInspected != simToAncestorMap.end()) {sim = alreadyInspected->second; break;}

                        inspectedSims.push_back(sim);
                        sim = sim->parent();
                    }
                    
                    for (const SimPtr& inspectedSim : inspectedSims) simToAncestorMap[inspectedSim] = sim;                    
                    ancestorsMap[sim].fill(&step,inspectedSims);
                    
                    totalEdep_ += step.totalEDep();
              }
         }
         
    }


    //-------------------------------------------------------------------------------------------------------------------------
    bool CaloShowerStepFromStepPt::isInsideCalorimeter(const PhysicalVolumeMultiHelper& vi, const art::Ptr<SimParticle>& thisSimPtr)
    {        
        return mapPhysVol_.find(&vi.startVolume(*thisSimPtr)) != mapPhysVol_.end();
        
        //const std::string material = vi.startVolume(*thisSimPtr).materialName();
        //for (std::string const& caloMaterial : caloMaterial_) if (material== caloMaterial) return true;
        //return false;
    }



    //-------------------------------------------------------------------------------------------------------------
    void CaloShowerStepFromStepPt::collectStepBySim(const HandleVector& stepsHandles, 
                                                    std::map<SimPtr,std::vector<const StepPointMC* > >& simStepMap)
    {       
         for ( HandleVector::const_iterator i=stepsHandles.begin(), e=stepsHandles.end(); i != e; ++i )
         {     
              const art::Handle<StepPointMCCollection>& handle(*i);
              const StepPointMCCollection& steps(*handle);

              for (const auto& step : steps ) simStepMap[step.simParticle()].push_back(&step);
         }
    }


    //-------------------------------------------------------------------------------------------------------------------------------
    void CaloShowerStepFromStepPt::compressSteps(const Calorimeter& cal, CaloShowerStepCollection &caloShowerStepMCs, 
                                                 bool isCrystal, int volId, const SimPtr& sim, std::vector<const StepPointMC*>& steps)
    {

          std::sort( steps.begin(), steps.end(), [](const StepPointMC* a, const StepPointMC* b) {return a->time() < b->time();} );      

          ShowerStepUtil buffer(numZSlices_,ShowerStepUtil::weight_type::energy );        
          for (const StepPointMC* step : steps)
          {
               CLHEP::Hep3Vector pos  = (isCrystal) ? cal.toCrystalFrame(volId,step->position()) : cal.toCrystalFrame(cal.crystalByRO(volId),step->position());
               int               idx  = (isCrystal) ? int(pos.z()/zSliceSize_) : 0;

               if (buffer.entries(idx) == 0) buffer.init(idx,step->time(),step->momentum().mag(),pos); 

               if (step->time()-buffer.t0(idx) > deltaTime_)
               { 
                  if (diagLevel_ > 2) 
                  {
                      hZpos_->Fill(idx);
		      hZpos2_->Fill(idx,buffer.energyDep(idx));
                      if (sim->genParticle()) hGenId_->Fill(sim->genParticle()->generatorId().id());
                         else hGenId_->Fill(-1);
                      
                      if (diagLevel_ > 3) {std::cout<<"CaloShowerStepFromStepPt  inserted     "; buffer.printBucket(idx);}                      
                  }    
                  
                  
                  caloShowerStepMCs.push_back(CaloShowerStep(volId, sim, buffer.entries(idx), buffer.time(idx), buffer.energyDep(idx), 
                                                               buffer.pIn(idx), buffer.posIn(idx), buffer.pos(idx), buffer.covPos(idx)));
                  buffer.reset(idx);   
                  buffer.init(idx,step->time(),step->momentum().mag(),pos);
               }

               buffer.add( idx, step->eDep(), step->time(), step->momentum().mag(), pos);
         }


         //do not forget to flush final buffer :-)
         for (int i=0;i<buffer.nBuckets();++i)
         {
              if (buffer.entries(i) == 0) continue;
              caloShowerStepMCs.push_back(CaloShowerStep(volId, sim,  buffer.entries(i), buffer.time(i), buffer.energyDep(i), 
                                                         buffer.pIn(i), buffer.posIn(i), buffer.pos(i), buffer.covPos(i)));
              
              if (diagLevel_ > 2) 
              {
                  hZpos_->Fill(i);
                  if (sim->genParticle()) hGenId_->Fill(sim->genParticle()->generatorId().id()); 
                  else hGenId_->Fill(-1);
                  
                  if (diagLevel_ > 3) {std::cout<<"CaloShowerStepFromStepPt  inserted     ";  buffer.printBucket(i);}
              }    
         }                           
    }

    
    
    
    //------------------------------------------------------------------------------------------------------------------------------------------
    bool CaloShowerStepFromStepPt::isCompressible(int simPdgId, const std::unordered_set<int>& processCodes, const SimParticlePtrCollection& sims)
    {      

        if (simPdgId > 1000000000)                         return true;  //ions are always compressed
        if (procCodes_.find(simPdgId) == procCodes_.end()) return false; 

        const std::set<int>& proc = procCodes_[simPdgId];      
        for (int code : processCodes) if ( proc.find(code) == proc.end() ) return false;

        if (simPdgId==2212 || simPdgId==2112)
        {         
           for (const auto& sim : sims) 
              if (sim->pdgId()==22 || sim->pdgId()==11 || sim->pdgId()==-11) {if (sim->startMomentum().mag() > 1) return false;}
        }         

        return true;    
    }

        

 

}



using mu2e::CaloShowerStepFromStepPt;
DEFINE_ART_MODULE(CaloShowerStepFromStepPt);


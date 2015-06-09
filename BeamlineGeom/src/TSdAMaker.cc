// Mu2e includes
#include "ConfigTools/inc/SimpleConfig.hh"
#include "ConfigTools/inc/checkForStale.hh"
#include "DetectorSolenoidGeom/inc/DetectorSolenoid.hh"
#include "BeamlineGeom/inc/TSdA.hh"
#include "BeamlineGeom/inc/TSdAMaker.hh"

// C++ includes
#include <algorithm>
#include <iostream>
#include <vector>

// CLHEP includes
#include "CLHEP/Vector/ThreeVector.h"

// Other includes
#include "cetlib/exception.h"

namespace mu2e {

  std::unique_ptr<TSdA> TSdAMaker::make(const SimpleConfig& c, const DetectorSolenoid& ds ) {

    checkForStale( "intneutronabs", c );

    std::unique_ptr<TSdA> tsda ( new TSdA() );
    
    tsda->_r4          = c.getDouble("tsda.r4");

    tsda->_halfLength4 = c.getDouble("tsda.halfLength4");

    tsda->_position    = CLHEP::Hep3Vector( ds.position().x(),0,c.getDouble("tsda.z0"));

    tsda->_mat4        = c.getString("tsda.materialName");
    
    return tsda;

  } // make()

} // namespace mu2e
#include "skate3_movie_skip_gate.h"
#include <stdexcept>
void Check(bool value) { if(!value) throw std::runtime_error("Movie skip regression"); }
int main() {
  Skate3MovieSkipGate gate;
  Check(gate.Poll(0,true,false,1500));
  Check(!gate.Poll(500,true,true,1500));
  for(int t=600;t<2000;t+=100) Check(!gate.Poll(t,false,true,1500));
  Check(gate.Poll(2000,false,true,1500));
  Check(!gate.Poll(2010,false,true,1500));
  Check(!gate.Poll(3000,false,true,1500));
  Check(!gate.Poll(3100,false,true,1500));
  // A pending skip belongs to one movie, not the next one after a gap.
  Check(!gate.Poll(3200,true,true,1500));
  Check(!gate.Poll(4000,false,true,1500));
  Check(gate.Poll(4010,true,true,0));
}

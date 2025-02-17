#include "SplineCtrlFeed.h"

namespace rai{

//===========================================================================

void SplineCtrlReference::initialize(const arr& q_real, const arr& qDot_real, double ctrlTime) {
  spline.set()->set(2, ~q_real, {ctrlTime});
}

void SplineCtrlReference::waitForInitialized(){
  while(!spline.get()->times.N) spline.waitForNextRevision();
}

void SplineCtrlReference::getReference(arr& q_ref, arr& qDot_ref, arr& qDDot_ref, const arr& q_real, const arr& qDot_real, double ctrlTime){
  if(!spline.get()->points.N) initialize(q_real, qDot_real, ctrlTime);
  spline.get() -> eval(q_ref, qDot_ref, qDDot_ref, ctrlTime);
}

void SplineCtrlReference::append(const arr& x, const arr& t, double ctrlTime, bool prependLast){
  waitForInitialized();
  arr _x(x), _t(t);
  auto splineSet = spline.set();
  if(prependLast){
    _x.prepend(splineSet->points[-1]);
    _t.prepend(0.);
  }
  if(ctrlTime > splineSet->end()){ //previous spline is done... create new one
    splineSet->set(2, _x, _t+ctrlTime);
  }else{ //previous spline still active... append
    CHECK_GE(t.first(), .01, "that's too harsh! When appending the first time knot should be greater zero (otherwise non-smooth).");
    splineSet->append(_x, _t);
  }
}

void SplineCtrlReference::overrideSmooth(const arr& x, const arr& t, double ctrlTime){
  CHECK(t.first()>.001, "that's too harsh!");
  waitForInitialized();
  arr x_now, xDot_now;
  arr _x(x), _t(t);
  auto splineSet = spline.set();
  splineSet->eval(x_now, xDot_now, NoArr, ctrlTime);
  _x.prepend(x_now);
  _t.prepend(0.);
  splineSet->set(2, _x, _t+ctrlTime, xDot_now);
}

void SplineCtrlReference::overrideHard(const arr& x, const arr& t, double ctrlTime){
  waitForInitialized();

  CHECK_LE(t.first(), .0, "");
  if(t.first()<-.5) LOG(0) <<"you first time knot is more than 500msec ago!";

  auto splineSet = spline.set();

  //only saftey checks: evaluate the old spline
  arr x_old, xDot_old;
  splineSet->eval(x_old, xDot_old, NoArr, ctrlTime);

  splineSet->set(2, x, t+ctrlTime, xDot_old);

  //only saftey checks: evaluate the new spline
  arr x_new, xDot_new;
  splineSet->eval(x_new, xDot_new, NoArr, ctrlTime);
  if(maxDiff(x_old,x_new)>.1) LOG(0) <<"your first point knot is too far from the current spline";
  if(maxDiff(xDot_old,xDot_new)>.5) LOG(0) <<"your initial velocity is too far from the current spline";
}

void SplineCtrlReference::report(double ctrlTime){
  waitForInitialized();
  arr x, xDot;
  auto splineGet = spline.get();
  cout <<"times: current: " <<ctrlTime << " knots: " <<splineGet->times <<endl;
  splineGet->eval(x, xDot, NoArr, splineGet->times.first());
  cout <<"eval(first): " <<x <<' ' <<xDot <<endl;
  splineGet->eval(x, xDot, NoArr, splineGet->times.last());
  cout <<"eval(last): " <<x <<' ' <<xDot <<endl;
  splineGet->eval(x, xDot, NoArr, ctrlTime);
  cout <<"eval(current): " <<x <<' ' <<xDot <<endl;
}

//===========================================================================


void CubicSplineCtrlReference::initialize(const arr& q_real, const arr& qDot_real, double ctrlTime) {
  spline.set()->set((q_real, q_real).reshape(2,-1), zeros(2, q_real.N), {ctrlTime-1., ctrlTime});
}

void CubicSplineCtrlReference::waitForInitialized(){
  while(!spline.get()->times.N) rai::wait(.01); //spline.waitForNextRevision();
}

void CubicSplineCtrlReference::getReference(arr& q_ref, arr& qDot_ref, arr& qDDot_ref, const arr& q_real, const arr& qDot_real, double ctrlTime){
  if(!spline.get()->times.N) initialize(q_real, qDot_real, ctrlTime);
  spline.get() -> eval(q_ref, qDot_ref, qDDot_ref, ctrlTime);
}

void CubicSplineCtrlReference::append(const arr& x, const arr& v, const arr& t, double ctrlTime){
  waitForInitialized();
  if(ctrlTime > getEndTime()){ //previous spline is done... create new one but 'overwrite'
    LOG(1) <<"override";
    overrideSmooth(x, v, t, ctrlTime);
  }else{ //previous spline still active... append
    CHECK_GE(t.first(), .01, "that's too harsh! When appending the first time knot should be greater zero (otherwise non-smooth).");
    spline.set()->append(x, v, t);
  }
}

void CubicSplineCtrlReference::overrideSmooth(const arr& x, const arr& v, const arr& t, double ctrlTime){
  waitForInitialized();
  arr x_now, xDot_now;
  arr _x(x), _v(v), _t(t);
  while(_t.first()<.01){
    LOG(0) <<"time.first()=" <<_t.first() <<"is harsh! -> I'll cut the first waypoint";
    if(_t.N==1) return;
    CHECK_GE(t(1), .001, "that's too harsh!");
    _x.delRows(0);
    _v.delRows(0);
    _t.remove(0);
  }
  auto splineSet = spline.set();
  CHECK_GE(splineSet->times.N, 2, "need a previous spline in order to override");
  splineSet->eval(x_now, xDot_now, NoArr, ctrlTime);
  _x.prepend(x_now);
  _v.prepend(xDot_now);
  _t.prepend(0.);
  splineSet->set(_x, _v, _t+ctrlTime);
}

void CubicSplineCtrlReference::overrideHard(const arr& x, const arr& v, const arr& t, double ctrlTime){
  waitForInitialized();

  CHECK_LE(t.first(), .0, "hard overwrite requires the spline to include a NOW node");
  CHECK_GE(t.first(), -.5, "you first time knot is more than 500msec ago!");

  auto splineSet = spline.set();
  arr x_old, xDot_old;
  splineSet->eval(x_old, xDot_old, NoArr, ctrlTime);

  splineSet->set(x, v, t+ctrlTime);

  arr x_new, xDot_new;
  splineSet->eval(x_new, xDot_new, NoArr, ctrlTime);

  CHECK_LE(maxDiff(x_old, x_new), .1, "your new reference is too far from the current spline");
  CHECK_LE(maxDiff(xDot_old, xDot_new), .5, "your reference velocity is too far from the current spline");
}

void CubicSplineCtrlReference::report(double ctrlTime){
  waitForInitialized();
  arr x, xDot;
  auto splineGet = spline.get();
  cout <<"times: current: " <<ctrlTime << " knots: " <<splineGet->times <<endl;
  splineGet->eval(x, xDot, NoArr, splineGet->times.first());
  cout <<"eval(first): " <<x <<' ' <<xDot <<endl;
  splineGet->eval(x, xDot, NoArr, splineGet->times.last());
  cout <<"eval(last): " <<x <<' ' <<xDot <<endl;
  splineGet->eval(x, xDot, NoArr, ctrlTime);
  cout <<"eval(current): " <<x <<' ' <<xDot <<endl;
  cout <<"pieces: " <<splineGet->pieces.N <<endl;
}

//===========================================================================

} //namespace

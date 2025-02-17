#include "LGP_computers.h"

#include <Kin/viewer.h>
#include <Gui/opengl.h>
#include <iomanip>
#include <Kin/F_qFeatures.h>

rai::LGPComp_root::LGPComp_root(rai::FOL_World& _L, rai::Configuration& _C, bool genericCollisions, const StringA& explicitCollisions, const StringA& explicitLift)
  : L(_L), C(_C), genericCollisions(genericCollisions), explicitCollisions(explicitCollisions), explicitLift(explicitLift) {
  name <<"LGPComp_root#0";

  L.reset_state();

  info = make_shared<LGP_GlobalInfo>();

  isComplete = true;
  fol_astar = make_shared<rai::AStar>(make_shared<rai::FOL_World_State>(L, nullptr, false));
  fol_astar -> verbose = info->verbose - 2;
}

std::shared_ptr<rai::ComputeNode> rai::LGPComp_root::createNewChild(int i){
  return make_shared<LGPcomp_Skeleton>(this, i);
}

//===========================================================================

rai::LGPcomp_Skeleton::LGPcomp_Skeleton(rai::LGPComp_root* _root, int num) : root(_root), num(num) {
  name <<"LGPcomp_Skeleton#"<<num;
}

rai::LGPcomp_Skeleton::LGPcomp_Skeleton(rai::LGPComp_root* _root, const rai::Skeleton& _skeleton) : root(_root), skeleton(_skeleton) {
  name <<"FixedSkeleton";
  planString <<skeleton;

  //same as last part of compute...
  skeleton.collisions=root->genericCollisions;
//  sol = make_shared<SkeletonSolver>(skeleton, root->kin);
  skeleton.addExplicitCollisions(root->explicitCollisions);
  skeleton.addLiftPriors(root->explicitLift);
  createNLPs(root->C);

  isComplete=true;
  l=0.;
  if(root->info->verbose>1) LOG(0) <<skeleton;
}

void rai::LGPcomp_Skeleton::createNLPs(const Configuration& C){
  skeleton.getKomo_path(C, 30, root->info->pathCtrlCosts, -1e-2, -1e-2, root->info->collScale);
  skeleton.getKomo_waypoints(C, 1e-2, -1e-2, root->info->collScale);
  //skeleton.getKOMO_finalSlice(C, 1e-2, -1e-2);
  //skeleton.komoWaypoints->addObjective({}, make_shared<F_q0Bias>(), {"ALL"}, OT_sos, {1e0});
}

void rai::LGPcomp_Skeleton::untimedCompute(){
  //-- get next astar solution
  while(root->fol_astar->solutions.N<=(uint)num){
    root->fol_astar->run();
  }
  //printTree(root->astar->mem); rai::wait();

  //-- extract skeleton
  FOL_World_State* s = dynamic_cast<FOL_World_State*>(root->fol_astar->solutions(-1));
  s->getStateSequence(states, times, planString);

  //cout <<skeletonString <<endl;
  skeleton.setFromStateSequence(states, times);
  //cout <<skeleton <<endl;

  //-- create NLPs
  skeleton.collisions=root->genericCollisions;
//  sol = make_shared<SkeletonSolver>(skeleton, root->kin);
  skeleton.addExplicitCollisions(root->explicitCollisions);
  skeleton.addLiftPriors(root->explicitLift);
  createNLPs(root->C);

  isComplete=true;
  l=0.;

  if(root->info->verbose>0) LOG(0) <<"sket " <<planString;
  if(root->info->verbose>1) LOG(0) <<skeleton;
}

std::shared_ptr<rai::ComputeNode> rai::LGPcomp_Skeleton::createNewChild(int i){
//  return make_shared<FactorBoundsComputer>(this, i);
//  if(states.N) return make_shared<PoseBoundsComputer>(this, i);
  return make_shared<LGPcomp_Waypoints>(this, i);
}

//===========================================================================

rai::PoseBoundsComputer::PoseBoundsComputer(rai::LGPcomp_Skeleton* _sket, int rndSeed) : sket(_sket), seed(rndSeed){
  name <<"PoseBoundsComputer#"<<seed;
}

void rai::PoseBoundsComputer::untimedCompute(){
  t++;

  rai::Skeleton S;
  //-- extract skeleton
  S.setFromStateSequence(sket->states({0, t}), sket->times({0, t}));
  cout <<S <<endl;
  S.addExplicitCollisions(sket->root->explicitCollisions);
  std::shared_ptr<KOMO> komo = S.getKOMO_finalSlice(sket->root->C, 1e-2, -1e-2);

  rnd.seed(seed);
  komo->initRandom(0);
//  komo->opt.animateOptimization = 2;

  NLP_Solver sol;
  sol.setProblem(komo->nlp());
  sol.setInitialization(komo->x);
  sol.solveStepping();

  if(!sol.ret->feasible){
    l = 1e10;
    isComplete = true;
    return;
  }

  if(t==sket->states.N-1){
    l = 0.;
    isComplete = true;
  }
}

std::shared_ptr<rai::ComputeNode> rai::PoseBoundsComputer::createNewChild(int i){
  return make_shared<LGPcomp_Waypoints>(sket, i);
}

//===========================================================================

rai::FactorBoundsComputer::FactorBoundsComputer(rai::LGPcomp_Skeleton* _root, int rndSeed) : sket(_root), seed(rndSeed){
  name <<"FactorBoundsComputer#"<<seed;

  komoWaypoints.clone(*sket->skeleton.komoWaypoints);
  rnd.seed(rndSeed);
  komoWaypoints.initRandom(0);

  nlp = komoWaypoints.nlp_FactoredTime();
  CHECK_EQ(nlp->variableDimensions.N, komoWaypoints.T, "");
}

void rai::FactorBoundsComputer::untimedCompute(){

  nlp->subSelect({t}, {});

  CHECK_EQ(nlp->getDimension(), komoWaypoints.x.N, "");

  //if(sket->verbose()>1) komo->view(sket->verbose()>2, STRING(name <<" - init"));

  NLP_Solver sol;
  sol.setProblem(nlp);
  sol.setInitialization(komoWaypoints.x);
  sol.solveStepping();

//  if(sket->verbose()>0) LOG(0) <<"pose " <<t <<": " <<*sol.ret;
//  if(sket->verbose()>1) nlp->report(cout, 2);
////  if(sket->verbose()>3) komoWaypoints.checkGradients();
//  if(sket->verbose()>1) komoWaypoints.view(sket->verbose()>2, STRING(name <<" - pose " <<t <<" optimized \n" <<*sol.ret));

  if(!sol.ret->feasible){
    l = 1e10;
    isComplete = true;
    if(sket->verbose()>1) komoWaypoints.view_close();
    return;
  }

  t++;
  if(t==komoWaypoints.T){
    isComplete = true;
    if(sket->verbose()>1) komoWaypoints.view_close();
  }
}

std::shared_ptr<rai::ComputeNode> rai::FactorBoundsComputer::createNewChild(int i){
  return make_shared<LGPcomp_Waypoints>(sket, i);
}

//===========================================================================

rai::LGPcomp_Waypoints::LGPcomp_Waypoints(rai::LGPcomp_Skeleton* _sket, int rndSeed) : sket(_sket), seed(rndSeed){
  name <<"LGPcomp_Waypoints#"<<seed;

  komoWaypoints = make_shared<KOMO>();

  komoWaypoints->clone(*sket->skeleton.komoWaypoints);

  rnd.seed(rndSeed);
  komoWaypoints->initRandom(0);
  if(sket->verbose()>2) komoWaypoints->view(sket->verbose()>2, STRING(name <<" - init"));

//  komoWaypoints->opt.animateOptimization=1;

  sol.setProblem(komoWaypoints->nlp());
  sol.setOptions(OptOptions().set_stopEvals(sket->root->info->waypointStopEvals));
  sol.setInitialization(komoWaypoints->x);

  gsol.setProblem(komoWaypoints->nlp_FactoredTime());
}

void rai::LGPcomp_Waypoints::untimedCompute(){
  std::shared_ptr<SolverReturn> ret;
  if(sket->root->info->useSequentialWaypointSolver){
    CHECK(!komoWaypoints->computeCollisions, "useSequentialWaypointSolver doesn't work with genericCollisions")
    gsol.solveInOrder();
    ret = gsol.ret;
  }else{
    for(uint i=0;i<100;i++) if(sol.step()) break;
    ret = sol.ret;
  }

  //    checkJacobianCP(*komoWaypoints->nlp_SparseNonFactored(), komoWaypoints->x, 1e-6);
  if(sket->verbose()>0) LOG(0) <<"ways " <<*ret;
  //if(sket->verbose()>1) cout <<komoWaypoints->getReport(sket->verbose()>3);
  if(sket->verbose()>3){
    //    komoWaypoints->pathConfig.reportProxies();
    komoWaypoints->pathConfig.reportLimits();
    komoWaypoints->checkGradients();
    sol.optCon->L.reportGradients(cout, komoWaypoints->featureNames);
  }
  if(sket->verbose()>1) komoWaypoints->view(sket->verbose()>2, STRING(name <<" - optimized \n" <<*ret));
  //    if(sket->verbose()>1){
  //      static int count=0;
  //      if(ret->feasible)  write_png(komoWaypoints->pathConfig.gl()->getScreenshot(), STRING("z.vid/"<<std::setw(4)<<std::setfill('0')<<count++<<".png"));
  //    }
  if(sket->verbose()>2) while(komoWaypoints->view_play(true));

  if(sket->verbose()>1 && !ret->feasible)

  l = sol.ret->eq + sol.ret->ineq;
  isComplete = ret->done;
  if(isComplete){
    if(ret->ineq>.5 || ret->eq>2.){
      isFeasible = false;
      komoWaypoints->view_close();
      sol.optCon->L.reportGradients(cout, komoWaypoints->featureNames);
    }else isFeasible = true;
  }
}

std::shared_ptr<rai::ComputeNode> rai::LGPcomp_Waypoints::createNewChild(int i){
  komoWaypoints->checkConsistency();
  CHECK(!i, "only single child");
  return make_shared<LGPcomp_RRTpath>(sket, this, 0);
}

//===========================================================================

rai::LGPcomp_RRTpath::LGPcomp_RRTpath(rai::LGPcomp_Skeleton* _root, rai::LGPcomp_Waypoints* _ways, uint _t) : sket(_root), ways(_ways), t(_t){
  name <<"LGPcomp_RRTpath#" <<ways->seed <<'.' <<t;
  if(sket->verbose()>1) LOG(0) <<"rrt for phase:" <<t;
  rai::Skeleton::getTwoWaypointProblem(t, C, q0, qT, *ways->komoWaypoints);
  //cout <<C.getJointNames() <<endl;
  cp = make_shared<ConfigurationProblem>(C, true, .03);
  if(sket->skeleton.explicitCollisions.N) cp->setExplicitCollisionPairs(sket->skeleton.explicitCollisions);
  cp->computeAllCollisions = sket->skeleton.collisions;

  for(rai::Frame *f:C.frames) f->ensure_X();
  rrt = make_shared<RRT_PathFinder>(*cp, q0, qT, .05);
  if(sket->verbose()>1) rrt->verbose=2;
  rrt->maxIters=sket->root->info->rrtStopEvals;
}

void rai::LGPcomp_RRTpath::untimedCompute(){
  int r=0;
  for(uint k=0;k<1000;k++){ r = rrt->stepConnect(); if(r) break; }
  if(r==1){
    isComplete=true;
    l=0.;
    path = path_resampleLinear(rrt->path, 30);
  }
  if(r==-1){
    isComplete=true;
    l=1e10;
    //      if(root->sol->opt.verbose>0) komoPath->view(root->sol->opt.verbose>1, "init path - RRT FAILED");
    if(sket->verbose()>1) LOG(-1) <<"FAILED";
    path.clear();
  }
  if(isComplete){
    cp.reset();
    rrt.reset();
  }
}

std::shared_ptr<rai::ComputeNode> rai::LGPcomp_RRTpath::createNewChild(int i){
  CHECK(!i, "only single child");
  if(t+1 < ways->komoWaypoints->T){
    auto rrt =  make_shared<LGPcomp_RRTpath>(sket, ways, t+1);
    rrt->prev = this;
    return rrt;
  }
  return make_shared<LGPcomp_Path>(sket, ways, this);
}

//===========================================================================

rai::LGPcomp_Path::LGPcomp_Path(rai::LGPcomp_Skeleton* _root, rai::LGPcomp_Waypoints* _ways, rai::LGPcomp_RRTpath* last)
  : sket(_root), ways(_ways){
  name <<"LGPcomp_Path#"<<ways->seed;

  isTerminal = true;

  komoPath = make_shared<KOMO>();
  komoPath->clone(*sket->skeleton.komoPath);

  //collect path and initialize
  rai::Array<LGPcomp_RRTpath*> rrts(ways->komoWaypoints->T);
  rrts=0;
  rrts(-1) = last;
  for(uint t=ways->komoWaypoints->T-1;t--;){
    rrts(t) = rrts(t+1)->prev;
    CHECK_EQ(rrts(t)->t, t, "");
  }

  //first set waypoints (including action parameters!)
  komoPath->initWithWaypoints( ways->komoWaypoints->getPath_qAll() );
//  if(rrtpath.N) komoPath->initWithPath_qOrg(rrtpath);
  komoPath->run_prepare(0.);
  if(sket->verbose()>2) komoPath->view(sket->verbose()>3, STRING(name <<" - init with constant waypoints"));

  for(uint t=0;t<ways->komoWaypoints->T;t++){
    komoPath->initPhaseWithDofsPath(t, rrts(t)->C.getDofIDs(), rrts(t)->path, false);
    if(sket->verbose()>2){
      komoPath->view(sket->verbose()>3, STRING(name <<" - init with rrt part" <<t));
      rai::wait(.1);
    }
  }

  if(sket->verbose()>1) komoPath->view(sket->verbose()>2, STRING(name <<" - init with rrts"));

  komoPath->run_prepare(0.);
  //  komoPath->opt.animateOptimization=2;
  sol.setProblem(komoPath->nlp());
  sol.setInitialization(komoPath->x);
  //sol.setOptions(OptOptions().set_verbose(4));
}

void rai::LGPcomp_Path::untimedCompute(){
  for(uint i=0;i<10;i++) if(sol.step()) break;

  l = sol.ret->eq + sol.ret->ineq;
  isComplete = sol.ret->done;
  if(isComplete){
    if(sket->verbose()>0) LOG(0) <<"path " <<*sol.ret;
    if(sket->verbose()>1) cout <<komoPath->getReport(sket->verbose()>2);
    if(sket->verbose()>0) komoPath->view(sket->verbose()>2, STRING(name <<" - optimized \n" <<*sol.ret <<"\n" <<sket->planString <<"\n"));
    if(sket->verbose()>0) ways->komoWaypoints->view_close();
    //komoPath->checkGradients();
    if(sket->verbose()>1) while(komoPath->view_play(sket->verbose()>1 && sol.ret->feasible));

    sol.ret->feasible = (sol.ret->ineq + sol.ret->eq < 3.);
    if(!sol.ret->feasible){
      //l = 1e10;
      isFeasible = false;
      komoPath->view_close();
      sol.optCon->L.reportGradients(cout, komoPath->featureNames);
    }else{
      isFeasible = true;
      auto path = STRING("z.sol_"<<ID<<"/");
      komoPath->pathConfig.gl().drawOptions.drawVisualsOnly=true;
      komoPath->view_play(false, .1, path);
      ofstream fil (path + "info.txt");
      fil <<*sol.ret <<"\n\nSkeleton:{" <<sket->planString <<"\n}" <<endl;
      fil <<komoPath->getReport(false) <<endl;
      sol.optCon->L.reportGradients(fil, komoPath->featureNames);
      ofstream cfil (path + "last.g");
      komoPath->world.setFrameState(komoPath->getConfiguration_X(komoPath->T-1));
      cfil <<komoPath->world;
      {
//        uint id = komoPath->world["obj"]->ID;
//        FrameL F = komoPath->timeSlices.col(id);
//        arr X = komoPath->pathConfig.getFrameState(F);
//        FILE("obj.path") <<X;
//        rai::wait();
      }
    }
    if(sket->verbose()<2) komoPath->view_close();
  }

  if(isComplete){
    ways->komoWaypoints.reset();
    komoPath.reset();
  }
}

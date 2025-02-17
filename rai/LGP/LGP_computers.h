#pragma once

#include <Search/ComputeNode.h>
#include <Search/AStar.h>
#include <KOMO/komo.h>
#include <KOMO/skeleton.h>
#include <KOMO/pathTools.h>
#include <Logic/folWorld.h>
#include <PathAlgos/ConfigurationProblem.h>
#include <PathAlgos/RRT_PathFinder.h>
#include <Optim/NLP_GraphSolver.h>

namespace rai {

  //===========================================================================

  struct LGP_GlobalInfo {
    RAI_PARAM("LGP/", int, verbose, 1)
    RAI_PARAM("LGP/", double, waypointBranching, 10.)
    RAI_PARAM("LGP/", int, waypointStopEvals, 1000)
    RAI_PARAM("LGP/", int, rrtStopEvals, 10000)
    RAI_PARAM("LGP/", double, pathCtrlCosts, 1.)
    RAI_PARAM("LGP/", double, collScale, 1e1)
    RAI_PARAM("LGP/", bool, useSequentialWaypointSolver, false)
  };

  //===========================================================================

  struct LGPComp_root : ComputeNode {
    FOL_World& L;
    Configuration& C;
    bool genericCollisions=false;
    StringA explicitCollisions;
    StringA explicitLift;
    std::shared_ptr<rai::AStar> fol_astar;
    std::shared_ptr<LGP_GlobalInfo> info;
    bool fixedSkeleton=false;

    LGPComp_root(FOL_World& _L, Configuration& _C, bool genericCollisions, const StringA& explicitCollisions, const StringA& explicitLift);

    virtual void untimedCompute(){}
    virtual int getNumDecisions(){ return -1.; }
    virtual double effortHeuristic(){ return 11.+10.; }
    virtual double branchingPenalty_child(int i){
      double pw=2., w0=1;
      return ::pow(double(i)/w0, pw);
    }

    virtual std::shared_ptr<ComputeNode> createNewChild(int i);
  };

  //===========================================================================

  struct LGPcomp_Skeleton : ComputeNode {
    LGPComp_root *root=0;
    int num=0;
    Skeleton skeleton;
    //shared_ptr<SkeletonSolver> sol;
    rai::String planString;
    Array<Graph*> states;
    arr times;

    LGPcomp_Skeleton(LGPComp_root* _root, int num);
    LGPcomp_Skeleton(LGPComp_root* _root, const Skeleton& _skeleton);

    void createNLPs(const rai::Configuration& C);

    virtual void untimedCompute();

    virtual int getNumDecisions(){ return -1.; }
    virtual double branchingHeuristic(){ return root->info->waypointBranching; }
    virtual double effortHeuristic(){ return 10.+10.; }
    virtual double branchingPenalty_child(int i){
      double pw=2., w0=root->info->waypointBranching;
      return ::pow(double(i)/w0, pw);
    }


    virtual std::shared_ptr<ComputeNode> createNewChild(int i);

    int verbose(){ return root->info->verbose; }
  };

  //===========================================================================

  struct FactorBoundsComputer : ComputeNode {
    LGPcomp_Skeleton *sket;
    int seed=0;
    KOMO komoWaypoints;
    std::shared_ptr<NLP_Factored> nlp;
    uint t=0;

    FactorBoundsComputer(LGPcomp_Skeleton *_root, int rndSeed);

    virtual void untimedCompute();
    virtual double effortHeuristic(){ return 10.+1.*(komoWaypoints.T); }
    virtual int getNumDecisions(){ return 1; }
    virtual std::shared_ptr<ComputeNode> createNewChild(int i);
  };

  //===========================================================================

  struct PoseBoundsComputer : ComputeNode {
    LGPcomp_Skeleton *sket;
    int seed=0;
    uint t=0;

    PoseBoundsComputer(LGPcomp_Skeleton *_sket, int rndSeed);

    virtual void untimedCompute();
    virtual double effortHeuristic(){ return 10.+1.*(sket->states.N); }
    virtual int getNumDecisions(){ return 1; }
    virtual std::shared_ptr<ComputeNode> createNewChild(int i);
  };

  //===========================================================================

  struct LGPcomp_Waypoints : ComputeNode {
    LGPcomp_Skeleton *sket;
    int seed=0;
    std::shared_ptr<KOMO> komoWaypoints;
    NLP_Solver sol;
    NLP_GraphSolver gsol;

    LGPcomp_Waypoints(LGPcomp_Skeleton *_sket, int rndSeed);

    virtual void untimedCompute();
    virtual double effortHeuristic(){ return 10.+1.*(komoWaypoints->T); }
    virtual int getNumDecisions(){ return 1; }
    virtual std::shared_ptr<ComputeNode> createNewChild(int i);
  };

  //===========================================================================

  struct LGPcomp_RRTpath : ComputeNode {
    LGPcomp_Skeleton *sket=0;
    LGPcomp_Waypoints * ways=0;
    LGPcomp_RRTpath *prev=0;

    Configuration C;
    uint t;
    shared_ptr<ConfigurationProblem> cp;
    shared_ptr<RRT_PathFinder> rrt;
    arr q0, qT;
    arr path;

    LGPcomp_RRTpath(LGPcomp_Skeleton *_root, LGPcomp_Waypoints *_ways, uint _t);

    virtual void untimedCompute();
    virtual double effortHeuristic(){ return 10.+1.*(ways->komoWaypoints->T-t-1); }

    virtual int getNumDecisions(){ return 1; }
    virtual std::shared_ptr<ComputeNode> createNewChild(int i);
  };

  //===========================================================================

  struct LGPcomp_Path : ComputeNode {
    LGPcomp_Skeleton *sket=0;
    LGPcomp_Waypoints *ways=0;

    shared_ptr<KOMO> komoPath;
    NLP_Solver sol;

    LGPcomp_Path(LGPcomp_Skeleton *_root, LGPcomp_Waypoints *_ways, LGPcomp_RRTpath *last);

    virtual void untimedCompute();
    virtual double effortHeuristic(){ return 0.; }

    virtual double sample(){
      CHECK(sol.ret, "");
      CHECK(sol.ret->done, "");
      if(sol.ret->ineq>1. || sol.ret->eq>4.) return 1e10;
      return sol.ret->ineq+sol.ret->eq;
    }

//        if(opt.verbose>0) LOG(0) <<*ret;
//        if(opt.verbose>1) cout <<komoPath.getReport(opt.verbose>2);
//        if(opt.verbose>0) komoPath.view(opt.verbose>1, STRING("optimized path\n" <<*ret));
//        //komoPath.checkGradients();
//        if(opt.verbose>1) while(komoPath.view_play(opt.verbose>2));

    virtual int getNumDecisions(){ return 0; }
    virtual std::shared_ptr<ComputeNode> createNewChild(int i){ HALT("is terminal"); }
  };

  //===========================================================================

}//namespace

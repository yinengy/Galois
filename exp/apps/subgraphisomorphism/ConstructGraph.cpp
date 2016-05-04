#include "Galois/Galois.h"
#include "Galois/Bag.h"
#include "Galois/Statistic.h"
#include "Galois/Timer.h"
#include "Galois/Graphs/Graph.h"
#include "Galois/Graphs/TypeTraits.h"
#include "llvm/Support/CommandLine.h"
#include "Lonestar/BoilerPlate.h"

#include <chrono>
#include <random>

#include <vector>
#include <iostream>

namespace cll = llvm::cl;

static const char* name = "Graph Construction";
static const char* desc = "Construct a random graph";
static const char* url = "construct_graph";

static cll::opt<bool> constructUndirected("constructUndirected", cll::desc("construct undirected graphs"), cll::init(false));

static cll::opt<unsigned int> numNodes("numNodes", cll::desc("# nodes"), cll::init(9));
static cll::opt<unsigned int> numEdges("numEdges", cll::desc("# edges"), cll::init(20));

static cll::opt<bool> rndSeedByTime("rndSeedByTime", cll::desc("rndSeed generated by system time"), cll::init(false));
static cll::opt<unsigned int> rndSeed("rndSeed", cll::desc("random seed"), cll::init(0));

static std::minstd_rand0 generator;
static std::uniform_int_distribution<unsigned> distribution;

template<typename Graph>
void constructGraph(Graph& g) {
  typedef typename Graph::GraphNode GNode;

  // construct a set of nodes
  std::vector<GNode> nodes(numNodes);
  for(unsigned int i = 0; i < numNodes; ++i) {
    GNode n = g.createNode(0);
    g.addNode(n);
    g.getData(n) = i;
    nodes[i] = n;
  }

  // add edges
  for(unsigned int i = 0; i < numEdges; ) {
    unsigned int src = distribution(generator) % numNodes;
    unsigned int dst = distribution(generator) % numNodes;

    // no self loops and repeated edges
    if(src != dst && g.findEdge(nodes[src], nodes[dst]) == g.edge_end(nodes[src])) {
      g.addEdge(nodes[src], nodes[dst]);
      if(constructUndirected) {
        g.addEdge(nodes[dst], nodes[src]);
      }
      ++i;
    }
  }
}

template<typename Graph>
void printGraph(Graph& g) {
  for(auto ni = g.begin(), ne = g.end(); ni != ne; ++ni) {
    auto& src = g.getData(*ni);
    for(auto ei = g.edge_begin(*ni), ee = g.edge_end(*ni); ei != ee; ++ei) {
      auto& dst = g.getData(g.getEdgeDst(ei));
      std::cout << src << " " << dst << std::endl;
    }
  }
  std::cout << std::endl;
}

int main(int argc, char **argv) {
  Galois::StatManager statManager;
  LonestarStart(argc, argv, name, desc, url);

  if(rndSeedByTime) {
    rndSeed = std::chrono::system_clock::now().time_since_epoch().count();
  }
  std::cout << "rndSeed: " << rndSeed << std::endl;
  unsigned int seed = rndSeed;
  generator.seed(seed);

  unsigned int maxNumEdges = numNodes * (numNodes - 1);
  if(constructUndirected) {
    maxNumEdges /= 2;
  }
  if(numEdges > maxNumEdges) {
    numEdges = maxNumEdges;
  }

  Galois::Graph::FirstGraph<int, void, true> g;
  constructGraph(g);
  printGraph(g);

  return 0;
}

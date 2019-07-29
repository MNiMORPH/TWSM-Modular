#include "fill_spill_merge.hpp"
#include "../common/netcdf.hpp"
#include <iostream>
#include <richdem/common/Array2D.hpp>
#include <string>
#include <stdexcept>

namespace rd = richdem;
namespace dh = richdem::dephier;

int main(int argc, char **argv){
  if(argc!=5){
    std::cerr<<"Syntax: "<<argv[0]<<" <Topo Input> <WTD Input> <Output> <OutGraph>"<<std::endl;
    return -1;
  }

  const std::string in_name   = argv[1];
  const std::string wtd_name   = argv[2];

  const std::string out_name  = argv[3];
  const std::string out_graph = argv[4];

  rd::Timer timer_io;
  timer_io.start();
  rd::Array2D<float> topo = LoadData<float>(in_name,std::string("value"));   //Load in the topography file
  rd::Array2D<float> wtd = LoadData<float>(wtd_name,std::string("value"));   //Load in the wtd file
  timer_io.stop();

  rd::Array2D<dh::dh_label_t> label   (topo.width(), topo.height(), dh::NO_DEP ); //No cells are part of a depression
  rd::Array2D<dh::dh_label_t> final_label   (topo.width(), topo.height(), dh::NO_DEP ); //No cells are part of a depression

  rd::Array2D<rd::flowdir_t>  flowdirs(topo.width(), topo.height(), rd::NO_FLOW); //No cells flow anywhere

  wtd.setNoData(topo.noData());

  //Label the ocean cells. This is a precondition for using
  //`GetDepressionHierarchy()`.
  #pragma omp parallel for
  for(unsigned int i=0;i<label.size();i++){
    if(topo.isNoData(i) || topo(i)==dh::OCEAN_LEVEL){ //Ocean Level is assumed to be lower than any other cells (even Death Valley)
      label(i) = dh::OCEAN;
      final_label(i) = dh::OCEAN;
      wtd  (i) = 0;
    }
  }

  //Generate flow directions, label all the depressions, and get the hierarchy
  //connecting them
  auto deps = dh::GetDepressionHierarchy<float,rd::Topology::D8>(topo, label, final_label, flowdirs);

  dh::FillSpillMerge(topo, label, final_label, flowdirs, deps, wtd);

  //TODO: Remove. For viewing test cases.
  if(label.width()<1000){
    //GraphViz dot-style output for drawing depression hierarchy graphs.
    std::ofstream fgraph(out_graph);
    fgraph<<"digraph {\n";
    for(int i=0;i<(int)deps.size();i++){
      fgraph<<i<<" -> "<<deps[i].parent;
      if(deps[i].parent!=dh::NO_VALUE && (deps[i].parent==dh::OCEAN || !(deps[deps[i].parent].lchild==i || deps[deps[i].parent].rchild==i)))
        fgraph<<" [color=\"blue\"]";
      fgraph<<";\n";
    }
    fgraph<<"}\n";
  }

  for(unsigned int i=0;i<topo.size();i++)
    if(!topo.isNoData(i))
      wtd(i) += topo(i);

  SaveAsNetCDF(wtd,out_name+"-flooded.nc","value");

  rd::FillDepressions<rd::Topology::D8>(topo);
  SaveAsNetCDF(topo,out_name+"-filled.nc","value");

  rd::Array2D<float> diff(wtd);
  for(unsigned int i=0;i<topo.size();i++)
    diff(i) = wtd(i)-topo(i);
  SaveAsNetCDF(diff,out_name+"-diff.nc","value");

  std::cerr<<"Finished"<<std::endl;
  std::cerr<<"IO time   = "<<timer_io.accumulated()<<" s"<<std::endl;

  return 0;
}

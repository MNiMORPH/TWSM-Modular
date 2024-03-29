#include "transient_groundwater.hpp"
#include "fill_spill_merge.hpp"
#include "evaporation.hpp"

#include "../common/netcdf.hpp"
#include "ArrayPack.hpp"
#include "parameters.hpp"
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <richdem/common/Array2D.hpp>
#include <richdem/common/timer.hpp>
#include <richdem/common/ProgressBar.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>
using namespace std;

namespace rd = richdem;
namespace dh = richdem::dephier;

const double UNDEF  = -1.0e7;


int main(int argc, char **argv){

  if(argc!=2){                               //Make sure that the user is running the code with a configuration file. 
    std::cerr<<"Syntax: "<<argv[0]<<" <Configuration File>"<<std::endl;
    return -1;
  }


  ArrayPack arp;
  std::cerr<<"Argv"<<argv<<std::endl;
  Parameters params(argv[1]);

  ofstream textfile;
  textfile.open (params.textfilename, std::ios_base::app);  //Text file to save outputs of how much is changing and min and max wtd at various times



//Initialise all of the values for an equilibrium or a transient-style run:
//Open data files, set the changing cellsize arrays, do any needed unit conversions. 
//load in the data files: ksat, mask, slope, precipitation, temperature, relative humidity, topography, and evaporation files. 


  arp.ksat = LoadData<float>(params.surfdatadir + params.region + "coarser_ksat.nc", "value");   //Units of ksat are m/s. 

  params.ncells_x = arp.ksat.width();  //width and height in number of cells in the array
  params.ncells_y = arp.ksat.height();

  if(params.run_type=="transient"){
    textfile<<"Initialise transient"<<std::endl;


    arp.slope_start         = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_slope.nc",  "value");  //Slope as a value from 0 to 1. 
    arp.precip_start        = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_precip.nc", "value");  //Units: m/yr. 
    arp.temp_start          = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_temp.nc",   "value");  //Units: degress Celsius
    arp.topo_start          = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_topo.nc",   "value");  //Units: metres
    arp.starting_evap_start = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_evap.nc",   "value");  //Units: m/yr
    arp.relhum_start        = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_relhum.nc", "value");  //Units: proportion from 0 to 1.

    arp.slope_end         = LoadData<float>(params.surfdatadir + params.region + params.time_end + "_coarser_slope.nc",  "value");  //Slope as a value from 0 to 1. 
    arp.land_mask         = LoadData<float>(params.surfdatadir + params.region + params.time_end + "_coarser_mask.nc",   "value");  //A binary mask that is 1 where there is land and 0 in the ocean
    arp.precip_end        = LoadData<float>(params.surfdatadir + params.region + params.time_end + "_coarser_precip.nc", "value");  //Units: m/yr. 
    arp.temp_end          = LoadData<float>(params.surfdatadir + params.region + params.time_end + "_coarser_temp.nc",   "value");  //Units: degress Celsius
    arp.topo_end          = LoadData<float>(params.surfdatadir + params.region + params.time_end + "_coarser_topo.nc",   "value");  //Units: metres
    arp.starting_evap_end = LoadData<float>(params.surfdatadir + params.region + params.time_end + "_coarser_evap.nc",   "value");  //Units: m/yr
    arp.relhum_end        = LoadData<float>(params.surfdatadir + params.region + params.time_end + "_coarser_relhum.nc", "value");  //Units: proportion from 0 to 1.

//load in the wtd result from the previous time: 
    arp.wtd    = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_wtd.nc", "value");


//calculate the fdepth (e-folding depth, representing rate of decay of the hydraulic conductivity with depth) arrays:
    arp.fdepth_start = rd::Array2D<float>(arp.topo,0); 
    arp.fdepth_end   = rd::Array2D<float>(arp.topo,0); 
    for(unsigned int i=0;i<arp.topo_start.size();i++){
      if(arp.temp_start(i) > -5)  //then fdepth = f from Ying's equation S7. 
        arp.fdepth_start(i) = std::max(1000/(1+150*arp.slope_start(i)),25.0f);  //TODO: allow user to vary these calibration constants depending on their input cellsize? Or do some kind of auto variation of them? 
      else{ //then fdpth = f*fT, Ying's equations S7 and S8. 
        if(arp.temp_start(i) < -14)
          arp.fdepth_start(i) = (std::max(1000/(1+150*arp.slope_start(i)),25.0f)) * (std::max(0.05, 0.17 + 0.005 * arp.temp_start(i)));
        else
          arp.fdepth_start(i) = (std::max(1000/(1+150*arp.slope_start(i)),25.0f)) * (std::min(1.0, 1.5 + 0.1 * arp.temp_start(i)));
      }
      if(arp.temp_end(i) > -5)  //then fdepth = f from Ying's equation S7. 
        arp.fdepth_end(i) = std::max(1000/(1+150*arp.slope_end(i)),25.0f);  //TODO: allow user to vary these calibration constants depending on their input cellsize? Or do some kind of auto variation of them? 
      else{ //then fdpth = f*fT, Ying's equations S7 and S8. 
        if(arp.temp_end(i) < -14)
          arp.fdepth_end(i) = (std::max(1000/(1+150*arp.slope_end(i)),25.0f)) * (std::max(0.05, 0.17 + 0.005 * arp.temp_end(i)));
        else
          arp.fdepth_end(i) = (std::max(1000/(1+150*arp.slope_end(i)),25.0f)) * (std::min(1.0, 1.5 + 0.1 * arp.temp_end(i)));
      }
    }

  //initialise the arrays to be as at the starting time:
    arp.fdepth        = arp.fdepth_start;
    arp.precip        = arp.precip_start;
    arp.temp          = arp.temp_start;
    arp.topo          = arp.topo_start;
    arp.starting_evap = arp.starting_evap_start;
    arp.relhum        = arp.relhum_start;
    arp.slope         = arp.slope_start;

  }
  else if(params.run_type == "equilibrium"){
    textfile<<"Initialise equilibrium"<<std::endl;

    arp.slope         = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_slope.nc",  "value");  //Slope as a value from 0 to 1. 
    arp.land_mask     = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_mask.nc",   "value"); //A binary mask that is 1 where there is land and 0 in the ocean
    arp.precip        = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_precip.nc", "value");  //Units: m/yr. 
    arp.temp          = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_temp.nc",   "value");  //Units: degress Celsius
    arp.topo          = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_topo.nc",   "value");  //Units: metres
    arp.starting_evap = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_evap.nc",   "value");  //Units: m/yr
    arp.relhum        = LoadData<float>(params.surfdatadir + params.region + params.time_start + "_coarser_relhum.nc", "value");  //Units: proportion from 0 to 1.
    arp.wtd           = rd::Array2D<float>(arp.topo,0.0);  //we start with a water table at the surface for equilibrium runs. 


    arp.fdepth   = rd::Array2D<float>(arp.topo,0); 
    for(unsigned int i=0;i<arp.topo.size();i++){
      if(arp.temp(i) > -5)  //then fdepth = f from Ying's equation S7. 
        arp.fdepth(i) = std::max(1000/(1+150*arp.slope(i)),25.0f);  //TODO: allow user to vary these calibration constants depending on their input cellsize? Or do some kind of auto variation of them? 
      else{ //then fdpth = f*fT, Ying's equations S7 and S8. 
        if(arp.temp(i) < -14)
          arp.fdepth(i) = (std::max(1000/(1+150*arp.slope(i)),25.0f)) * (std::max(0.05, 0.17 + 0.005 * arp.temp(i)));
        else
          arp.fdepth(i) = (std::max(1000/(1+150*arp.slope(i)),25.0f)) * (std::min(1.0, 1.5 + 0.1 * arp.temp(i)));
      }
    }
  }
  else{
    throw std::runtime_error("That was not a recognised run type! Please choose transient or equilibrium.");
  }


  //Initialise arrays that start out empty:

  arp.wtd_old       = arp.wtd;
  arp.wtd_mid       = arp.wtd;
  arp.runoff        = rd::Array2D<float>(arp.topo,0);
  arp.head          = rd::Array2D<float>(arp.topo,0);        //Just to initialise these - we'll add the appropriate values later. 
  arp.kcell         = rd::Array2D<float>(arp.topo,0);
  arp.evap          = rd::Array2D<float>(arp.topo,0);  
  arp.e_sat         = rd::Array2D<float>(arp.topo,0);  
  arp.e_a           = rd::Array2D<float>(arp.topo,0);  
  arp.surface_evap  = rd::Array2D<float>(arp.topo,0);  
  arp.surface_water = rd::Array2D<float>(arp.topo,0);

  arp.infiltration_array = rd::Array2D<float>(arp.topo,0);
  arp.evaporation_array = rd::Array2D<float>(arp.topo,0);
  arp.surface_array = rd::Array2D<float>(arp.topo,0);


  arp.wtd_change_total = rd::Array2D<float>(arp.topo,0.0f);   //This array is used to store the values of how much the water table will change in one iteration, then adding it to wtd gets the new wtd.

  textfile<<"loaded all"<<std::endl;

//**************************************************************************************************************************************************************
  //compute changing cell size and distances between cells as these change with latitude:

  const float earth_radius = 6371000.; //metres

  params.cellsize_n_s_metres = (earth_radius*(M_PI/180.))/params.cells_per_degree; //distance between lines of latitude is a constant. 

//initialise some arrays
  arp.latitude_radians.resize       (params.ncells_y);  //the latitude of each row of cells
  arp.cellsize_e_w_metres.resize    (params.ncells_y);  //size of a cell in the east-west direction at the centre of the cell (metres)
  arp.cellsize_e_w_metres_N.resize  (params.ncells_y);  //size of a cell in the east-west direction at the northern edge of the cell (metres)
  arp.cellsize_e_w_metres_S.resize  (params.ncells_y);  //size of a cell in the east-west direction at the southern edge of the cell (metres)
  arp.cell_area.resize              (params.ncells_y);  //cell area (metres squared)


  for(unsigned int j=0;j<arp.latitude_radians.size();j++){
    //latitude at the centre of a cell:
    arp.latitude_radians[j]       = (float(j)/params.cells_per_degree+params.southern_edge)*(M_PI/180.); //southern edge of the domain in degrees, plus the number of cells up from this location/the number of cells per degree, converted to radians.

    //cells_per_degree = 120, there are this many 30 arc-second pieces in one degree. (or however many pieces per degree the user has)
    //j/cells_per_degree gives the number  of degrees up from the southern edge, add southern_edge since the southern edge may not be at 0 latitude. *pi/180 to convert to radians. 
    //latitude_radians is now the latitude in radians. 

    //latitude at the southern edge of a cell (subtract half a cell):
    double latitude_radians_S = ((float(j) - 0.5)/params.cells_per_degree+params.southern_edge)*(M_PI/180.); 
    //latitude at the northern edge of a cell (add half a cell):
    double latitude_radians_N = ((float(j) + 0.5)/params.cells_per_degree+params.southern_edge)*(M_PI/180.); 

    //distance between lines of longitude varies with latitude. This is the distance at the centre of a cell for a given latitude:
    arp.cellsize_e_w_metres[j] = earth_radius*std::cos(arp.latitude_radians[j])*(M_PI/180.)/params.cells_per_degree;

    //distance at the northern edge of the cell for the given latitude:
    arp.cellsize_e_w_metres_N[j] = earth_radius*std::cos(latitude_radians_N)*(M_PI/180.)/params.cells_per_degree;
    //distance at the southern edge of the cell for the given latitude:
    arp.cellsize_e_w_metres_S[j] = earth_radius*std::cos(latitude_radians_S)*(M_PI/180.)/params.cells_per_degree;

    //cell area computed as a trapezoid, using unchanging north-south distance, and east-west distances at the northern and southern edges of the cell:
    arp.cell_area[j] = params.cellsize_n_s_metres* (arp.cellsize_e_w_metres_N[j] + arp.cellsize_e_w_metres_S[j])/2;


//TODO: see which, if any, arrays can be cleared after use to free up memory. How to do this?
  }

  textfile<<"computed distances, areas, and latitudes"<<std::endl;

  //Change undefined cells to 0
  for(unsigned int i=0;i<arp.topo.size();i++){
    if(arp.topo(i)<=UNDEF){
      arp.topo(i) = 0;
    }

    //Converting to appropriate time step
    arp.precip(i)        *= (params.deltat/(60*60*24*365));                 //convert to appropriate units for the time step. 
    arp.starting_evap(i) *= (params.deltat/(60*60*24*365));                  //convert to appropriate units for the time step. 
  } 


  arp.check();

//get the starting recharge using precip and evap inputs:

  #pragma omp parallel for
  for(unsigned int i=0;i<arp.topo.size();i++){
    arp.evap(i) = arp.starting_evap(i);               //Use a different array for evaporation here because we want to be able to switch back to the original evaporation in cases where there is surface water for a while, but it goes away.
    arp.runoff(i) = (arp.precip(i)-arp.evap(i));
    arp.runoff(i) = std::max(arp.runoff(i),0.0f);         //Runoff is always positive. 
  }


//Wtd is 0 in the ocean:
  #pragma omp parallel for
  for(unsigned int i=0;i<arp.topo.size();i++){
    if(arp.land_mask(i) == 0){ 
      arp.wtd  (i) = 0;
    }
  }

 //We have finished initialising all of the arrays and data that will be used for groundwater. 


//initialise the things we need for surface water:

  rd::Array2D<dh::dh_label_t> label   (params.ncells_x, params.ncells_y, dh::NO_DEP ); //No cells are part of a depression
  rd::Array2D<dh::dh_label_t> final_label   (params.ncells_x, params.ncells_y, dh::NO_DEP ); //No cells are part of a depression
  rd::Array2D<rd::flowdir_t>  flowdirs(params.ncells_x, params.ncells_y, rd::NO_FLOW); //No cells flow anywhere

    //Label the ocean cells. This is a precondition for using
    //`GetDepressionHierarchy()`.
    #pragma omp parallel for
    for(unsigned int i=0;i<label.size();i++){
      if(arp.land_mask(i) == 0.0f){ 
        label(i) = dh::OCEAN;
        final_label(i) = dh::OCEAN;
      }
    }

    //Generate flow directions, label all the depressions, and get the hierarchy
    //connecting them
  auto deps = dh::GetDepressionHierarchy<float,rd::Topology::D8>(arp, label, final_label, flowdirs);

 int cycles_done = 0;
 float total_wtd_change = 0.0;
 float wtd_mid_change = 0.0;
 float GW_wtd_change = 0.0;
 float abs_total_wtd_change = 0.0;
 float abs_wtd_mid_change = 0.0;
 float abs_GW_wtd_change = 0.0;
 float evaporation_change = 0.0;
 float infiltration_change = 0.0;
 float surface_change = 0.0;


while(true){

  if(params.run_type == "transient"){
  //I am going to do this just as if the iterations are set to 10 years and I want to update the data files every 10 years here (i.e. updates every iteration),
  // but probably there should be an option for how often you want to do the update. Perhaps every x amount of iterations as a user input. TODO


    for(unsigned int i=0;i<arp.topo.size();i++){

      arp.fdepth(i)         = (arp.fdepth_start(i)        * (1-(cycles_done/params.total_cycles))) + (arp.fdepth_end(i)        * (cycles_done/params.total_cycles));
      arp.precip(i)         = (arp.precip_start(i)        * (1-(cycles_done/params.total_cycles))) + (arp.precip_end(i)        * (cycles_done/params.total_cycles));
      arp.temp(i)           = (arp.temp_start(i)          * (1-(cycles_done/params.total_cycles))) + (arp.temp_end(i)          * (cycles_done/params.total_cycles));
      arp.topo(i)           = (arp.topo_start(i)          * (1-(cycles_done/params.total_cycles))) + (arp.topo_end(i)          * (cycles_done/params.total_cycles));
      arp.starting_evap(i)  = (arp.starting_evap_start(i) * (1-(cycles_done/params.total_cycles))) + (arp.starting_evap_end(i) * (cycles_done/params.total_cycles));
      arp.relhum(i)         = (arp.relhum_start(i)        * (1-(cycles_done/params.total_cycles))) + (arp.relhum_end(i)        * (cycles_done/params.total_cycles));


      //Converting to appropriate time step
      arp.precip(i)        *= (params.deltat/(60*60*24*365));                  //convert to appropriate units for the time step. 
      arp.starting_evap(i) *= (params.deltat/(60*60*24*365));                  //convert to appropriate units for the time step. 
    
      label(i)        = dh::NO_DEP; //No cells are part of a depression
      final_label(i)  = dh::NO_DEP; //No cells are part of a depression
      flowdirs(i)     = rd::NO_FLOW; //No cells flow anywhere
    } 

    #pragma omp parallel for
    for(unsigned int i=0;i<label.size();i++){
      if(arp.land_mask(i) == 0.0f){ 
        label(i) = dh::OCEAN;
        final_label(i) = dh::OCEAN;
      }
    }
    //with transient runs, we have to redo the depression hierarchy every time, since the topography is changing. 
    auto deps = dh::GetDepressionHierarchy<float,rd::Topology::D8>(arp, label, final_label, flowdirs);
  }


  textfile<<"Cycles done: "<<cycles_done<<std::endl;
  if(cycles_done == params.total_cycles)  //For transient - user set param that I am setting for now at 50 to get 500 years total. 
    break;

//TODO: How should equilibrium know when to exit?

  if((cycles_done % 100) == 0){
    textfile<<"saving partway result"<<std::endl;  
    string cycles_str = to_string(cycles_done);
    SaveAsNetCDF(arp.wtd,params.outfilename + cycles_str +".nc","value");
  }

  arp.wtd_old = arp.wtd;
  arp.wtd_mid = arp.wtd;

  for(int y=1;y<params.ncells_y-1;y++)
  for(int x=1;x<params.ncells_x-1; x++){
    if(arp.land_mask(x,y) == 0)      //skip ocean cells
      continue;
    arp.surface_water(x,y) = arp.runoff(x,y);
  }
  
  //Move surface water.
  dh::FillSpillMerge(params,arp.topo, label, final_label, flowdirs, deps, arp.wtd,arp);

  arp.wtd_mid = arp.wtd;

  //Run the groundwater code to move water

  groundwater(params,arp);

  //check to see where there is surface water, and adjust how evaporation works at these locations. 
  evaporation_update(params,arp);

  abs_total_wtd_change = 0.0;
  abs_wtd_mid_change = 0.0;
  abs_GW_wtd_change = 0.0;
  total_wtd_change = 0.0;
  wtd_mid_change = 0.0;
  GW_wtd_change = 0.0;

  for(int y=1;y<params.ncells_y-1;y++)
  for(int x=1;x<params.ncells_x-1; x++){
    abs_total_wtd_change += fabs(arp.wtd(x,y)     - arp.wtd_old(x,y));
    abs_GW_wtd_change    += fabs(arp.wtd(x,y)     - arp.wtd_mid(x,y));
    abs_wtd_mid_change   += fabs(arp.wtd_mid(x,y) - arp.wtd_old(x,y));
    total_wtd_change     += (arp.wtd(x,y)         - arp.wtd_old(x,y));
    GW_wtd_change        += (arp.wtd(x,y)         - arp.wtd_mid(x,y));
    wtd_mid_change       += (arp.wtd_mid(x,y)     - arp.wtd_old(x,y));
    evaporation_change   += arp.evaporation_array(x,y);
    infiltration_change  += arp.infiltration_array(x,y);
    surface_change       += arp.surface_array(x,y);

  }

    textfile<<"cycles_done "<<cycles_done<<std::endl;
    textfile<<"total wtd change was "<<total_wtd_change<<" change in GW only was "<<GW_wtd_change<<" and change in SW only was "<<wtd_mid_change<<std::endl;
    textfile<<"absolute value total wtd change was "<<abs_total_wtd_change<<" change in GW only was "<<abs_GW_wtd_change<<" and change in SW only was "<<abs_wtd_mid_change<<std::endl;
    textfile<<"the change in infiltration was "<<infiltration_change<<" change in evaporation was "<<evaporation_change<<" and change to surface water was "<<surface_change<<std::endl;

  arp.wtd_old = arp.wtd;

  cycles_done += 1;
}

//We are finished, save the result.   
  textfile<<"done with processing"<<std::endl;  
  SaveAsNetCDF(arp.wtd,params.outfilename,"value");

  textfile.close();

  return 0;
}
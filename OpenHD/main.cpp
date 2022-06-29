//
// Created by consti10 on 02.05.22.
//

#include <iostream>
#include <memory>
#include <sstream>

#include "ohd_common/openhd-platform.hpp"
#include "ohd_common/openhd-profile.hpp"
#include "ohd_common/openhd-platform-discover.hpp"

#include <DCameras.h>
#include <OHDInterface.h>
#include <OHDVideo.h>
#include <OHDTelemetry.hpp>

//TODO fix the cmake crap and then we can build a single executable.
static const char optstr[] = "?:da";
static const struct option long_options[] = {
	{"skip_discovery", no_argument, nullptr, 'd'},
	{"force_air", no_argument, nullptr, 'a'},
	{"force_ground", no_argument, nullptr, 'g'},
	{nullptr, 0, nullptr, 0},
};

struct OHDRunOptions {
  //bool skip_discovery = false;
  bool force_air = false;
  bool force_ground=false;
};

static OHDRunOptions parse_run_parameters(int argc, char *argv[]){
  OHDRunOptions ret{};
  int c;
  while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
    const char *tmp_optarg = optarg;
    switch (c) {
      //case 'd':options.skip_discovery = true;
      //	break;
      case 'a':ret.force_air = true;
        break;
      case 'g':ret.force_ground = true;
        break;
      case '?':
      default:
        std::cout << "Usage: --skip_detection [Skip detection step, usefully for changing things in json manually] \n" <<
                  "force_air [Force to boot as air pi, even when no camera is detected] \n" <<
                  "force_ground [Force to boot as ground pi,even though one or more cameras are connected] \n";
        exit(1);
    }
  }
  if(OHDFilesystemUtil::exists("/boot/air.txt")){
    ret.force_air=true;
  }
  if(OHDFilesystemUtil::exists("/boot/ground.txt")){
    ret.force_ground=true;
  }
  if(ret.force_air && ret.force_ground){
    std::cerr << "Cannot force air and ground at the same time\n";
    exit(1);
  }
  return ret;
}

int main(int argc, char *argv[]) {
  // parse some arguments usefully for debugging
  const OHDRunOptions options=parse_run_parameters(argc,argv);

  std::cout << "OpenHD START with " <<"\n"<<
			//"skip_discovery:" << (options.skip_discovery ? "Y" : "N") <<"\n"<<
			"force_air:" << (options.force_air ? "Y" : "N") <<"\n"<<
			"force_ground:" << (options.force_ground ? "Y" : "N") <<"\n";

  try {
	// First discover the platform:
	const auto platform = DPlatform::discover();
	std::cout<<platform->to_string()<<"\n";

	// Now we need to discover detected cameras, to determine the n of cameras and then
	// decide if we are air or ground unit
	std::vector<Camera> cameras{};
    // To force ground, we just skip the discovery step (0 cameras means ground automatically)
	if (!options.force_ground){
	  cameras = DCameras::discover(*platform);
  	}
	// and by just adding a dummy camera we automatically become air
  	if(options.force_air && cameras.empty()) {
		cameras.emplace_back(createDummyCamera());
	}
	for(const auto& camera:cameras){
	  std::cout<<camera.to_string()<<"\n";
	}
	// Now e can crate the immutable profile
	const auto profile=DProfile::discover(static_cast<int>(cameras.size()));

	// Then start ohdInterface, which discovers detected wifi cards and more.
	auto ohdInterface = std::make_unique<OHDInterface>(*profile);

	// then we can start telemetry, which uses OHDInterface for wfb tx/rx (udp)
	auto ohdTelemetry = std::make_unique<OHDTelemetry>(*platform,* profile);

	// and start ohdVideo if we are on the air pi
	std::unique_ptr<OHDVideo> ohdVideo;
	if (profile->is_air) {
	  ohdVideo = std::make_unique<OHDVideo>(*platform,*profile,cameras);
	}
    // we need to start QOpenHD when we are running as ground
    if(!profile->is_air){
      OHDUtil::run_command("systemctl",{" start qopenhd"});
    }else{
      OHDUtil::run_command("systemctl",{" stop qopenhd"});
    }
	std::cout << "All OpenHD modules running\n";

	// run forever, everything has its own threads. Note that the only way to break out basically
	// is when one of the modules encounters an exception.
	while (true) {
	  std::this_thread::sleep_for(std::chrono::seconds(2));
	  // To make sure this is all tightly packed together, we write it to a stringstream first
	  // and then to stdout in one big chunk. Otherwise, some other debug output might stand in between the OpenHD
	  // state debug chunk.
	  std::stringstream ss;
	  ss<< "---------------------------------OpenHD-state debug begin ---------------------------------\n";
	  ss<<ohdInterface->createDebug();
	  if(ohdVideo){
		ohdVideo->restartIfStopped();
		ss<<ohdVideo->createDebug();
	  }
	  ss << ohdTelemetry->createDebug();
	  ss<<"---------------------------------OpenHD-state debug   end ---------------------------------\n";
	  std::cout<<ss.str();
	}
  } catch (std::exception &ex) {
	std::cerr << "Error: " << ex.what() << std::endl;
	exit(1);
  } catch (...) {
	std::cerr << "Unknown exception occurred" << std::endl;
	exit(1);
  }
}
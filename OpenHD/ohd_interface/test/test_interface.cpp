#include <iostream>

#include "openhd-platform.hpp"
#include "openhd-profile.hpp"

#include "OHDInterface.h"

int main(int argc, char *argv[]) {

  const OHDProfile profile{false,"0"};
  OHDInterface ohdInterface(profile);

  std::cerr << "OHDInterface started\n";

  // run forever, OHDInterface runs in its own threads
  while (true) {
	std::this_thread::sleep_for(std::chrono::seconds(2));
	std::cout<<"XInterface\n";
	std::cout<<ohdInterface.createDebug();
  }
  std::cerr << "OHDInterface stopped\n";

  return 0;
}
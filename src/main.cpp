#include <string>
#include <vector>

#include "rocket_watcher/cli.hpp"

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  return rocket_watcher::runMain(args);
}

// interactiveMain.cpp - Interactive generator launcher.
#include "InteractiveGenerator.hpp"
#include "generators/common/run_generator.hpp"

int main(int argc, char* argv[]) {
    InteractiveGenerator gen;
    return run_generator(argc, argv, gen);
}

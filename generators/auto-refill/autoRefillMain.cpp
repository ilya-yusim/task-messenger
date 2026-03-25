// autoRefillMain.cpp - Auto-refill generator launcher.
#include "AutoRefillGenerator.hpp"
#include "generators/common/run_generator.hpp"

int main(int argc, char* argv[]) {
    AutoRefillGenerator gen;
    return run_generator(argc, argv, gen);
}

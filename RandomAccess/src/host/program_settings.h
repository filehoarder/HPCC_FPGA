
#ifndef SRC_HOST_PROGRAM_SETTINGS_H_
#define SRC_HOST_PROGRAM_SETTINGS_H_

#include "parameters.h"

/* C++ standard library headers */
#include <memory>

#include "CL/opencl.h"

/*
Short description of the program.
Moreover the version and build time is also compiled into the description.
*/

#define PROGRAM_DESCRIPTION "Implementation of the random access benchmark"\
                            " proposed in the HPCC benchmark suite for FPGA.\n"\
                            "Version: " VERSION "\n"


struct ProgramSettings {
    uint numRepetitions;
    uint numReplications;
    int defaultPlatform;
    int defaultDevice;
    size_t dataSize;
    std::string kernelFileName;
};


#endif

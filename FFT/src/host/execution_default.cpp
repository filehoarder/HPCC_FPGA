/*
Copyright (c) 2019 Marius Meyer

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* Related header files */
#include "execution.h"

/* C++ standard library headers */
#include <memory>
#include <vector>
#include <chrono>

/* External library headers */
#include "CL/cl.hpp"

/* Project's headers */

namespace bm_execution {

    /*
    Implementation for the single kernel.
     @copydoc bm_execution::calculate()
    */
    std::unique_ptr<fft::FFTExecutionTimings>
    calculate(hpcc_base::ExecutionSettings<fft::FFTProgramSettings> const&  config,
            std::complex<HOST_DATA_TYPE>* data,
            std::complex<HOST_DATA_TYPE>* data_out,
            unsigned iterations,
            bool inverse) {
        
        int err;

        std::vector<cl::Buffer> inBuffers;
        std::vector<cl::Buffer> outBuffers;
        std::vector<cl::Kernel> fetchKernels;
        std::vector<cl::Kernel> fftKernels;
        std::vector<cl::Kernel> storeKernels;
        std::vector<cl::CommandQueue> fetchQueues;
        std::vector<cl::CommandQueue> fftQueues;
        std::vector<cl::CommandQueue> storeQueues;

        unsigned iterations_per_kernel = iterations / config.programSettings->kernelReplications;

        for (int r=0; r < config.programSettings->kernelReplications; r++) {
                inBuffers.push_back(cl::Buffer(*config.context, CL_MEM_READ_ONLY, (1 << LOG_FFT_SIZE) * iterations_per_kernel * 2 * sizeof(HOST_DATA_TYPE), NULL, &err));
                ASSERT_CL(err)
                outBuffers.push_back(cl::Buffer(*config.context, CL_MEM_WRITE_ONLY, (1 << LOG_FFT_SIZE) * iterations_per_kernel * 2 * sizeof(HOST_DATA_TYPE), NULL, &err));
                ASSERT_CL(err)

        #ifdef INTEL_FPGA
                cl::Kernel fetchKernel(*config.program, (FETCH_KERNEL_NAME + std::to_string(r)).c_str(), &err);
                ASSERT_CL(err)
                cl::Kernel fftKernel(*config.program, (FFT_KERNEL_NAME + std::to_string(r)).c_str(), &err);
                ASSERT_CL(err)
        #ifdef USE_SVM
                err = clSetKernelArgSVMPointer(fetchKernel(), 0,
                                                reinterpret_cast<void*>(data));
                ASSERT_CL(err)
                err = clSetKernelArgSVMPointer(fftKernel(), 0,
                                                reinterpret_cast<void*>(data_out));
                ASSERT_CL(err)
        #else
                err = fetchKernel.setArg(0, inBuffers[r]);
                ASSERT_CL(err)
                err = fftKernel.setArg(0, outBuffers[r]);
                ASSERT_CL(err)
        #endif
                err = fftKernel.setArg(1, iterations_per_kernel);
                ASSERT_CL(err)
                err = fftKernel.setArg(2, static_cast<cl_int>(inverse));
                ASSERT_CL(err)
        #endif

        #ifdef XILINX_FPGA
                cl::Kernel fetchKernel(*config.program, (std::string(FETCH_KERNEL_NAME) + "0:{" + FETCH_KERNEL_NAME + "0_" + std::to_string(r + 1) + "}").c_str(), &err);
                ASSERT_CL(err)
                cl::Kernel fftKernel(*config.program, (std::string(FFT_KERNEL_NAME) + "0:{" + FFT_KERNEL_NAME + "0_" + std::to_string(r + 1) + "}").c_str(), &err);
                ASSERT_CL(err)
                cl::Kernel storeKernel(*config.program, (std::string(STORE_KERNEL_NAME) + "0:{" + STORE_KERNEL_NAME + "0_" + std::to_string(r + 1) + "}").c_str(), &err);
                ASSERT_CL(err)
                err = storeKernel.setArg(0, outBuffers[r]);
                ASSERT_CL(err)
                err = storeKernel.setArg(1, iterations_per_kernel);
                ASSERT_CL(err)

                err = fetchKernel.setArg(0, inBuffers[r]);
                ASSERT_CL(err)

                err = fftKernel.setArg(0, iterations_per_kernel);
                ASSERT_CL(err)
                err = fftKernel.setArg(1, static_cast<cl_int>(inverse));
                ASSERT_CL(err)

                storeQueues.push_back(cl::CommandQueue(*config.context, *config.device, 0, &err));
                ASSERT_CL(err)

                storeKernels.push_back(storeKernel);
        #endif

                err = fetchKernel.setArg(1, iterations_per_kernel);
                ASSERT_CL(err)

                fetchQueues.push_back(cl::CommandQueue(*config.context, *config.device, 0, &err));
                ASSERT_CL(err)
                fftQueues.push_back(cl::CommandQueue(*config.context, *config.device, 0, &err));
                ASSERT_CL(err)

                fetchKernels.push_back(fetchKernel);
                fftKernels.push_back(fftKernel);

#ifdef USE_SVM
                err = clEnqueueSVMMap(fetchQueue[r](), CL_TRUE,
                                CL_MAP_READ,
                                reinterpret_cast<void *>(&data[r * (1 << LOG_FFT_SIZE) * iterations_per_kernel]),
                                (1 << LOG_FFT_SIZE) * iterations_per_kernel * 2 * sizeof(HOST_DATA_TYPE), 0,
                                NULL, NULL);
                ASSERT_CL(err)
                err = clEnqueueSVMMap(fftQueues[r](), CL_TRUE,
                                CL_MAP_WRITE,
                                reinterpret_cast<void *>(&data_out[r * (1 << LOG_FFT_SIZE) * iterations_per_kernel]),
                                (1 << LOG_FFT_SIZE) * iterations_per_kernel * 2 * sizeof(HOST_DATA_TYPE), 0,
                                NULL, NULL);
                ASSERT_CL(err)
#else
                err = fetchQueues[r].enqueueWriteBuffer(inBuffers[r],CL_TRUE,0, (1 << LOG_FFT_SIZE) * iterations_per_kernel * 2 * sizeof(HOST_DATA_TYPE), &data[r * (1 << LOG_FFT_SIZE) * iterations_per_kernel]);
                ASSERT_CL(err)
#endif
        }

        std::vector<double> calculationTimings;
        for (uint r =0; r < config.programSettings->numRepetitions; r++) {
            auto startCalculation = std::chrono::high_resolution_clock::now();
            for (int r=0; r < config.programSettings->kernelReplications; r++) {
                fetchQueues[r].enqueueTask(fetchKernels[r]);
                fftQueues[r].enqueueTask(fftKernels[r]);
        #ifdef XILINX_FPGA
                storeQueues[r].enqueueTask(storeKernels[r]);
        #endif
            }
            for (int r=0; r < config.programSettings->kernelReplications; r++) {
                fetchQueues[r].finish();
                fftQueues[r].finish();
#ifdef XILINX_FPGA
                storeQueues[r].finish();
#endif
            }
            auto endCalculation = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> calculationTime =
                    std::chrono::duration_cast<std::chrono::duration<double>>
                            (endCalculation - startCalculation);
            calculationTimings.push_back(calculationTime.count());
        }
        for (int r=0; r < config.programSettings->kernelReplications; r++) {
#ifdef USE_SVM
                err = clEnqueueSVMUnmap(fetchQueues[r](),
                                        reinterpret_cast<void *>(&data[r * (1 << LOG_FFT_SIZE) * iterations_per_kernel]), 0,
                                        NULL, NULL);
                ASSERT_CL(err)
                err = clEnqueueSVMUnmap(fftQueues[r](),
                                        reinterpret_cast<void *>(&data_out[r * (1 << LOG_FFT_SIZE) * iterations_per_kernel]), 0,
                                        NULL, NULL);
                ASSERT_CL(err)
#else
                err = fetchQueues[r].enqueueReadBuffer(outBuffers[r],CL_TRUE,0, (1 << LOG_FFT_SIZE) * iterations_per_kernel * 2 * sizeof(HOST_DATA_TYPE), &data_out[r * (1 << LOG_FFT_SIZE) * iterations_per_kernel]);
                ASSERT_CL(err)
#endif
        }
        std::unique_ptr<fft::FFTExecutionTimings> result(new fft::FFTExecutionTimings{
                calculationTimings
        });
        return result;
    }

}  // namespace bm_execution

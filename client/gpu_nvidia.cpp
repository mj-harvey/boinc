// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2012 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

// Detection of NVIDIA GPUs

#ifdef _WIN32
#include "boinc_win.h"
/* get annotation macros from sal.h */
/* define the ones that don't exist */
#include "sal.h"
/* These are just an annotations.  They don't do anything */
#ifndef __success
#define __success(x)  
#endif
#ifndef __in
#define __in
#endif
#ifndef __out
#define __out
#endif
#ifndef __in_ecount
#define __in_ecount(x)
#endif
#ifndef __out_ecount
#define __out_ecount(x)
#endif
#ifndef __in_opt
#define __in_opt
#endif
#ifndef __out_opt
#define __out_opt
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __inout_opt
#define __inout_opt
#endif
#ifndef __inout_ecount
#define __inout_ecount(x)
#endif
#ifndef __inout_ecount_full
#define __inout_ecount_full(x)
#endif
#ifndef __inout_ecount_part_opt
#define __inout_ecount_part_opt(x,y)
#endif 
#ifndef __inout_ecount_full_opt
#define __inout_ecount_full_opt(x,y)
#endif 
#ifndef __out_ecount_full_opt
#define __out_ecount_full_opt(x)
#endif 

#include "nvapi.h"
#ifdef _MSC_VER
#define snprintf _snprintf
#endif
#else
#ifdef __APPLE__
// Suppress obsolete warning when building for OS 10.3.9
#define DLOPEN_NO_WARN
#include <mach-o/dyld.h>
#include <Carbon/Carbon.h>
#endif
#include "config.h"
#include <dlfcn.h>
#endif

#include <vector>
#include <string>

using std::vector;
using std::string;

#include "coproc.h"
#include "util.h"

#include "client_msgs.h"
#include "gpu_detect.h"

static void get_available_nvidia_ram(COPROC_NVIDIA &cc, vector<string>& warnings);

#if !(defined(_WIN32) || defined(__APPLE__))

static int nvidia_driver_version() {
    int (*nvml_init)()  = NULL;
    int (*nvml_finish)()  = NULL;
    int (*nvml_driver)(char *f, unsigned int len) = NULL;
    int dri_ver  = 0;
    void *handle = NULL;
    char driver_string[81];

    handle  = dlopen("libnvidia-ml.so", RTLD_NOW);
    if (!handle) goto end; 

    nvml_driver = (int(*)(char *, unsigned int)) dlsym(handle,  "nvmlSystemGetDriverVersion");
    nvml_init = (int(*)(void)) dlsym(handle,  "nvmlInit");
    nvml_finish = (int(*)(void)) dlsym(handle,  "nvmlShutdown");
    if (!nvml_driver || !nvml_init || !nvml_finish) goto end;

    if (nvml_init()) goto end;
    if (nvml_driver(driver_string, 80)) goto end;
    dri_ver = (int) (100. * atof(driver_string));

end:
    if (nvml_finish) nvml_finish();
    if (handle) dlclose(handle);
    return dri_ver;
}

#endif 

// return 1/-1/0 if device 1 is more/less/same capable than device 2.
// factors (decreasing priority):
// - compute capability
// - software version
// - available memory
// - speed
//
// If "loose", ignore FLOPS and tolerate small memory diff
//
int nvidia_compare(COPROC_NVIDIA& c1, COPROC_NVIDIA& c2, bool loose) {
    if (c1.prop.major > c2.prop.major) return 1;
    if (c1.prop.major < c2.prop.major) return -1;
    if (c1.prop.minor > c2.prop.minor) return 1;
    if (c1.prop.minor < c2.prop.minor) return -1;
    if (c1.cuda_version > c2.cuda_version) return 1;
    if (c1.cuda_version < c2.cuda_version) return -1;
    if (loose) {
        if (c1.available_ram> 1.4*c2.available_ram) return 1;
        if (c1.available_ram < .7* c2.available_ram) return -1;
        return 0;
    }
    if (c1.available_ram > c2.available_ram) return 1;
    if (c1.available_ram < c2.available_ram) return -1;
    double s1 = c1.peak_flops;
    double s2 = c2.peak_flops;
    if (s1 > s2) return 1;
    if (s1 < s2) return -1;
    return 0;
}

enum CUdevice_attribute_enum {
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 1,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X = 2,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y = 3,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z = 4,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X = 5,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y = 6,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z = 7,
    CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK = 8,
    CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY = 9,
    CU_DEVICE_ATTRIBUTE_WARP_SIZE = 10,
    CU_DEVICE_ATTRIBUTE_MAX_PITCH = 11,
    CU_DEVICE_ATTRIBUTE_REGISTERS_PER_BLOCK = 12,
    CU_DEVICE_ATTRIBUTE_CLOCK_RATE = 13,
    CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT = 14,
    CU_DEVICE_ATTRIBUTE_GPU_OVERLAP = 15,
    CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 16,
    CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT = 17,
    CU_DEVICE_ATTRIBUTE_INTEGRATED = 18,
    CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY = 19,
    CU_DEVICE_ATTRIBUTE_PCI_BUS_ID = 33,
    CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID = 34,
    CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID = 50
};

#ifdef _WIN32
typedef int (__stdcall *CUDA_GDC)(int *count);
typedef int (__stdcall *CUDA_GDV)(int* version);
typedef int (__stdcall *CUDA_GDI)(unsigned int);
typedef int (__stdcall *CUDA_GDG)(int*, int);
typedef int (__stdcall *CUDA_GDA)(int*, int, int);
typedef int (__stdcall *CUDA_GDN)(char*, int, int);
typedef int (__stdcall *CUDA_GDM)(size_t*, int);
typedef int (__stdcall *CUDA_GDCC)(int*, int*, int);
typedef int (__stdcall *CUDA_CC)(void**, unsigned int, unsigned int);
typedef int (__stdcall *CUDA_CD)(void*);
typedef int (__stdcall *CUDA_MA)(unsigned int*, size_t);
typedef int (__stdcall *CUDA_MF)(unsigned int);
typedef int (__stdcall *CUDA_MGI)(size_t*, size_t*);

CUDA_GDC __cuDeviceGetCount = NULL;
CUDA_GDV __cuDriverGetVersion = NULL;
CUDA_GDI __cuInit = NULL;
CUDA_GDG __cuDeviceGet = NULL;
CUDA_GDA __cuDeviceGetAttribute = NULL;
CUDA_GDN __cuDeviceGetName = NULL;
CUDA_GDM __cuDeviceTotalMem = NULL;
CUDA_GDCC __cuDeviceComputeCapability = NULL;
CUDA_CC __cuCtxCreate = NULL;
CUDA_CD __cuCtxDestroy = NULL;
CUDA_MA __cuMemAlloc = NULL;
CUDA_MF __cuMemFree = NULL;
CUDA_MGI __cuMemGetInfo = NULL;
#else
void* cudalib;
int (*__cuInit)(unsigned int);
int (*__cuDeviceGetCount)(int*);
int (*__cuDriverGetVersion)(int*);
int (*__cuDeviceGet)(int*, int);
int (*__cuDeviceGetAttribute)(int*, int, int);
int (*__cuDeviceGetName)(char*, int, int);
int (*__cuDeviceTotalMem)(size_t*, int);
int (*__cuDeviceComputeCapability)(int*, int*, int);
int (*__cuCtxCreate)(void**, unsigned int, unsigned int);
int (*__cuCtxDestroy)(void*);
int (*__cuMemAlloc)(unsigned int*, size_t);
int (*__cuMemFree)(unsigned int);
int (*__cuMemGetInfo)(size_t*, size_t*);
#endif

// NVIDIA interfaces are documented here:
// http://developer.download.nvidia.com/compute/cuda/2_3/toolkit/docs/online/index.html

void COPROC_NVIDIA::get(
    vector<string>& warnings
) {
    int cuda_ndevs, retval;
    char buf[256];

#ifdef _WIN32
    HMODULE cudalib = LoadLibrary("nvcuda.dll");
    if (!cudalib) {
        warnings.push_back("No NVIDIA library found");
        return;
    }
    __cuDeviceGetCount = (CUDA_GDC)GetProcAddress( cudalib, "cuDeviceGetCount" );
    __cuDriverGetVersion = (CUDA_GDV)GetProcAddress( cudalib, "cuDriverGetVersion" );
    __cuInit = (CUDA_GDI)GetProcAddress( cudalib, "cuInit" );
    __cuDeviceGet = (CUDA_GDG)GetProcAddress( cudalib, "cuDeviceGet" );
    __cuDeviceGetAttribute = (CUDA_GDA)GetProcAddress( cudalib, "cuDeviceGetAttribute" );
    __cuDeviceGetName = (CUDA_GDN)GetProcAddress( cudalib, "cuDeviceGetName" );
    __cuDeviceTotalMem = (CUDA_GDM)GetProcAddress( cudalib, "cuDeviceTotalMem" );
    __cuDeviceComputeCapability = (CUDA_GDCC)GetProcAddress( cudalib, "cuDeviceComputeCapability" );
    __cuCtxCreate = (CUDA_CC)GetProcAddress( cudalib, "cuCtxCreate" );
    __cuCtxDestroy = (CUDA_CD)GetProcAddress( cudalib, "cuCtxDestroy" );
    __cuMemAlloc = (CUDA_MA)GetProcAddress( cudalib, "cuMemAlloc" );
    __cuMemFree = (CUDA_MF)GetProcAddress( cudalib, "cuMemFree" );
    __cuMemGetInfo = (CUDA_MGI)GetProcAddress( cudalib, "cuMemGetInfo" );

#ifndef SIM
    NvAPI_Initialize();
    NvAPI_ShortString ss;
    NvU32 Version = 0;
    NvAPI_SYS_GetDriverAndBranchVersion(&Version, ss);

#if 0
    // NvAPI now provides an API for getting #cores :-)
    // But not FLOPs per clock cycle :-(
    // Anyway, don't use this for now because server code estimates FLOPS
    // based on compute capability, so we may as well do the same
    // See http://docs.nvidia.com/gameworks/content/gameworkslibrary/coresdk/nvapi/
    //
    NvPhysicalGpuHandle GPUHandle[NVAPI_MAX_PHYSICAL_GPUS];
    NvU32 GpuCount, nc;
    NvAPI_EnumPhysicalGPUs(GPUHandle, &GpuCount);
    for (unsigned int i=0; i<GpuCount; i++) {
        NvAPI_GPU_GetGpuCoreCount(GPUHandle[i], &nc);
    }
#endif
#endif
#else

#ifdef __APPLE__
    cudalib = dlopen("/usr/local/cuda/lib/libcuda.dylib", RTLD_NOW);
#else
    cudalib = dlopen("libcuda.so", RTLD_NOW);
#endif
    if (!cudalib) {
        warnings.push_back("No NVIDIA library found");
        return;
    }
    __cuDeviceGetCount = (int(*)(int*)) dlsym(cudalib, "cuDeviceGetCount");
    __cuDriverGetVersion = (int(*)(int*)) dlsym( cudalib, "cuDriverGetVersion" );
    __cuInit = (int(*)(unsigned int)) dlsym( cudalib, "cuInit" );
    __cuDeviceGet = (int(*)(int*, int)) dlsym( cudalib, "cuDeviceGet" );
    __cuDeviceGetAttribute = (int(*)(int*, int, int)) dlsym( cudalib, "cuDeviceGetAttribute" );
    __cuDeviceGetName = (int(*)(char*, int, int)) dlsym( cudalib, "cuDeviceGetName" );
    __cuDeviceTotalMem = (int(*)(size_t*, int)) dlsym( cudalib, "cuDeviceTotalMem" );
    __cuDeviceComputeCapability = (int(*)(int*, int*, int)) dlsym( cudalib, "cuDeviceComputeCapability" );
    __cuCtxCreate = (int(*)(void**, unsigned int, unsigned int)) dlsym( cudalib, "cuCtxCreate" );
    __cuCtxDestroy = (int(*)(void*)) dlsym( cudalib, "cuCtxDestroy" );
    __cuMemAlloc = (int(*)(unsigned int*, size_t)) dlsym( cudalib, "cuMemAlloc" );
    __cuMemFree = (int(*)(unsigned int)) dlsym( cudalib, "cuMemFree" );
    __cuMemGetInfo = (int(*)(size_t*, size_t*)) dlsym( cudalib, "cuMemGetInfo" );
    dlclose(cudalib);
#endif

    if (!__cuDriverGetVersion) {
        warnings.push_back("cuDriverGetVersion() missing from NVIDIA library");
        return;
    }
    if (!__cuInit) {
        warnings.push_back("cuInit() missing from NVIDIA library");
        return;
    }
    if (!__cuDeviceGetCount) {
        warnings.push_back("cuDeviceGetCount() missing from NVIDIA library");
        return;
    }
    if (!__cuDeviceGet) {
        warnings.push_back("cuDeviceGet() missing from NVIDIA library");
        return;
    }
    if (!__cuDeviceGetAttribute) {
        warnings.push_back("cuDeviceGetAttribute() missing from NVIDIA library");
        return;
    }
    if (!__cuDeviceTotalMem) {
        warnings.push_back("cuDeviceTotalMem() missing from NVIDIA library");
        return;
    }
    if (!__cuDeviceComputeCapability) {
        warnings.push_back("cuDeviceComputeCapability() missing from NVIDIA library");
        return;
    }
    if (!__cuMemAlloc) {
        warnings.push_back("cuMemAlloc() missing from NVIDIA library");
        return;
    }
    if (!__cuMemFree) {
        warnings.push_back("cuMemFree() missing from NVIDIA library");
        return;
    }

#ifdef __APPLE__
    // If system is just booting, CUDA driver may not be ready yet
    for (int retryCount=0; retryCount<45; retryCount++) {
#endif
        retval = (*__cuInit)(0);
#ifdef __APPLE__
        if (!retval) break;
        if (TickCount() > (120*60)) break;   // Don't retry if system has been up for over 2 minutes
        boinc_sleep(1.);
        continue;
    }
#endif
    
    if (retval) {
        sprintf(buf, "NVIDIA drivers present but no GPUs found");
        warnings.push_back(buf);
        return;
    }

    retval = (*__cuDriverGetVersion)(&cuda_version);
    if (retval) {
        sprintf(buf, "cuDriverGetVersion() returned %d", retval);
        warnings.push_back(buf);
        return;
    }

    have_cuda = true;

    retval = (*__cuDeviceGetCount)(&cuda_ndevs);
    if (retval) {
        sprintf(buf, "cuDeviceGetCount() returned %d", retval);
        warnings.push_back(buf);
        return;
    }
    sprintf(buf, "NVIDIA library reports %d GPU%s", cuda_ndevs, (cuda_ndevs==1)?"":"s");
    warnings.push_back(buf);

    int j, itemp;
    size_t global_mem = 0;
    COPROC_NVIDIA cc;
    string s;
    for (j=0; j<cuda_ndevs; j++) {
        memset(&cc.prop, 0, sizeof(cc.prop));
        CUdevice device;
        retval = (*__cuDeviceGet)(&device, j);
        if (retval) {
            sprintf(buf, "cuDeviceGet(%d) returned %d", j, retval);
            warnings.push_back(buf);
            return;
        }
        (*__cuDeviceGetName)(cc.prop.name, 256, device);
        if (retval) {
            sprintf(buf, "cuDeviceGetName(%d) returned %d", j, retval);
            warnings.push_back(buf);
            return;
        }
        (*__cuDeviceComputeCapability)(&cc.prop.major, &cc.prop.minor, device);
        (*__cuDeviceTotalMem)(&global_mem, device);
        cc.prop.totalGlobalMem = (double) global_mem;
        (*__cuDeviceGetAttribute)(&itemp, CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK, device);
        cc.prop.sharedMemPerBlock = (double) itemp;
        (*__cuDeviceGetAttribute)(&cc.prop.regsPerBlock, CU_DEVICE_ATTRIBUTE_REGISTERS_PER_BLOCK, device);
        (*__cuDeviceGetAttribute)(&cc.prop.warpSize, CU_DEVICE_ATTRIBUTE_WARP_SIZE, device);
        (*__cuDeviceGetAttribute)(&itemp, CU_DEVICE_ATTRIBUTE_MAX_PITCH, device);
        cc.prop.memPitch = (double) itemp;
        retval = (*__cuDeviceGetAttribute)(&cc.prop.maxThreadsPerBlock, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK, device);
        retval = (*__cuDeviceGetAttribute)(&cc.prop.maxThreadsDim[0], CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X, device);
        (*__cuDeviceGetAttribute)(&cc.prop.maxThreadsDim[1], CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y, device);
        (*__cuDeviceGetAttribute)(&cc.prop.maxThreadsDim[2], CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z, device);
        (*__cuDeviceGetAttribute)(&cc.prop.maxGridSize[0], CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X, device);
        (*__cuDeviceGetAttribute)(&cc.prop.maxGridSize[1], CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y, device);
        (*__cuDeviceGetAttribute)(&cc.prop.maxGridSize[2], CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z, device);
        (*__cuDeviceGetAttribute)(&cc.prop.clockRate, CU_DEVICE_ATTRIBUTE_CLOCK_RATE, device);
        (*__cuDeviceGetAttribute)(&itemp, CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY, device);
        cc.prop.totalConstMem = (double) itemp;
        (*__cuDeviceGetAttribute)(&itemp, CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT, device);
        cc.prop.textureAlignment = (double) itemp;
        (*__cuDeviceGetAttribute)(&cc.prop.deviceOverlap, CU_DEVICE_ATTRIBUTE_GPU_OVERLAP, device);
        (*__cuDeviceGetAttribute)(&cc.prop.multiProcessorCount, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device);
        (*__cuDeviceGetAttribute)(&cc.pci_info.bus_id, CU_DEVICE_ATTRIBUTE_PCI_BUS_ID, device);
        (*__cuDeviceGetAttribute)(&cc.pci_info.device_id, CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID, device);
        (*__cuDeviceGetAttribute)(&cc.pci_info.domain_id, CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID, device);
        if (cc.prop.major <= 0) continue;  // major == 0 means emulation
        if (cc.prop.major > 100) continue;  // e.g. 9999 is an error
#if defined(_WIN32) && !defined(SIM)
        cc.display_driver_version = Version;
#elif defined(__APPLE__)
        cc.display_driver_version = NSVersionOfRunTimeLibrary("cuda");
#else
        cc.display_driver_version = nvidia_driver_version();
#endif
        cc.have_cuda = true;
        cc.cuda_version = cuda_version;
        cc.device_num = j;
        cc.set_peak_flops();
        get_available_nvidia_ram(cc, warnings);
        nvidia_gpus.push_back(cc);
    }
    if (!nvidia_gpus.size()) {
        warnings.push_back("No CUDA-capable NVIDIA GPUs found");
    }
}


void COPROC_NVIDIA::correlate(
    bool use_all,    // if false, use only those equivalent to most capable
    vector<int>& ignore_devs
) {
    unsigned int i;

    if (!nvidia_gpus.size()) return;
    
    // identify the most capable non-ignored instance
    //
    bool first = true;
    for (i=0; i<nvidia_gpus.size(); i++) {
        if (in_vector(nvidia_gpus[i].device_num, ignore_devs)) continue;
#ifdef __APPLE__
        if ((nvidia_gpus[i].cuda_version >= 6050) && nvidia_gpus[i].prop.major < 2) {
            // Can't use GPUs with compute capability < 2 with CUDA drivers >= 6.5.x
            nvidia_gpus[i].is_used = COPROC_UNUSED;
            continue;
        }
#endif
        if (first) {
            *this = nvidia_gpus[i];
            first = false;
        } else if (nvidia_compare(nvidia_gpus[i], *this, false) > 0) {
            *this = nvidia_gpus[i];
        }
    }

    // see which other instances are equivalent,
    // and set "count", "device_nums", and "pci_infos"
    //
    count = 0;
    for (i=0; i<nvidia_gpus.size(); i++) {
        if (in_vector(nvidia_gpus[i].device_num, ignore_devs)) {
            nvidia_gpus[i].is_used = COPROC_IGNORED;
        } else if (this->have_opencl && !nvidia_gpus[i].have_opencl) {
            nvidia_gpus[i].is_used = COPROC_UNUSED;
        } else if (this->have_cuda && !nvidia_gpus[i].have_cuda) {
            nvidia_gpus[i].is_used = COPROC_UNUSED;
#ifdef __APPLE__
        } else if (nvidia_gpus[i].is_used == COPROC_UNUSED) {
            // Can't use GPUs with compute capability < 2 with CUDA drivers >= 6.5.x
            continue;
#endif
        } else if (use_all || !nvidia_compare(nvidia_gpus[i], *this, true)) {
            device_nums[count] = nvidia_gpus[i].device_num;
            pci_infos[count] = nvidia_gpus[i].pci_info;
            count++;
            nvidia_gpus[i].is_used = COPROC_USED;
        } else {
            nvidia_gpus[i].is_used = COPROC_UNUSED;
        }
    }
}

// See how much RAM is available on this GPU.
//
// CAUTION: as currently written, this method should be
// called only from COPROC_NVIDIA::get().  If in the 
// future you wish to call it from additional places:
// * It must be called from a separate child process on
//   dual-GPU laptops (e.g., Macbook Pros) with the results
//   communicated to the main client process via IPC or a
//   temp file.  See the comments about dual-GPU laptops 
//   in gpu_detect.cpp and main.cpp for more details.
// * The CUDA library must be loaded and cuInit() called 
//   first.
// * See client/coproc_detect.cpp and cpu_sched.cpp in
//   BOINC 6.12.36 for an earlier attempt to call this
//   from the scheduler.  Note that it was abandoned
//   due to repeated calls crashing the driver.
//
static void get_available_nvidia_ram(COPROC_NVIDIA &cc, vector<string>& warnings) {
    int retval;
    size_t memfree = 0, memtotal = 0;
    int device;
    void* ctx;
    char buf[256];
    
    cc.available_ram = cc.prop.totalGlobalMem;
    if (!__cuDeviceGet) {
        warnings.push_back("cuDeviceGet() missing from NVIDIA library");
        return;
    }
    if (!__cuCtxCreate) {
        warnings.push_back("cuCtxCreate() missing from NVIDIA library");
        return;
    }
    if (!__cuCtxDestroy) {
        warnings.push_back("cuCtxDestroy() missing from NVIDIA library");
        return;
    }
    if (!__cuMemGetInfo) {
        warnings.push_back("cuMemGetInfo() missing from NVIDIA library");
        return;
    }

    retval = (*__cuDeviceGet)(&device, cc.device_num);
    if (retval) {
        snprintf(buf, sizeof(buf),
            "[coproc] cuDeviceGet(%d) returned %d", cc.device_num, retval
        );
        warnings.push_back(buf);
        return;
    }
    retval = (*__cuCtxCreate)(&ctx, 0, device);
    if (retval) {
        snprintf(buf, sizeof(buf),
            "[coproc] cuCtxCreate(%d) returned %d", cc.device_num, retval
        );
        warnings.push_back(buf);
        return;
    }
    retval = (*__cuMemGetInfo)(&memfree, &memtotal);
    if (retval) {
        snprintf(buf, sizeof(buf),
            "[coproc] cuMemGetInfo(%d) returned %d", cc.device_num, retval
        );
        warnings.push_back(buf);
        (*__cuCtxDestroy)(ctx);
        return;
    }
    (*__cuCtxDestroy)(ctx);
    cc.available_ram = (double) memfree;
}

// check whether each GPU is running a graphics app (assume yes)
// return true if there's been a change since last time
//
// CAUTION: this method is not currently used.  If you wish
// to call it in the future:
// * It must be called from a separate child process on
//   dual-GPU laptops (e.g., Macbook Pros) with the results
//   communicated to the main client process via IPC or a
//   temp file.  See the comments about dual-GPU laptops 
//   in gpu_detect.cpp and main.cpp for more details.
// * The CUDA library must be loaded and cuInit() called 
//   first.
//
#if 0
bool COPROC_NVIDIA::check_running_graphics_app() {
    int retval, j;
    bool change = false;
    if (!__cuDeviceGet) {
        warnings.push_back("cuDeviceGet() missing from NVIDIA library");
        return;
    }
    if (!__cuDeviceGetAttribute) {
        warnings.push_back("cuDeviceGetAttribute() missing from NVIDIA library");
        return;
    }

    for (j=0; j<count; j++) {
        bool new_val = true;
        int device, kernel_timeout;
        retval = (*__cuDeviceGet)(&device, j);
        if (!retval) {
            retval = (*__cuDeviceGetAttribute)(&kernel_timeout, CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT, device);
            if (!retval && !kernel_timeout) {
                new_val = false;
            }
        }
        if (new_val != running_graphics_app[j]) {
            change = true;
        }
        running_graphics_app[j] = new_val;
    }
    return change;
}
#endif

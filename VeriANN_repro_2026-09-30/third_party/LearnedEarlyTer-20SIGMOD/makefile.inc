# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

CXX          = g++ -std=c++11
CXXCPP       = g++ -std=c++11 -E
CPPFLAGS     = -DFINTEGER=int -I/home/dwang12576/.conda/envs/torch_env/include -fopenmp 
CXXFLAGS     = -fPIC -m64 -Wno-sign-compare -g -O3 -Wall -Wextra
CPUFLAGS     = -msse4 -mpopcnt
LDFLAGS      = -fopenmp -L/home/dwang12576/.conda/envs/torch_env/lib 
LIBS         = -lopenblas   
PYTHONCFLAGS =   -I

NVCC         = 
CUDA_ROOT    = 
CUDA_ARCH    = default
NVCCFLAGS    = -I $(CUDA_ROOT)/targets/x86_64-linux/include/ \
-Xcompiler -fPIC \
-Xcudafe --diag_suppress=unrecognized_attribute \
$(CUDA_ARCH) \
-lineinfo \
-ccbin $(CXX) -DFAISS_USE_FLOAT16

OS = $(shell uname -s)

SHAREDEXT   = so
SHAREDFLAGS = -shared

ifeq ($(OS),Darwin)
	SHAREDEXT   = dylib
	SHAREDFLAGS = -dynamiclib -undefined dynamic_lookup
endif

MKDIR_P      = /usr/bin/mkdir -p
PYTHON       = 
SWIG         = 
AR          ?= ar

prefix      ?= /usr/local
exec_prefix ?= ${prefix}
libdir       = ${exec_prefix}/lib
includedir   = ${prefix}/include

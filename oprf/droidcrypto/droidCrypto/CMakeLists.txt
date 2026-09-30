cmake_minimum_required(VERSION 3.10)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(GSL)

# RELIC configuration
# set RELIC to build Prime EC
set(DOCUM OFF CACHE BOOL "Build RELIC documentation") 
set(WITH "BN;DV;FP;EP;MD" CACHE STRING "RELIC features") 
set(FP_PRIME "256" CACHE STRING "The prime modulus size in bits.")
set(MULTI "PTHREAD" CACHE STRING "Multithreading API.")
if(APPLE)
set(OPSYS "MACOSX" CACHE STRING "Operating system." FORCE)
else()
set(OPSYS "LINUX" CACHE STRING "Operating system." FORCE)
endif()
set(TESTS "0" CACHE STRING "Build and run tests INTEGER times.")
set(BENCH "0" CACHE STRING "Build and run benchmarks n*n times.")
SET(SHLIB OFF CACHE BOOL "Build Shared Library")
SET(STLIB ON CACHE BOOL "Build Static Library")
add_subdirectory(relic)
# RELIC configuration end

add_subdirectory(keccak)
add_subdirectory(ot/SimplestOT)


set(SRCS
  AES.cpp
  PRNG.cpp
  Defines.cpp
  RCurve.cpp
  SHA1.cpp
  utils/Utils.cpp
  utils/Log.cpp
  utils/LinearCode.cpp
  BitVector.cpp
  ot/NaorPinkas.cpp
  ot/SimplestOT.cpp
  ot/VerifiedSimplestOT.cpp
  ot/TwoChooseOne/IknpOtExtSender.cpp
  ot/TwoChooseOne/IknpOtExtReceiver.cpp
  ot/TwoChooseOne/IknpDotExtSender.cpp
  ot/TwoChooseOne/IknpDotExtReceiver.cpp
  ot/TwoChooseOne/KosOtExtReceiver.cpp
  ot/TwoChooseOne/KosOtExtSender.cpp
  ot/TwoChooseOne/KosDotExtReceiver.cpp
  ot/TwoChooseOne/KosDotExtSender.cpp
  ChannelWrapper.cpp
  SecureRandom.cpp
  gc/WireLabel.cpp
  gc/HalfGate.cpp
  gc/circuits/Circuit.cpp
  gc/circuits/TestCircuit.cpp
  gc/circuits/AESCircuit.cpp
  psi/tools/ECNRPRF.cpp
  psi/ECNRPSIClient.cpp
  psi/OPRFAESPSIClient.cpp
  SHAKE128.cpp
  psi/oprf/oprf.h
  psi/oprf/oprf.cpp
  )


set(SRCS ${SRCS}
  psi/ECNRPSIServer.cpp
  psi/OPRFAESPSIServer.cpp
  )

add_library(droidcrypto SHARED ${SRCS})

set(DROIDCRYPTO_CPU_FLAGS "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
  set(DROIDCRYPTO_CPU_FLAGS "-maes -msse2 -msse4.1 -mpclmul")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(DROIDCRYPTO_CPU_FLAGS "-DHAVE_NEON")
endif()

set(CMAKE_C_FLAGS "-ffunction-sections -Wall ${DROIDCRYPTO_CPU_FLAGS} -Wfatal-errors -fPIC -Wno-ignored-attributes")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14")

SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO " -O2 -g -ggdb -rdynamic")
SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb -rdynamic")

target_include_directories(droidcrypto PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../")
get_target_property(RELIC_INCLUDES relic_s INCLUDE_DIRECTORIES)
target_include_directories(droidcrypto PUBLIC "${RELIC_INCLUDES}")

target_link_libraries(droidcrypto
  SimplestOT
  GSL
  relic_s 
  #grpc++
  )

add_subdirectory(psi/oprf)

target_link_libraries(droidcrypto keccak)
add_subdirectory(tests)

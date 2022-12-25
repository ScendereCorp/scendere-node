#!/usr/bin/env bash

set -o xtrace

DISTRO_CFG=""
if [[ "$OSTYPE" == "linux-gnu" ]]; then
    CPACK_TYPE="TBZ2"
    distro=$(lsb_release -i -c -s | tr '\n' '_')
    DISTRO_CFG="-DSCENDERE_DISTRO_NAME=${distro}"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    CPACK_TYPE="DragNDrop"
elif [[ "$OSTYPE" == "cygwin" ]]; then
    CPACK_TYPE="TBZ2"
elif [[ "$OSTYPE" == "msys" ]]; then
    CPACK_TYPE="NSIS" #?
elif [[ "$OSTYPE" == "win32" ]]; then
    CPACK_TYPE="NSIS"
elif [[ "$OSTYPE" == "freebsd"* ]]; then
    CPACK_TYPE="TBZ2"
    DISTRO_CFG="-DSCENDERE_DISTRO_NAME='freebsd'"
else
    CPACK_TYPE="TBZ2"
fi

if [[ ${SIMD} -eq 1 ]]; then
    SIMD_CFG="-DSCENDERE_SIMD_OPTIMIZATIONS=ON"
    echo SIMD and other optimizations enabled
    echo local CPU:
    cat /proc/cpuinfo # TBD for macOS
else
    SIMD_CFG=""
fi

if [[ ${ASAN_INT} -eq 1 ]]; then
    SANITIZERS="-DSCENDERE_ASAN_INT=ON"
elif [[ ${ASAN} -eq 1 ]]; then
    SANITIZERS="-DSCENDERE_ASAN=ON"
elif [[ ${TSAN} -eq 1 ]]; then
    SANITIZERS="-DSCENDERE_TSAN=ON"
else
    SANITIZERS=""
fi

if [[ "${BOOST_ROOT}" -ne "" ]]; then
    BOOST_CFG="-DBOOST_ROOT='${BOOST_ROOT}'"
else
    BOOST_CFG=""
fi

BUSYBOX_BASH=${BUSYBOX_BASH-0}
if [[ ${FLAVOR-_} == "_" ]]; then
    FLAVOR=""
fi

if [[ "${BETA}" -eq 1 ]]; then
    NETWORK_CFG="-DACTIVE_NETWORK=scendere_beta_network"
    CONFIGURATION="RelWithDebInfo"
else
    NETWORK_CFG="-DACTIVE_NETWORK=scendere_live_network"
    CONFIGURATION="Release"
fi

set -o nounset

run_build() {
    build_dir=build_${FLAVOR}

    mkdir ${build_dir}
    cd ${build_dir}
    cmake -GNinja \
    -DSCENDERE_GUI=ON \
    -DCMAKE_BUILD_TYPE=${CONFIGURATION} \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DCMAKE_INSTALL_PREFIX="../install" \
    ${NETWORK_CFG} \
    ${DISTRO_CFG} \
    ${SIMD_CFG} \
    -DBOOST_ROOT=/usr/local/boost \
    ${BOOST_CFG} \
    ${SANITIZERS} \
    ..

    cmake --build ${PWD} -- -v
    cmake --build ${PWD} -- install -v
    cpack -G ${CPACK_TYPE} -C ${CONFIGURATION} ${PWD}
    sha1sum *.tar* >SHA1SUMS
}

run_build

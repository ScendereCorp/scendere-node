#!/bin/bash

qt_dir=${1}

set -o errexit
set -o nounset
set -o xtrace
OS=$(uname)

source "$(dirname "$BASH_SOURCE")/impl/code-inspector.sh"
code_inspect "${ROOTPATH:-.}"

mkdir build
pushd build

if [[ "${RELEASE:-false}" == "true" ]]; then
    BUILD_TYPE="RelWithDebInfo"
fi

if [[ ${ASAN_INT:-0} -eq 1 ]]; then
    SANITIZERS="-DSCENDERE_ASAN_INT=ON"
elif [[ ${ASAN:-0} -eq 1 ]]; then
    SANITIZERS="-DSCENDERE_ASAN=ON"
elif [[ ${TSAN:-0} -eq 1 ]]; then
    SANITIZERS="-DSCENDERE_TSAN=ON"
elif [[ ${LCOV:-0} -eq 1 ]]; then
    SANITIZERS="-DCOVERAGE=ON"
fi

ulimit -S -n 8192

if [[ "$OS" == 'Linux' ]]; then
    if clang --version && [ ${LCOV:-0} == 0 ]; then
        BACKTRACE="-DSCENDERE_STACKTRACE_BACKTRACE=ON \
        -DBACKTRACE_INCLUDE=</tmp/backtrace.h>"
    else
        BACKTRACE="-DSCENDERE_STACKTRACE_BACKTRACE=ON"
    fi
else
    BACKTRACE=""
fi

cmake \
-G'Unix Makefiles' \
-DACTIVE_NETWORK=scendere_dev_network \
-DSCENDERE_TEST=ON \
-DSCENDERE_GUI=ON \
-DPORTABLE=1 \
-DSCENDERE_WARN_TO_ERR=ON \
-DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Debug} \
-DCMAKE_VERBOSE_MAKEFILE=ON \
-DBOOST_ROOT=${BOOST_ROOT:-/tmp/boost/} \
-DSCENDERE_SHARED_BOOST=ON \
-DQt5_DIR=${qt_dir} \
-DCI_TEST="1" \
${BACKTRACE:-} \
${SANITIZERS:-} \
..

if [[ "$OS" == 'Linux' ]]; then
    if [[ ${LCOV:-0} == 1 ]]; then
        cmake --build ${PWD} --target generate_coverage -- -j2
    else
        cmake --build ${PWD} --target build_tests -k -- -j2
    fi
else
    sudo cmake --build ${PWD} --target build_tests -- -j2
fi

popd

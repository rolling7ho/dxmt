#!/bin/sh

UNIX_INSTALL_DIR=$2
NAME=${3:-winemetal}
INSTALL_DIR="${DESTDIR%%/}${MESON_INSTALL_PREFIX}/${UNIX_INSTALL_DIR}"

mkdir -p "${INSTALL_DIR}"
cp "${MESON_BUILD_ROOT}/src/winemetal/unix/${NAME}.so" "${INSTALL_DIR}/${NAME}.so"


if [ $1 == "true" ]; then
    strip "${INSTALL_DIR}/${NAME}.so" -x
fi

#!/bin/bash

CC=gcc
PROTO_PREFIX=/usr/share/wayland-protocols
WLR_PROTO_PREFIX=/home/user/dev/labwc/subprojects/wlr-protocols

sources=(
	src/client.c
	src/backends/drm.c
	src/interfaces/ext_capture.c
	src/interfaces/ext_foreign_toplevel_list.c
	src/interfaces/wl_seat.c
	src/interfaces/wl_shm.c
	src/interfaces/wl_surface.c
	src/interfaces/wlr_layershell.c
	src/interfaces/xdg_shell.c
	src/renderers/simple.c
)

protocols=(
	$PROTO_PREFIX/stable/xdg-shell/xdg-shell.xml
	$PROTO_PREFIX/staging/cursor-shape/cursor-shape-v1.xml
	$PROTO_PREFIX/unstable/tablet/tablet-unstable-v2.xml # required by cursor-shape
	$PROTO_PREFIX/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	$PROTO_PREFIX/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
	$PROTO_PREFIX/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
	$PROTO_PREFIX/staging/ext-image-capture-source/ext-image-capture-source-v1.xml
	$WLR_PROTO_PREFIX/unstable/wlr-layer-shell-unstable-v1.xml
)

binaries=(
	window
	window_custom
	window_animate
	window_pointer
	toplevel_list
	toplevel_capture
	panel
	panel_animate
	drm
)

includes=(
	-I include/
	-I /usr/include/drm/
	-I build/protocols/
)

libs=(
	-l wayland-client
	-l drm
	-l gbm
)

opts=(
	#-std=c11
	#-fsanitize=address,undefined
	#-O2
)

if ! test -e "$WLR_PROTO_PREFIX"; then
	echo "wlr-protocols not found, please adjust build.sh"
	exit 1;
fi

mkdir -p build/protocols

# FIXME: replace the .xml part
for proto in "${protocols[@]}"; do
	name=$(basename "$proto")
	test -e build/protocols/"$name".h && continue;
	wayland-scanner client-header "$proto" build/protocols/"$name".h
	wayland-scanner private-code "$proto" build/protocols/"$name".c
done

for binary in "${binaries[@]}"; do
	printf "build/%s.. " "$binary"
	if $CC -Wall -g                \
		"${sources[@]}"        \
		src/examples/$binary.c \
		build/protocols/*.c    \
		"${includes[@]}"       \
		"${libs[@]}"           \
		"${opts[@]}"           \
		-o build/$binary       \
		"$@"
	then
		echo "done";
	else
		echo "failed";
		exit 1;
	fi
done

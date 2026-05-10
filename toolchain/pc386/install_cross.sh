#!/usr/bin/env bash
# toolchain/pc386/install_cross.sh — bootstrap the i686-elf cross toolchain.
#
# Detects whether i686-elf-gcc is already on PATH, and if not, installs
# it. Strategy:
#
#   1. Already present → exit 0.
#   2. macOS + homebrew → install from nativeos/i686-elf-toolchain tap.
#   3. Linux + apt → install gcc-multilib + suggest building from source
#      (most distros don't ship a packaged i686-elf cross toolchain).
#   4. Fallback: build binutils + gcc from source under
#      ~/.local/i686-elf-toolchain. Slow (~20 min) but works anywhere
#      with build-essential.
#
# Idempotent: re-running is a no-op if the tools are already on PATH.

set -euo pipefail

REQUIRED_TOOLS=(i686-elf-gcc i686-elf-ld i686-elf-as i686-elf-objcopy)

check_present() {
    for t in "${REQUIRED_TOOLS[@]}"; do
        if ! command -v "$t" >/dev/null 2>&1; then
            return 1
        fi
    done
    return 0
}

install_macos_brew() {
    if ! command -v brew >/dev/null 2>&1; then
        echo "homebrew not installed; skipping brew path" >&2
        return 1
    fi

    echo "Installing i686-elf cross toolchain via homebrew..."
    # nativeos/i686-elf-toolchain provides precompiled bottles for
    # x86_64 and arm64 macOS. Reasonable to ~30s install.
    brew tap nativeos/i686-elf-toolchain || true
    brew install i686-elf-binutils i686-elf-gcc
}

install_from_source() {
    local prefix="${HOME}/.local/i686-elf-toolchain"
    local build_dir="${prefix}/build"

    local binutils_ver="2.42"
    local gcc_ver="14.2.0"

    echo "Building i686-elf cross toolchain from source under $prefix"
    echo "(This takes ~20 minutes. Coffee break time.)"

    mkdir -p "$build_dir"
    cd "$build_dir"

    # --- binutils ---
    if [[ ! -d "binutils-${binutils_ver}" ]]; then
        curl -LO "https://ftpmirror.gnu.org/binutils/binutils-${binutils_ver}.tar.xz"
        tar xf "binutils-${binutils_ver}.tar.xz"
    fi
    mkdir -p "build-binutils-${binutils_ver}"
    pushd "build-binutils-${binutils_ver}" >/dev/null
    "../binutils-${binutils_ver}/configure" \
        --target=i686-elf \
        --prefix="$prefix" \
        --with-sysroot \
        --disable-nls \
        --disable-werror
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
    make install
    popd >/dev/null

    export PATH="$prefix/bin:$PATH"

    # --- gcc ---
    if [[ ! -d "gcc-${gcc_ver}" ]]; then
        curl -LO "https://ftpmirror.gnu.org/gcc/gcc-${gcc_ver}/gcc-${gcc_ver}.tar.xz"
        tar xf "gcc-${gcc_ver}.tar.xz"
    fi
    pushd "gcc-${gcc_ver}" >/dev/null
    ./contrib/download_prerequisites
    popd >/dev/null

    mkdir -p "build-gcc-${gcc_ver}"
    pushd "build-gcc-${gcc_ver}" >/dev/null
    "../gcc-${gcc_ver}/configure" \
        --target=i686-elf \
        --prefix="$prefix" \
        --disable-nls \
        --enable-languages=c \
        --without-headers
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" all-gcc
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" all-target-libgcc
    make install-gcc
    make install-target-libgcc
    popd >/dev/null

    cat <<EOF

Installed under: $prefix
Add this to your shell profile:

    export PATH="$prefix/bin:\$PATH"

EOF
}

main() {
    if check_present; then
        echo "i686-elf cross toolchain already present:"
        for t in "${REQUIRED_TOOLS[@]}"; do
            printf "  %-22s %s\n" "$t" "$(command -v "$t")"
        done
        exit 0
    fi

    case "$(uname -s)" in
        Darwin)
            if install_macos_brew && check_present; then
                echo "i686-elf cross toolchain installed via homebrew."
                exit 0
            fi
            ;;
        Linux)
            # Most Linux distros don't package i686-elf-* — they ship
            # i686-linux-gnu instead, which links against glibc and is
            # not freestanding. Building from source is the reliable
            # path. (Arch's `gcc-i686-elf-bin` AUR package is an
            # exception; users who have it will already pass the
            # check_present check above.)
            ;;
    esac

    echo "Falling back to source build."
    install_from_source

    if check_present; then
        echo "i686-elf cross toolchain built from source."
        exit 0
    fi

    cat >&2 <<'EOF'
ERROR: i686-elf cross toolchain installation failed.

Options:
  1. Install homebrew on macOS, then re-run this script.
  2. Manually build binutils + gcc with --target=i686-elf following
     https://wiki.osdev.org/GCC_Cross-Compiler.
  3. Use a Docker image with the toolchain pre-built (TBD).
EOF
    exit 1
}

main "$@"

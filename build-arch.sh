#!/usr/bin/env bash
# Art Swap (PS5) - native Arch Linux build, replacing the Lima VM in ps5/tools/vm.sh
#
#   ./build-arch.sh                 # first run: packages + ps5/tools/bootstrap.sh, then build the title
#   ./build-arch.sh --skip-setup    # just build (stack already bootstrapped)
#   ./build-arch.sh --rebuild-radv  # bootstrap and rebuild the RADV archive
#   ./build-arch.sh TOOL [args]     # run any other ps5/tools/TOOL.sh (like vm.sh does)
#
# Run it from the root of your fork. Output: dist/PPSA99318/
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
STACK="${STACK:-$HOME/ps5}"          # same layout the README uses in the VM
GH="https://github.com/mihawk-99"

SKIP_SETUP=0; REBUILD_RADV=0; TOOL=build
while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-setup)   SKIP_SETUP=1; shift ;;
    --rebuild-radv) REBUILD_RADV=1; shift ;;
    -h|--help)      sed -n '2,8p' "$0"; exit 0 ;;
    *)              TOOL="$1"; shift; break ;;
  esac
done

log() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ -f "$ROOT/ps5/tools/bootstrap.sh" ]] || die "ps5/tools/bootstrap.sh not found: run this from the root of your fork."

# Same variables vm.sh exports
export PS5_VULKAN_DIR="$STACK/PS5_Vulkan"
export PS5_PAYLOAD_SDK_FORK="$STACK/PS5_PayloadSDK"
export PS5_MESA_DIR="$STACK/PS5_Mesa"
export PS5_VKHOMEBREWUI="$STACK/PS5_VulkanTemplate/.deps/PS5_VKHomebrewUI.git"

if [[ $SKIP_SETUP -eq 0 ]]; then
  log "Installing packages (sudo)"
  sudo pacman -S --needed --noconfirm \
    base-devel git cmake ninja make pkgconf curl bash \
    python python-pip python-mako python-yaml python-pillow python-ply python-packaging \
    meson clang lld llvm \
    glslang shaderc spirv-tools spirv-headers spirv-llvm-translator libclc vulkan-headers \
    bison flex zlib zstd expat libdrm libelf

  mkdir -p "$STACK/PS5_VulkanTemplate/.deps"
  if [[ ! -d "$PS5_VKHOMEBREWUI" ]]; then
    log "Cloning PS5_VKHomebrewUI"
    git clone --bare "$GH/PS5_VKHomebrewUI" "$PS5_VKHOMEBREWUI"
  fi

  if [[ ! -f "$STACK/env.sh" ]]; then
    printf 'export CC=clang CXX=clang++\n' > "$STACK/env.sh"
  fi
  . "$STACK/env.sh"

  # The repo's own bootstrap clones PS5_Vulkan / PS5_Mesa / PS5_PayloadSDK, sets up the
  # payload SDK, builds ps5-native-tool + libc.prx (make in PS5_Vulkan) and the RADV
  # release archive (a long build).
  # bootstrap also wants the console's address (for deploying over FTP) in PS5_Vulkan/.env.
  # Building does not need it: set PS5_HOST=<your PS5's IP> when running, or edit the file later.
  ENVFILE="$PS5_VULKAN_DIR/.env"
  if [[ -d "$PS5_VULKAN_DIR" && ! -f "$ENVFILE" ]] || { [[ -f "$ENVFILE" ]] && ! grep -q '^PS5_HOST=' "$ENVFILE"; }; then
    mkdir -p "$PS5_VULKAN_DIR"
    echo "PS5_HOST=${PS5_HOST:-192.168.1.100}" >> "$ENVFILE"
    echo "Wrote PS5_HOST=${PS5_HOST:-192.168.1.100} to $ENVFILE (edit it to your PS5's real address)"
  fi

  log "Running ps5/tools/bootstrap.sh"
  args=()
  [[ $REBUILD_RADV -eq 1 ]] && args+=(--rebuild-radv)
  bash "$ROOT/ps5/tools/bootstrap.sh" "${args[@]}" || echo "bootstrap.sh reported items to do (see above); checking what the build needs..."
fi

[[ -x "$PS5_VULKAN_DIR/build/host/ps5-native-tool" ]] || die "ps5-native-tool is missing in $PS5_VULKAN_DIR/build/host: run without --skip-setup."
[[ -f "$PS5_VULKAN_DIR/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a" ]] || die "RADV release archive missing: run without --skip-setup."

[[ -f "$STACK/env.sh" ]] && . "$STACK/env.sh"
command -v clang >/dev/null && echo "clang: $(clang --version | head -n1)  (README uses LLVM 22)"

cd "$ROOT"
# vm.sh keeps build/ps5 on a fast disk via a symlink; do the same.
if [[ ! -e build/ps5 ]]; then
  mkdir -p build "$STACK/build/PS5_ArtSwap/ps5"
  ln -s "$STACK/build/PS5_ArtSwap/ps5" build/ps5
fi

# An earlier failed run may have cached a wrong PS5_VULKAN_DIR; make CMake re-read it.
CACHE="build/ps5/CMakeCache.txt"
if [[ -f "$CACHE" ]] && ! grep -q "^PS5_VULKAN_DIR:.*=$PS5_VULKAN_DIR\$" "$CACHE"; then
  log "Clearing stale CMake cache (wrong PS5_VULKAN_DIR)"
  find build/ps5/ -mindepth 1 -delete   # whole folder, so build.ninja and CMakeFiles/ stay in step
fi

log "Running ps5/tools/$TOOL.sh $*"
bash "ps5/tools/$TOOL.sh" "$@"

if [[ "$TOOL" == build && -d dist/PPSA99318 ]]; then
  log "Done. Copy this folder to /data/homebrew/ on the PS5:"
  echo "  $ROOT/dist/PPSA99318"
  ls -lh dist/PPSA99318
fi

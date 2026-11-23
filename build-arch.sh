#!/usr/bin/env bash
# Art Swap (PS5) - native Arch Linux build, replacing the Lima VM in ps5/tools/vm.sh
#
#   ./build-arch.sh                 # first run: packages + ps5/tools/bootstrap.sh, then build the title
#   ./build-arch.sh --no-pacman     # do the setup but skip the pacman install (no sudo needed)
#   ./build-arch.sh --real-home     # don't redirect $HOME/caches into .ps5/home (default: redirected)
#   ./build-arch.sh --clean         # delete intermediate build files only; keeps dist/ and the helper, then stops
#   ./build-arch.sh --rebuild       # wipe everything (incl. dist/), then build from scratch (+ --skip-setup to skip setup)
#   ./build-arch.sh --skip-setup    # just build (stack already bootstrapped)
#   ./build-arch.sh --rebuild-radv  # bootstrap and rebuild the RADV archive
#   ./build-arch.sh TOOL [args]     # run any other ps5/tools/TOOL.sh (like vm.sh does)
#
# Run it from the root of your fork. Everything it creates stays in this folder:
#   .ps5/  the toolchain (PS5_Vulkan, PS5_Mesa, PS5_PayloadSDK, RADV, native tool)
#   build/ intermediates     dist/PPSA99318/  the finished title
# (only the pacman packages are system-wide; delete the fork folder to remove everything else)
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
STACK="${STACK:-$ROOT/.ps5}"          # everything (SDK, Mesa, RADV, tools) stays inside this repo folder
GH="https://github.com/mihawk-99"

SKIP_SETUP=0; REBUILD_RADV=0; NO_PACMAN=0; REAL_HOME=0; CLEAN=0; REBUILD=0; TOOL=build
while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-setup)   SKIP_SETUP=1; shift ;;
    --no-pacman)    NO_PACMAN=1; shift ;;
    --real-home)    REAL_HOME=1; shift ;;
    --clean)        CLEAN=1; shift ;;
    --rebuild)      CLEAN=1; REBUILD=1; shift ;;
    --rebuild-radv) REBUILD_RADV=1; shift ;;
    -h|--help)      sed -n '2,17p' "$0"; exit 0 ;;
    *)              TOOL="$1"; shift; break ;;
  esac
done

log() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ -f "$ROOT/ps5/tools/bootstrap.sh" ]] || die "ps5/tools/bootstrap.sh not found: run this from the root of your fork."

# --clean   removes intermediate files only (compiled objects, CMake cache) and KEEPS the finished
#           build: dist/ and build/helper/. --rebuild removes those as well.
# The toolchain in .ps5/ (SDK, RADV, native tool) is never touched, since it takes hours to rebuild;
# use --rebuild-radv if you want that redone.
do_clean() {
  log "Cleaning intermediate build files in $ROOT"
  [[ -d "$ROOT/build/ps5" ]] && find "$ROOT/build/ps5/" -mindepth 1 -delete   # via the symlink
  rm -f "$ROOT/build/ps5.configure.log"
  if [[ $REBUILD -eq 1 ]]; then
    echo "Also removing the finished build (dist/, build/helper/)"
    rm -rf "$ROOT/build/helper" "$ROOT/dist"
    make -C "$ROOT/helper" clean >/dev/null 2>&1 || true
  else
    echo "Kept: dist/ and build/helper/"
  fi
  echo "Clean."
}
if [[ $CLEAN -eq 1 ]]; then
  do_clean
  [[ $REBUILD -eq 1 ]] || exit 0
fi

# Point every checkout the repo's scripts look for at .ps5/ (their defaults are ../PS5_Vulkan etc.,
# i.e. next to the repo). Both Mesa names are set: vm.sh/PS5_Vulkan use PS5_MESA_DIR, bootstrap.sh
# reads PS5_MESA_FORK. The UI kit's bare repo goes where ps5/ui/setup-kit.sh keeps it, inside the repo.
export PS5_VULKAN_DIR="$STACK/PS5_Vulkan"
export PS5_PAYLOAD_SDK_FORK="$STACK/PS5_PayloadSDK"
export PS5_MESA_DIR="$STACK/PS5_Mesa"
export PS5_MESA_FORK="$STACK/PS5_Mesa"
export PS5_VKHOMEBREWUI="$ROOT/.deps/PS5_VKHomebrewUI.git"
mkdir -p "$STACK" "$ROOT/.deps"

# Keep $HOME-relative files (~/.local/bin, ~/.cache, pip/meson/cargo caches, tools/preview's
# ~/ps5/build) and temp files inside .ps5/ too. Done after pacman, which needs the real user.
use_local_home() {
  [[ $REAL_HOME -eq 1 ]] && return 0
  export HOME="$STACK/home" TMPDIR="$STACK/tmp"
  export XDG_CACHE_HOME="$HOME/.cache" XDG_CONFIG_HOME="$HOME/.config" XDG_DATA_HOME="$HOME/.local/share"
  mkdir -p "$HOME" "$TMPDIR" "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  # git still needs to trust the repo folders when HOME has no .gitconfig
  git config --global --add safe.directory '*' 2>/dev/null || true
}

if [[ $SKIP_SETUP -eq 0 ]]; then
  if [[ $NO_PACMAN -eq 1 ]]; then
    log "Skipping pacman (--no-pacman): make sure the packages are installed"
  else
    log "Installing packages (sudo)"
    sudo pacman -S --needed --noconfirm \
      base-devel git cmake ninja make pkgconf curl bash \
      python python-pip python-mako python-yaml python-pillow python-ply python-packaging \
      meson clang lld llvm \
      glslang shaderc spirv-tools spirv-headers spirv-llvm-translator libclc vulkan-headers \
      bison flex zlib zstd expat libdrm libelf
  fi

  use_local_home

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

[[ $SKIP_SETUP -eq 1 ]] && use_local_home
[[ -f "$STACK/env.sh" ]] && . "$STACK/env.sh"
command -v clang >/dev/null && echo "clang: $(clang --version | head -n1)  (README uses LLVM 22)"

cd "$ROOT"
# build/ps5 is a real folder inside the repo (vm.sh symlinked it to the VM's disk; not needed here).
# Replace a symlink left by an older version of this script.
[[ -L build/ps5 ]] && rm -f build/ps5
mkdir -p build/ps5

# Keep the toolchain and build output out of git without touching tracked files.
if [[ -d .git ]]; then
  mkdir -p .git/info
  for pat in /.ps5/ /build/ /dist/; do
    grep -qxF "$pat" .git/info/exclude 2>/dev/null || echo "$pat" >> .git/info/exclude
  done
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

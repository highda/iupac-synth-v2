#!/usr/bin/env bash
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
source_dir="$repo_root/build/pluginval-source"
build_dir="$repo_root/build/pluginval"
repository=https://github.com/Tracktion/pluginval.git
commit=ed19c2c16b57a6d94db391bea3ef4a80b769d5bf
juce_commit=5179f4e720d8406ebd1b5401c86aea8da6cc83c9

if [[ ! -d "$source_dir/.git" ]]; then
    git clone --no-checkout "$repository" "$source_dir"
fi

git -C "$source_dir" fetch origin "$commit"
git -C "$source_dir" checkout --detach "$commit"
git -C "$source_dir" submodule update --init --recursive

[[ "$(git -C "$source_dir" rev-parse HEAD)" == "$commit" ]]
[[ "$(git -C "$source_dir/modules/juce" rev-parse HEAD)" == "$juce_commit" ]]

patch_file="$repo_root/cmake/pluginval-linux.patch"
if [[ "$(uname -s)" == Darwin ]]; then
    # The LADSPA host patch is Linux-only; macOS builds the pinned source unmodified.
    [[ -z "$(git -C "$source_dir" status --short --untracked-files=no)" ]]
    extra_configure=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0)
    executable="$build_dir/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval"
elif git -C "$source_dir" apply --reverse --check "$patch_file" 2>/dev/null; then
    : # The declared Linux-only build patch is already applied.
    extra_configure=(); executable="$build_dir/pluginval_artefacts/Release/pluginval"
else
    [[ -z "$(git -C "$source_dir" status --short --untracked-files=no)" ]]
    git -C "$source_dir" apply "$patch_file"
    extra_configure=(); executable="$build_dir/pluginval_artefacts/Release/pluginval"
fi
git -C "$source_dir" diff --check
git -C "$source_dir" diff --quiet -- . ':!CMakeLists.txt'
[[ "$(uname -s)" == Darwin ]] || git -C "$source_dir" apply --reverse --check "$patch_file"

cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release ${extra_configure[@]+"${extra_configure[@]}"}
cmake --build "$build_dir" --target pluginval --parallel "${IUPAC_BUILD_JOBS:-2}"
"$executable" --version

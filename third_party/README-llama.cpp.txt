Embedded llama.cpp workspace

This directory is populated on demand by:

    ./scripts/embed_llama_cpp.sh

That script clones ggml-org/llama.cpp into third_party/llama.cpp and builds
the libraries needed for the integrated GUI backend under the llama.cpp build
tree.

After the bootstrap completes, re-run CMake for the main project so the GUI
links against the embedded llama.cpp targets.

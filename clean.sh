#rm -rf third_party/GCutil/bdwgc/out/
#rm -rf CMakeCache.txt
#rm -rf CMakeFiles

#CC=clang-6.0 CXX=clang++-6.0 \
cmake -DESCARGOT_HOST=linux \
      -DESCARGOT_ARCH=x64 \
      -DESCARGOT_MODE=debug \
      -DESCARGOT_OUTPUT=bin \
      -GNinja \
      -DESCARGOT_MEM_STATS=ON \
      -DESCARGOT_SNAPSHOT_SAVE=ON
#     -DESCARGOT_MEM_STATS=ON
#     -DESCARGOT_VALGRIND=ON

ninja

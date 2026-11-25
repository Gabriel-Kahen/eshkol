cmake_minimum_required(VERSION 3.24)
project(eshkol_embedding_consumer LANGUAGES C CXX)

find_package(EshkolEmbedding CONFIG REQUIRED
  PATHS "${ESHKOL_SDK_PREFIX}" NO_DEFAULT_PATH)

add_executable(eshkol_embedding_smoke embed.c)
target_compile_features(eshkol_embedding_smoke PRIVATE c_std_11)
target_link_libraries(eshkol_embedding_smoke PRIVATE Eshkol::embed)
set_target_properties(eshkol_embedding_smoke PROPERTIES LINKER_LANGUAGE CXX)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/consumer-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:eshkol_embedding_smoke>\n${EshkolEmbedding_DIR}\n${EshkolEmbedding_STDLIB_DIR}\n")

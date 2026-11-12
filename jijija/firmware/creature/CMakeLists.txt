# اسکلت Zephyr موجود (فاز 0.5) — هنوز با west build تست نشده؛ Zephyr در محیط توسعه فعلی نصب نیست.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(creature)

set(ORGAN_DIR ${CMAKE_CURRENT_LIST_DIR}/../../genome/organs)
set(ORGAN_GEN ${CMAKE_CURRENT_BINARY_DIR}/organs_embedded.c)

file(GLOB ORGAN_TOMLS ${ORGAN_DIR}/*.toml)
add_custom_command(
  OUTPUT ${ORGAN_GEN}
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_organs.py
          ${ORGAN_DIR} ${ORGAN_GEN}
  DEPENDS ${ORGAN_TOMLS} ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_organs.py
)

target_sources(app PRIVATE src/main.c src/organ.c ${ORGAN_GEN})
target_include_directories(app PRIVATE src)

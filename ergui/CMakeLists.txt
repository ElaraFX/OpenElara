
# ER executable

INCLUDE_DIRECTORIES (${CMAKE_CURRENT_SOURCE_DIR}/../liber/include)

FILE (GLOB h_src *.h)
FILE (GLOB cpp_src *.cpp)

ADD_EXECUTABLE (er ${h_src} ${cpp_src})
TARGET_LINK_LIBRARIES (er liber ${TBB_LIBRARYS})

INSTALL (TARGETS er RUNTIME DESTINATION bin)

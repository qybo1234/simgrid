if(enable_smpi AND enable_smpi_MPICH3_testsuite AND SMPI_FORTRAN)
  if(WIN32)
    set(CMAKE_C_FLAGS "-include ${CMAKE_HOME_DIRECTORY}/include/smpi/smpi_main.h")
  else()
    set(CMAKE_C_COMPILER "${CMAKE_BINARY_DIR}/smpi_script/bin/smpicc")
    set(CMAKE_Fortran_COMPILER "${CMAKE_BINARY_DIR}/smpi_script/bin/smpiff")
  endif()

  include_directories(BEFORE "${CMAKE_HOME_DIRECTORY}/include/smpi")

#  add_executable(allocmemf EXCLUDE_FROM_ALL allocmemf.f)
#  add_executable(c2f2cf    EXCLUDE_FROM_ALL c2f2cf.f c2f2c.c)
#  add_executable(ctypesinf EXCLUDE_FROM_ALL ctypesinf.f ctypesfromc.c)
# add_dependencies(tests allocmemf c2f2cf ctypesinf)

#  target_link_libraries(allocmemf simgrid mtest_f77)
#  target_link_libraries(c2f2cf simgrid mtest_f77)
#  target_link_libraries(ctypesinf simgrid mtest_f77)
endif()

set(examples_src
  ${examples_src}
  ${CMAKE_CURRENT_SOURCE_DIR}/add1size.h
  ${CMAKE_CURRENT_SOURCE_DIR}/allocmemf.f
  ${CMAKE_CURRENT_SOURCE_DIR}/c2f2c.c
  ${CMAKE_CURRENT_SOURCE_DIR}/c2f2cf.f
  ${CMAKE_CURRENT_SOURCE_DIR}/c2fmult.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ctypesfromc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ctypesinf.f
  PARENT_SCOPE)
set(txt_files
  ${txt_files}
  ${CMAKE_CURRENT_SOURCE_DIR}/testlist
  PARENT_SCOPE)

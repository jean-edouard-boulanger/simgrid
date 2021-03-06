foreach(x actor actor-autorestart actor-migration
        activity-lifecycle
        comm-pt2pt
        cloud-interrupt-migration cloud-sharing
        concurrent_rw storage_client_server listen_async pid )
  add_executable       (${x}  ${x}/${x}.cpp)
  target_link_libraries(${x}  simgrid)
  set_target_properties(${x}  PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${x})

  set(teshsuite_src ${teshsuite_src} ${CMAKE_CURRENT_SOURCE_DIR}/${x}/${x}.cpp)
endforeach()

## Add the tests.
## Some need to be run with all factories, some need not tesh to run
foreach(x actor actor-autorestart actor-migration 
        activity-lifecycle
	cloud-interrupt-migration concurrent_rw) # TODO: actor-autorestart is disabled for now
  set(tesh_files    ${tesh_files}    ${CMAKE_CURRENT_SOURCE_DIR}/${x}/${x}.tesh)
  ADD_TESH_FACTORIES(tesh-s4u-${x} "thread;ucontext;raw;boost" --setenv srcdir=${CMAKE_HOME_DIRECTORY}/teshsuite/s4u/${x} --setenv platfdir=${CMAKE_HOME_DIRECTORY}/examples/platforms --cd ${CMAKE_BINARY_DIR}/teshsuite/s4u/${x} ${CMAKE_HOME_DIRECTORY}/teshsuite/s4u/${x}/${x}.tesh)
endforeach()

# Manually add this file, but the test is broken and must be disabled for now (TODO)
set(tesh_files    ${tesh_files} ${CMAKE_CURRENT_SOURCE_DIR}/actor-autorestart/actor-autorestart.tesh)


foreach(x listen_async pid storage_client_server cloud-sharing)
  set(tesh_files    ${tesh_files}    ${CMAKE_CURRENT_SOURCE_DIR}/${x}/${x}.tesh)
  ADD_TESH(tesh-s4u-${x} --setenv srcdir=${CMAKE_HOME_DIRECTORY}/teshsuite/s4u/${x} --setenv platfdir=${CMAKE_HOME_DIRECTORY}/examples/platforms --cd ${CMAKE_BINARY_DIR}/teshsuite/s4u/${x} ${CMAKE_HOME_DIRECTORY}/teshsuite/s4u/${x}/${x}.tesh)
endforeach()

# The output is not relevant
ADD_TEST(tesh-s4u-comm-pt2pt    ${CMAKE_BINARY_DIR}/teshsuite/s4u/comm-pt2pt/comm-pt2pt    ${CMAKE_HOME_DIRECTORY}/examples/platforms/cluster_backbone.xml)



set(teshsuite_src ${teshsuite_src}  PARENT_SCOPE)
set(tesh_files    ${tesh_files}     PARENT_SCOPE)
set(xml_files     ${xml_files}      ${CMAKE_CURRENT_SOURCE_DIR}/activity-lifecycle/testing_platform.xml PARENT_SCOPE)

foreach(example actor-create actor-daemon actor-join actor-kill actor-migrate actor-suspend actor-yield # actor-lifetime
                exec-basic)
  set(tesh_files    ${tesh_files}   ${CMAKE_CURRENT_SOURCE_DIR}/${example}/${example}.tesh)
  set(examples_src  ${examples_src} ${CMAKE_CURRENT_SOURCE_DIR}/${example}/${example}.py)

  if(enable_python)
    ADD_TESH(python-${example} --setenv srcdir=${CMAKE_CURRENT_SOURCE_DIR}/${example}
                               --setenv platfdir=${CMAKE_HOME_DIRECTORY}/examples/platforms
			       --setenv LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/lib
			       --setenv PYTHONPATH=${CMAKE_BINARY_DIR}/lib
			       --cd ${CMAKE_CURRENT_SOURCE_DIR}/${example}
                               ${CMAKE_HOME_DIRECTORY}/examples/python/${example}/${example}.tesh)
  endif()
endforeach()

set(tesh_files    examples/python/actor-create/actor-create_d.xml
                  examples/python/actor-lifetime/actor-lifetime_d.xml
		  examples/python/actor-lifetime/actor-lifetime.py     # example broken so far
		  examples/python/actor-lifetime/actor-lifetime.tesh
                  ${tesh_files}    PARENT_SCOPE)
set(examples_src  ${examples_src}  PARENT_SCOPE)

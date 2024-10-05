#[=========================================================================[
  Copyright (c) 2022-2024 Pedro López-Cabanillas

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
]=========================================================================]

add_executable ( sonivoxrender sonivoxrender.c )

if (BUILD_SONIVOX_STATIC)
    target_link_libraries ( sonivoxrender sonivox::sonivox-static )
elseif (BUILD_SONIVOX_SHARED)
    target_link_libraries ( sonivoxrender sonivox::sonivox )
endif()

if (BUILD_MANPAGE)
    if(NOT EXISTS ${PANDOC_EXECUTABLE})
        find_program(PANDOC_EXECUTABLE pandoc)
        mark_as_advanced(PANDOC_EXECUTABLE)
    endif()

    if (EXISTS ${PANDOC_EXECUTABLE})
        set(_src ${CMAKE_CURRENT_SOURCE_DIR}/sonivoxrender.1.md)
        if (NOT PROJECT_RELEASE_DATE)
            unset(_date)
            execute_process (
                COMMAND bash -c "LANG=en;date +'%B %d, %Y'"
                OUTPUT_VARIABLE _date
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            set(_date ${PROJECT_RELEASE_DATE})
        endif()
        set(_footer "${PROJECT_NAME} ${PROJECT_VERSION}")
        add_custom_command (
            OUTPUT sonivoxrender.1
            COMMAND ${PANDOC_EXECUTABLE} -s -t man -Vdate=${_date} -Vfooter=${_footer} ${_src} -o sonivoxrender.1
            DEPENDS ${_src}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            VERBATIM
        )
        add_custom_target(manpage ALL DEPENDS sonivoxrender.1)
    endif()
else()
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sonivoxrender.1 ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
endif()

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sonivoxrender.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

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

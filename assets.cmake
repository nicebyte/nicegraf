#[[
Copyright (c) 2022 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
]]

# This function adds a new target and sets some configuration options for it.
# Parameters:
#  NAME - the name of the target.
#  DEPS - a list of dependencies for the target.
#  PROPERTIES - a list of properties to set for the target.
#  JSON_PATH - the path to the JSON file to read.
#  SOURCES_BASE_DIR - the directory containing the source files.
#  DESTINATION_BASE_DIR - the directory to copy the files to.

function(nmk_assets_target)
    cmake_parse_arguments(ASSETS_TARGET "" "NAME;DEPS;JSON_PATH;SOURCES_BASE_DIR;DESTINATION_BASE_DIR" "PROPERTIES" ${ARGN})

    # Create the target for copying assets
    add_custom_target(${ASSETS_TARGET_NAME})
    add_dependencies(${ASSETS_TARGET_NAME} ${ASSETS_TARGET_DEPS})
    set_target_properties(${ASSETS_TARGET_NAME} PROPERTIES ${ASSETS_TARGET_PROPERTIES})

    # Read the JSON file into a variable
    file(READ ${ASSETS_TARGET_JSON_PATH} JSON_CONTENT)

    # Define a regular expression pattern to match asset objects
    set(PATTERN "\"[^\"]+\"[ \t\r\n]*:[ \t\r\n]*{[^}]+}")

    # Find all matches in the JSON content
    string(REGEX MATCHALL ${PATTERN} MATCHES ${JSON_CONTENT})

    add_custom_command(
        TARGET ${ASSETS_TARGET_NAME}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSETS_TARGET_DESTINATION_BASE_DIR}/assets"
        COMMENT "Creating resource directories"
    )

    add_custom_command(
        TARGET ${ASSETS_TARGET_NAME}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSETS_TARGET_SOURCES_BASE_DIR}/shaders/imgui.pipeline"
            "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders/imgui.pipeline"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSETS_TARGET_SOURCES_BASE_DIR}/shaders/imgui.ps.21.msl"
            "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders/imgui.ps.21.msl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSETS_TARGET_SOURCES_BASE_DIR}/shaders/imgui.ps.spv"
            "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders/imgui.ps.spv"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSETS_TARGET_SOURCES_BASE_DIR}/shaders/imgui.vs.21.msl"
            "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders/imgui.vs.21.msl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSETS_TARGET_SOURCES_BASE_DIR}/shaders/imgui.vs.spv"
            "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders/imgui.vs.spv"
        COMMENT "Copying imgui shader resources"
    )

    # Iterate over the matches and extract the asset objects
    foreach(MATCH ${MATCHES})
        # Extract the asset object
        string(REGEX REPLACE "\"[^\"]+\"[ \t\r\n]*:[ \t\r\n]*{([^}]+)}" "\\1" ASSET_OBJECT ${MATCH})
        
        # Parse the asset object
        string(REGEX MATCH "\"type\":[ \t\r\n]*\"([^\"]+)\"" TYPE_MATCH ${ASSET_OBJECT})
        string(REGEX MATCH "\"path\":[ \t\r\n]*\"([^\"]+)\"" PATH_MATCH ${ASSET_OBJECT})
        
        # Extract the type and path values
        string(REGEX REPLACE "\"type\":[ \t\r\n]*\"([^\"]+)\"" "\\1" ASSET_TYPE ${TYPE_MATCH})
        string(REGEX REPLACE "\"path\":[ \t\r\n]*\"([^\"]+)\"" "\\1" ASSET_FILE_PATH ${PATH_MATCH})

        # Set the destination folder based on the asset type
        if(ASSET_TYPE STREQUAL "vertexShader" OR ASSET_TYPE STREQUAL "fragmentShader" OR ASSET_TYPE STREQUAL "computeShader")
            set(ASSET_EXTENSION "vs")

            if (ASSET_TYPE STREQUAL "fragmentShader")
                set(ASSET_EXTENSION "ps")
            elseif (ASSET_TYPE STREQUAL "computeShader")
                set(ASSET_EXTENSION "cs")
            endif()

            # Remove the extension from ASSET_FILE_PATH
            string(REGEX REPLACE "\\.[^.]*$" "" ASSET_FILE_NAME ${ASSET_FILE_PATH})

            add_custom_command(
                TARGET ${ASSETS_TARGET_NAME}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ASSETS_TARGET_SOURCES_BASE_DIR}/shaders/${ASSET_FILE_NAME}.${ASSET_EXTENSION}.21.msl"
                    "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders/${ASSET_FILE_NAME}.${ASSET_EXTENSION}.21.msl"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ASSETS_TARGET_SOURCES_BASE_DIR}/shaders/${ASSET_FILE_NAME}.${ASSET_EXTENSION}.spv"
                    "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders/${ASSET_FILE_NAME}.${ASSET_EXTENSION}.spv"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ASSETS_TARGET_SOURCES_BASE_DIR}/shaders/${ASSET_FILE_NAME}.pipeline"
                    "${ASSETS_TARGET_DESTINATION_BASE_DIR}/shaders/${ASSET_FILE_NAME}.pipeline"
                COMMENT "Copying shader resources"
            )
        else()
            # Copy the asset to the destination folder
            add_custom_command(
                TARGET ${ASSETS_TARGET_NAME}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ASSETS_TARGET_SOURCES_BASE_DIR}/assets/${ASSET_FILE_PATH}"
                    "${ASSETS_TARGET_DESTINATION_BASE_DIR}/assets/${ASSET_FILE_PATH}"
                COMMENT "Copying ${ASSET_FILE_PATH} to asset resources"
            )
        endif()
    endforeach(MATCH)
endfunction()
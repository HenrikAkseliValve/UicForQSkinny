cmake_minimum_required(VERSION 3.16)

#############################################
#  Builder for qskinny-uic                  #
#############################################
project(qskinny-uic VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Xml)
qt_standard_project_setup()

qt_add_executable(qskuic
	src/uic/main.cpp
)

target_link_libraries(qskuic PRIVATE Qt6::Core Qt6::Xml)

#############################################
# Document builder                          #
#############################################
find_program(SPHINX_EXECUTABLE sphinx-build)
If(SPHINX_EXECUTABLE)
	add_custom_target(doc
		COMMAND ${SPHINX_EXECUTABLE} -b html ${CMAKE_SOURCE_DIR}/doc doc
		COMMENT "Building documentation....."
	)
else()
	message(STATUS "Sphinx document generator not found.")
endif()
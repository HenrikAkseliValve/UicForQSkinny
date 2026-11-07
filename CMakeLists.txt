cmake_minimum_required(VERSION 3.16)

#######################################################################################################################################
# Adds profile for the address sanitizing.                                                                                            #
# source https://stackoverflow.com/questions/44320465/whats-the-proper-way-to-enable-addresssanitizer-in-cmake-that-works-in-xcode    #
#######################################################################################################################################

if(CMAKE_CONFIGURATION_TYPES)
	list(APPEND CMAKE_CONFIGURATION_TYPES Asan)
else()
	set(allowedBuildTypes Asan Debug Release RelWithDebInfo MinSizeRel)
		set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${allowedBuildTypes}")

	if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE IN_LIST allowedBuildTypes)
		message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}")
	endif()
endif()

set(CMAKE_C_FLAGS_ASAN
    "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer" CACHE STRING
    "Flags used by the C compiler for Asan build type or configuration." FORCE)

set(CMAKE_CXX_FLAGS_ASAN
    "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer" CACHE STRING
    "Flags used by the C++ compiler for Asan build type or configuration." FORCE)

set(CMAKE_EXE_LINKER_FLAGS_ASAN
    "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address" CACHE STRING
    "Linker flags to be used to create executables for Asan build type." FORCE)

set(CMAKE_SHARED_LINKER_FLAGS_ASAN
    "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address" CACHE STRING
    "Linker lags to be used to create shared libraries for Asan build type." FORCE)

#############################################
#  Builder for qskinny-uic                  #
#############################################
project(qskinny-uic VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

qt_add_executable(qskuic
	src/uic/main.cpp
)

target_link_libraries(qskuic PRIVATE Qt6::Core)

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
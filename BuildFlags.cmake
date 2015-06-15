# -----------------------------------------------------------------------------
# Default build mode and default flags
# -----------------------------------------------------------------------------

# CMake does not intialize the build type by default. Set it to release
# if no cache variable is present.
IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE Release CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
ENDIF(NOT CMAKE_BUILD_TYPE)

option(BUILD_SHARED_LIBS "Build all libraries shared" OFF)

# The newer standard requirement feature + automatic flag setting requires
# cmake > 3.1.
if(CMAKE_VERSION VERSION_LESS "3.2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
    # Require C++11 capable compiler for the whole project.
    # Could also be done one a per target level.
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED on)
endif()

if (NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -pg") # Not a valid MSVC option
endif()

# Use automatically architecture inference.
if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++11")
    #Clang infers march automatically.
endif()

# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-src")
  file(MAKE_DIRECTORY "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-src")
endif()
file(MAKE_DIRECTORY
  "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-build"
  "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-subbuild/boost_math-populate-prefix"
  "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-subbuild/boost_math-populate-prefix/tmp"
  "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-subbuild/boost_math-populate-prefix/src/boost_math-populate-stamp"
  "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-subbuild/boost_math-populate-prefix/src"
  "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-subbuild/boost_math-populate-prefix/src/boost_math-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-subbuild/boost_math-populate-prefix/src/boost_math-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/coding/TechnoVisualsVulkanCpp/build/_deps/boost_math-subbuild/boost_math-populate-prefix/src/boost_math-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()

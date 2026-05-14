# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-src")
  file(MAKE_DIRECTORY "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-src")
endif()
file(MAKE_DIRECTORY
  "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-build"
  "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-subbuild/kfr-populate-prefix"
  "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-subbuild/kfr-populate-prefix/tmp"
  "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-subbuild/kfr-populate-prefix/src/kfr-populate-stamp"
  "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-subbuild/kfr-populate-prefix/src"
  "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-subbuild/kfr-populate-prefix/src/kfr-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-subbuild/kfr-populate-prefix/src/kfr-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ricky/coding/proyects/vulkancpp/build/_deps/kfr-subbuild/kfr-populate-prefix/src/kfr-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()

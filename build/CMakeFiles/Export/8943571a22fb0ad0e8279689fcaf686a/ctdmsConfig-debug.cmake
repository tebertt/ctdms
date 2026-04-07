#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ctdms::ctdms" for configuration "Debug"
set_property(TARGET ctdms::ctdms APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(ctdms::ctdms PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libctdms.a"
  )

list(APPEND _cmake_import_check_targets ctdms::ctdms )
list(APPEND _cmake_import_check_files_for_ctdms::ctdms "${_IMPORT_PREFIX}/lib/libctdms.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

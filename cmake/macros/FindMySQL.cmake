#
# Find the MySQL client includes and library
#

# This module defines
# MYSQL_INCLUDE_DIR, where to find mysql.h
# MYSQL_LIBRARIES, the libraries to link against to connect to MySQL
# MYSQL_EXECUTABLE, the MySQL executable.
# MYSQL_FOUND, if false, you cannot build anything that requires MySQL.

# also defined, but not for general use are
# MYSQL_LIBRARY, where to find the MySQL library.

set( MYSQL_FOUND 0 )

if( UNIX )
  set(MYSQL_CONFIG_PREFER_PATH "$ENV{MYSQL_HOME}/bin" CACHE FILEPATH
    "preferred path to MySQL (mysql_config)"
  )

  find_program(MYSQL_CONFIG mysql_config
    ${MYSQL_CONFIG_PREFER_PATH}
    /usr/local/mysql/bin/
    /usr/local/bin/
    /usr/bin/
  )

  if( MYSQL_CONFIG )
    message(STATUS "Using mysql-config: ${MYSQL_CONFIG}")
    # set INCLUDE_DIR
    exec_program(${MYSQL_CONFIG}
      ARGS --include
      OUTPUT_VARIABLE MY_TMP
    )

    string(REGEX REPLACE "-I([^ ]*)( .*)?" "\\1" MY_TMP "${MY_TMP}")
    set(MYSQL_ADD_INCLUDE_PATH ${MY_TMP} CACHE FILEPATH INTERNAL)
    #message("[DEBUG] MYSQL ADD_INCLUDE_PATH : ${MYSQL_ADD_INCLUDE_PATH}")
    # set LIBRARY_DIR
    exec_program(${MYSQL_CONFIG}
      ARGS --libs_r
      OUTPUT_VARIABLE MY_TMP
    )
    set(MYSQL_ADD_LIBRARIES "")
    string(REGEX MATCHALL "-l[^ ]*" MYSQL_LIB_LIST "${MY_TMP}")
    foreach(LIB ${MYSQL_LIB_LIST})
      string(REGEX REPLACE "[ ]*-l([^ ]*)" "\\1" LIB "${LIB}")
      list(APPEND MYSQL_ADD_LIBRARIES "${LIB}")
      #message("[DEBUG] MYSQL ADD_LIBRARIES : ${MYSQL_ADD_LIBRARIES}")
    endforeach(LIB ${MYSQL_LIB_LIST})

    set(MYSQL_ADD_LIBRARIES_PATH "")
    string(REGEX MATCHALL "-L[^ ]*" MYSQL_LIBDIR_LIST "${MY_TMP}")
    foreach(LIB ${MYSQL_LIBDIR_LIST})
      string(REGEX REPLACE "[ ]*-L([^ ]*)" "\\1" LIB "${LIB}")
      list(APPEND MYSQL_ADD_LIBRARIES_PATH "${LIB}")
      #message("[DEBUG] MYSQL ADD_LIBRARIES_PATH : ${MYSQL_ADD_LIBRARIES_PATH}")
    endforeach(LIB ${MYSQL_LIBS})

  else( MYSQL_CONFIG )
    set(MYSQL_ADD_LIBRARIES "")
    list(APPEND MYSQL_ADD_LIBRARIES "mysqlclient_r")
  endif( MYSQL_CONFIG )
endif( UNIX )

if( WIN32 )
  # read environment variables and change \ to /
  # NOTE: test the variable, do not dereference it - "if (${VAR})" expands to
  # if ("C:/Program Files") which CMake evaluates as false, so the normalization
  # below never ran. The replace pattern was also wrong: in CMake "\\" is a
  # single literal backslash, "\\\\" is two.
  set(PROGRAM_FILES_32 "$ENV{ProgramFiles}")
  if( PROGRAM_FILES_32 )
    string(REPLACE "\\" "/" PROGRAM_FILES_32 "${PROGRAM_FILES_32}")
  endif()

  set(PROGRAM_FILES_64 "$ENV{ProgramW6432}")
  if( PROGRAM_FILES_64 )
    string(REPLACE "\\" "/" PROGRAM_FILES_64 "${PROGRAM_FILES_64}")
  endif()

  set(SYSTEM_DRIVE "$ENV{SystemDrive}")
  if( SYSTEM_DRIVE )
    string(REPLACE "\\" "/" SYSTEM_DRIVE "${SYSTEM_DRIVE}")
  endif()

  # Build the search bases one at a time: an unset environment variable would
  # otherwise leave the bare string "/MySQL", which is truthy and gets probed
  # against the filesystem root.
  set(MYSQL_SEARCH_BASES "")
  if( PROGRAM_FILES_64 )
    list(APPEND MYSQL_SEARCH_BASES "${PROGRAM_FILES_64}/MySQL")
  endif()
  if( PROGRAM_FILES_32 )
    list(APPEND MYSQL_SEARCH_BASES "${PROGRAM_FILES_32}/MySQL")
  endif()
  if( SYSTEM_DRIVE )
    list(APPEND MYSQL_SEARCH_BASES "${SYSTEM_DRIVE}/MySQL")
  endif()
  list(APPEND MYSQL_SEARCH_BASES "C:/MySQL")

  # Discover every installed "MySQL Server <version>" directory rather than
  # relying on a hardcoded version list, which silently broke on each new MySQL
  # release. Newest version is searched first.
  set(MYSQL_VERSIONED_ROOTS "")
  foreach( MYSQL_SEARCH_BASE ${MYSQL_SEARCH_BASES} )
    if( IS_DIRECTORY "${MYSQL_SEARCH_BASE}" )
      file(GLOB MYSQL_SEARCH_CANDIDATES LIST_DIRECTORIES true "${MYSQL_SEARCH_BASE}/MySQL Server *")
      foreach( MYSQL_SEARCH_CANDIDATE ${MYSQL_SEARCH_CANDIDATES} )
        if( IS_DIRECTORY "${MYSQL_SEARCH_CANDIDATE}" )
          list(APPEND MYSQL_VERSIONED_ROOTS "${MYSQL_SEARCH_CANDIDATE}")
        endif()
      endforeach()
    endif()
  endforeach()

  if( MYSQL_VERSIONED_ROOTS )
    list(REMOVE_DUPLICATES MYSQL_VERSIONED_ROOTS)

    # Sorting the full paths lets the directory prefix outweigh the version, so
    # "C:/Program Files/MySQL/MySQL Server 5.7" would beat "C:/MySQL/MySQL Server 9.7".
    # Sort on the version alone by prefixing it, then strip the prefix back off.
    set(MYSQL_SORTABLE_ROOTS "")
    foreach( MYSQL_ROOT_DIR ${MYSQL_VERSIONED_ROOTS} )
      # A directory whose name carries no version sorts last rather than first
      set(MYSQL_ROOT_VERSION "0")
      if( "${MYSQL_ROOT_DIR}" MATCHES "MySQL Server ([0-9][0-9.]*)" )
        set(MYSQL_ROOT_VERSION "${CMAKE_MATCH_1}")
      endif()
      list(APPEND MYSQL_SORTABLE_ROOTS "${MYSQL_ROOT_VERSION}|${MYSQL_ROOT_DIR}")
    endforeach()
    list(SORT MYSQL_SORTABLE_ROOTS COMPARE NATURAL ORDER DESCENDING)

    set(MYSQL_VERSIONED_ROOTS "")
    foreach( MYSQL_SORTABLE_ROOT ${MYSQL_SORTABLE_ROOTS} )
      string(REGEX REPLACE "^[^|]*\\|" "" MYSQL_ROOT_DIR "${MYSQL_SORTABLE_ROOT}")
      list(APPEND MYSQL_VERSIONED_ROOTS "${MYSQL_ROOT_DIR}")
    endforeach()
  endif()

  set(MYSQL_DISCOVERED_INCLUDE_PATHS "")
  set(MYSQL_DISCOVERED_LIB_PATHS "")
  set(MYSQL_DISCOVERED_BIN_PATHS "")
  foreach( MYSQL_ROOT_DIR ${MYSQL_VERSIONED_ROOTS} )
    list(APPEND MYSQL_DISCOVERED_INCLUDE_PATHS "${MYSQL_ROOT_DIR}/include")
    list(APPEND MYSQL_DISCOVERED_LIB_PATHS "${MYSQL_ROOT_DIR}/lib" "${MYSQL_ROOT_DIR}/lib/opt")
    list(APPEND MYSQL_DISCOVERED_BIN_PATHS "${MYSQL_ROOT_DIR}/bin" "${MYSQL_ROOT_DIR}/bin/opt")
  endforeach()

  if( MYSQL_VERSIONED_ROOTS )
    message(STATUS "Detected MySQL installations: ${MYSQL_VERSIONED_ROOTS}")
  endif()
endif ( WIN32 )

find_path(MYSQL_INCLUDE_DIR
  NAMES
    mysql.h
  PATHS
    ${MYSQL_ADD_INCLUDE_PATH}
    ${MYSQL_DISCOVERED_INCLUDE_PATHS}
    /usr/include
    /usr/include/mysql
    /usr/local/include
    /usr/local/include/mysql
    /usr/local/mysql/include
    "${PROGRAM_FILES_64}/MySQL/include"
    "${PROGRAM_FILES_32}/MySQL/include"
    "C:/MySQL/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 9.0;Location]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.4;Location]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.0;Location]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 9.0;Location]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.4;Location]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.0;Location]/include"
    "c:/msys/local/include"
    "$ENV{MYSQL_ROOT}/include"
  DOC
    "Specify the directory containing mysql.h."
)

if( UNIX )
  foreach(LIB ${MYSQL_ADD_LIBRARIES})
    find_library( MYSQL_LIBRARY
      NAMES
        mysql libmysql ${LIB}
      PATHS
        ${MYSQL_ADD_LIBRARIES_PATH}
        /usr/lib
        /usr/lib/mysql
        /usr/local/lib
        /usr/local/lib/mysql
        /usr/local/mysql/lib
      DOC "Specify the location of the mysql library here."
    )
  endforeach(LIB ${MYSQL_ADD_LIBRARY})
endif( UNIX )

if( WIN32 )
  find_library( MYSQL_LIBRARY
    NAMES
      libmysql
    PATHS
      ${MYSQL_ADD_LIBRARIES_PATH}
      ${MYSQL_DISCOVERED_LIB_PATHS}
      "${PROGRAM_FILES_64}/MySQL/lib"
      "${PROGRAM_FILES_32}/MySQL/lib"
      "C:/MySQL/lib/debug"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 9.0;Location]/lib"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.4;Location]/lib"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.0;Location]/lib"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 9.0;Location]/lib/opt"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.4;Location]/lib/opt"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.0;Location]/lib/opt"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 9.0;Location]/lib"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.4;Location]/lib"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.0;Location]/lib"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 9.0;Location]/lib/opt"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.4;Location]/lib/opt"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.0;Location]/lib/opt"
      "c:/msys/local/include"
      "$ENV{MYSQL_ROOT}/lib"
    DOC "Specify the location of the mysql library here."
  )
endif( WIN32 )

# On Windows you typically don't need to include any extra libraries
# to build MYSQL stuff.

if( NOT WIN32 )
  find_library( MYSQL_EXTRA_LIBRARIES
    NAMES
      z zlib
    PATHS
      /usr/lib
      /usr/local/lib
    DOC
      "if more libraries are necessary to link in a MySQL client (typically zlib), specify them here."
  )
else( NOT WIN32 )
  set( MYSQL_EXTRA_LIBRARIES "" )
endif( NOT WIN32 )

if( UNIX )
    find_program(MYSQL_EXECUTABLE mysql
    PATHS
        ${MYSQL_CONFIG_PREFER_PATH}
        /usr/local/mysql/bin/
        /usr/local/bin/
        /usr/bin/
    DOC
        "path to your mysql binary."
    )
endif( UNIX )

if( WIN32 )
    find_program(MYSQL_EXECUTABLE mysql
      PATHS
        ${MYSQL_DISCOVERED_BIN_PATHS}
        "${PROGRAM_FILES_64}/MySQL/bin"
        "${PROGRAM_FILES_32}/MySQL/bin"
        "C:/MySQL/bin/debug"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 9.0;Location]/bin"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.4;Location]/bin"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.0;Location]/bin"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 9.0;Location]/bin/opt"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.4;Location]/bin/opt"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MySQL AB\\MySQL Server 8.0;Location]/bin/opt"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 9.0;Location]/bin"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.4;Location]/bin"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.0;Location]/bin"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 9.0;Location]/bin/opt"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.4;Location]/bin/opt"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\MySQL AB\\MySQL Server 8.0;Location]/bin/opt"
        "c:/msys/local/include"
        "$ENV{MYSQL_ROOT}/bin"
     DOC
        "path to your mysql binary."
    )
endif( WIN32 )

if( MYSQL_LIBRARY )
  if( MYSQL_INCLUDE_DIR )
    set( MYSQL_FOUND 1 )
    message(STATUS "Found MySQL library: ${MYSQL_LIBRARY}")
    message(STATUS "Found MySQL headers: ${MYSQL_INCLUDE_DIR}")
  else( MYSQL_INCLUDE_DIR )
    message(FATAL_ERROR "Could not find MySQL headers! Please install the development libraries and headers")
  endif( MYSQL_INCLUDE_DIR )
  if( MYSQL_EXECUTABLE )
    message(STATUS "Found MySQL executable: ${MYSQL_EXECUTABLE}")
  endif( MYSQL_EXECUTABLE )
  mark_as_advanced( MYSQL_FOUND MYSQL_LIBRARY MYSQL_EXTRA_LIBRARIES MYSQL_INCLUDE_DIR MYSQL_EXECUTABLE)
else( MYSQL_LIBRARY )
  message(FATAL_ERROR "Could not find the MySQL libraries! Please install the development libraries and headers")
endif( MYSQL_LIBRARY )

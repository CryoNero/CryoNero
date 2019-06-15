if(NOT IOS)
    find_package(Miniupnpc QUIET)
endif()

# If we have the correct shared version and we're not building static, use it
if(STATIC OR IOS)
 set(USE_SHARED_MINIUPNPC false)
elseif(MINIUPNP_FOUND AND MINIUPNPC_VERSION_1_7_OR_HIGHER)
 set(USE_SHARED_MINIUPNPC true)
endif()

if(USE_SHARED_MINIUPNPC)
  message(STATUS "Using shared miniupnpc found at ${MINIUPNP_INCLUDE_DIR}")

  set(UPNP_STATIC false PARENT_SCOPE)
  set(UPNP_INCLUDE ${MINIUPNP_INCLUDE_DIR} PARENT_SCOPE)
  set(UPNP_LIBRARIES ${MINIUPNP_LIBRARY} PARENT_SCOPE)
else()
  if(STATIC)
    message(STATUS "Using miniupnpc from local source tree for static build")
  else()
    message(STATUS "Using miniupnpc from local source tree (/external/miniupnpc)")
  endif()

  add_subdirectory(miniupnpc)

  set_property(TARGET libminiupnpc-static PROPERTY FOLDER "external")
  if(MSVC)
    set_property(TARGET libminiupnpc-static APPEND_STRING PROPERTY COMPILE_FLAGS " -wd4244 -wd4267")
  elseif(NOT MSVC)
    set_property(TARGET libminiupnpc-static APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-undef -Wno-unused-result -Wno-unused-value")
  endif()

  set(UPNP_STATIC true PARENT_SCOPE)
  set(UPNP_LIBRARIES "libminiupnpc-static" PARENT_SCOPE)
endif()


if(DEFINED ENV{REZ_USDCYCLES_ROOT})

    message(STATUS "Searching for Usd Cycles: $ENV{REZ_USDCYCLES_ROOT}/plugin/usd")

    list(APPEND CMAKE_FIND_LIBRARY_PREFIXES "")
    find_library(_usd_cycles
            NAMES
            usdCycles
            PATHS
            $ENV{REZ_USDCYCLES_ROOT}/plugin/usd
            REQUIRED
            )

    message(STATUS "Found Usd Cycles: ${_usd_cycles}")

    add_library(usdCycles SHARED IMPORTED GLOBAL)

    set_target_properties(usdCycles
            PROPERTIES
            IMPORTED_LOCATION ${_usd_cycles}
            IMPORTED_IMPLIB ${_usd_cycles}
            )

    target_include_directories(usdCycles
            INTERFACE
            $ENV{REZ_USDCYCLES_ROOT}/include
            )

    target_compile_definitions(usdCycles
            INTERFACE
            USE_USD_CYCLES_SCHEMA
            )
endif()

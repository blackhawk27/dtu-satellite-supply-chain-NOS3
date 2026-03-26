###########################################################
#
# MM App platform build setup
#
# This file is evaluated as part of the "prepare" stage
# and can be used to set up prerequisites for the build,
# such as generating header files
#
###########################################################

# The list of header files that control the CI configuration
set(MM_PLATFORM_CONFIG_FILE_LIST
  mm_internal_cfg_values.h
  mm_platform_cfg.h
  mm_msgids.h
  mm_msgid_values.h
)

foreach(MM_CFGFILE ${MM_PLATFORM_CONFIG_FILE_LIST})
  generate_config_includefile(
    FILE_NAME     "${MM_CFGFILE}"
    FALLBACK_FILE "${CMAKE_CURRENT_LIST_DIR}/config/default_${MM_CFGFILE}"
  )
endforeach()

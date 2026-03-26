###########################################################
#
# MD platform build setup
#
# This file is evaluated as part of the "prepare" stage
# and can be used to set up prerequisites for the build,
# such as generating header files
#
###########################################################

# The list of header files that control the MD configuration
set(MD_PLATFORM_CONFIG_FILE_LIST
  md_internal_cfg_values.h
  md_platform_cfg.h
  md_perfids.h
  md_msgids.h
  md_msgid_values.h
)

foreach(MD_CFGFILE ${MD_PLATFORM_CONFIG_FILE_LIST})
  generate_config_includefile(
    FILE_NAME     "${MD_CFGFILE}"
    FALLBACK_FILE "${CMAKE_CURRENT_LIST_DIR}/config/default_${MD_CFGFILE}"
  )
endforeach()
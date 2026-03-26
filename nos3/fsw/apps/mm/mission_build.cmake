# MM App mission build setup
# generate_configfile_set() is not available in this cFE version;
# replicated as an explicit foreach using generate_config_includefile().

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/docs/dox_src ${MISSION_BINARY_DIR}/docs/mm-usersguide)

set(MM_MISSION_CONFIG_FILE_LIST
  mm_extern_typedefs.h
  mm_filedefs.h
  mm_fcncode_values.h
  mm_interface_cfg_values.h
  mm_mission_cfg.h
  mm_msgdefs.h
  mm_msg.h
  mm_msgstruct.h
  mm_perfids.h
  mm_topicid_values.h
)

foreach(MM_CFGFILE ${MM_MISSION_CONFIG_FILE_LIST})
  generate_config_includefile(
    FILE_NAME     "${MM_CFGFILE}"
    FALLBACK_FILE "${CMAKE_CURRENT_LIST_DIR}/config/default_${MM_CFGFILE}"
  )
endforeach()

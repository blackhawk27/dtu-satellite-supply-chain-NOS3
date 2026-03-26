# MD App mission build setup
# generate_configfile_set() is not available in this cFE version;
# replicated as an explicit foreach using generate_config_includefile().

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/docs/dox_src ${MISSION_BINARY_DIR}/docs/md-usersguide)

set(MD_MISSION_CONFIG_FILE_LIST
  md_fcncode_values.h
  md_interface_cfg_values.h
  md_mission_cfg.h
  md_perfids.h
  md_msg.h
  md_msgdefs.h
  md_msgstruct.h
  md_tbl.h
  md_tbldefs.h
  md_tblstruct.h
  md_topicid_values.h
  md_extern_typedefs.h
)

foreach(MD_CFGFILE ${MD_MISSION_CONFIG_FILE_LIST})
  generate_config_includefile(
    FILE_NAME     "${MD_CFGFILE}"
    FALLBACK_FILE "${CMAKE_CURRENT_LIST_DIR}/config/default_${MD_CFGFILE}"
  )
endforeach()

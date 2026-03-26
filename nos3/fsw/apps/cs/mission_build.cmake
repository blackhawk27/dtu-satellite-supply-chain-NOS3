# CS App mission build setup
# generate_configfile_set() is not available in this cFE version;
# replicated as an explicit foreach using generate_config_includefile().

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/docs/dox_src ${MISSION_BINARY_DIR}/docs/cs-usersguide)

set(CS_MISSION_CONFIG_FILE_LIST
  cs_extern_typedefs.h
  cs_fcncode_values.h
  cs_interface_cfg_values.h
  cs_mission_cfg.h
  cs_msgdefs.h
  cs_msg.h
  cs_msgstruct.h
  cs_tbldefs.h
  cs_tbl.h
  cs_topicid_values.h
)

foreach(CS_CFGFILE ${CS_MISSION_CONFIG_FILE_LIST})
  generate_config_includefile(
    FILE_NAME     "${CS_CFGFILE}"
    FALLBACK_FILE "${CMAKE_CURRENT_LIST_DIR}/config/default_${CS_CFGFILE}"
  )
endforeach()

if(TARGET mm2_net)
    add_executable(netprobe main.cpp SyncReport.cpp)
    target_link_libraries(netprobe PRIVATE OpenMM2::net)
endif()

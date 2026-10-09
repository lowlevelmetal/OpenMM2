add_library(mm2_net STATIC
    Net.cpp
    BitStream.cpp
    Sha256.cpp
    Protocol.cpp
    Snapshot.cpp
    ClockSync.cpp
    Transport.cpp
    Discovery.cpp
    Session.cpp
    AmbientState.cpp
    PropState.cpp
    NatPmp.cpp
    NatPmpBackend.cpp
    UpnpBackend.cpp
    PortMapper.cpp)
target_link_libraries(mm2_net
    PUBLIC OpenMM2::core
    PRIVATE OpenMM2::enet miniupnpc::miniupnpc)
if(WIN32)
    target_link_libraries(mm2_net PRIVATE iphlpapi ws2_32)
endif()
add_library(OpenMM2::net ALIAS mm2_net)

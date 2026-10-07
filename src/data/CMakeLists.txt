add_library(mm2_data STATIC
    DatFile.cpp
    TextTables.cpp
    PeResources.cpp)
target_link_libraries(mm2_data PUBLIC OpenMM2::core)
add_library(OpenMM2::data ALIAS mm2_data)
